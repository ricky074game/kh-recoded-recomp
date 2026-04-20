#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <iomanip>
#include <filesystem>
#include <set>

#include "arm_decoder.h"
#include "c_emitter.h"

namespace fs = std::filesystem;

static std::vector<uint8_t> ReadBinaryFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

const int CHUNK_SIZE = 2000;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <binary.bin> <output_dir> <base_address> <entry_point> [overlay_id]\n";
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_dir = (argc > 2) ? argv[2] : "generated";
    uint32_t base_address = (argc > 3) ? std::stoul(argv[3], nullptr, 16) : 0x02000000;
    uint32_t entry_point = (argc > 4) ? std::stoul(argv[4], nullptr, 16) : base_address;
    int overlay_id = (argc > 5) ? std::stoi(argv[5]) : -1;

    fs::create_directories(output_dir);

    auto rom_data = ReadBinaryFile(input_path);
    if (rom_data.empty()) return 1;

    ARMDecoder decoder;
    if (!decoder.Initialize()) return 1;
    decoder.LoadROM(rom_data, base_address);
    decoder.AnalyzeControlFlow(entry_point);

    // Seed a minimal late helper that may be reached through dynamic call stubs
    // during boot bring-up but is not always discovered from the entry CFG.
    if (overlay_id < 0) {
        decoder.AnalyzeControlFlow(0x02029B55);
        // Late boot path branches into this Thumb helper via indirect call/data
        // patterns that are not always discoverable from the root entry walk.
        decoder.AnalyzeControlFlow(0x0202B455);
        // Early boot occasionally dispatches into this Thumb path through
        // indirect branch/data flow that the root walk may miss.
        decoder.AnalyzeControlFlow(0x0203A85B);
        // Another early Thumb target reached through sparse dynamic stubs.
        decoder.AnalyzeControlFlow(0x0202ABA9);
    }

    const auto& instructions = decoder.GetDecodedInstructions();
    std::string prefix = (overlay_id >= 0) ? "overlay_" + std::to_string(overlay_id) : "arm9";

    if (instructions.empty()) {
        std::cerr << "[Warning] No instructions decoded for " << prefix << ". Binary might be data-only." << std::endl;
        // Proceed anyway to generate empty registration
    }

    CEmitter emitter(&decoder);

    auto it = instructions.begin();
    int chunk_idx = 0;

    while (it != instructions.end()) {
        std::set<uint32_t> chunk_addrs;
        auto temp_it = it;
        for (int i = 0; i < CHUNK_SIZE && temp_it != instructions.end(); ++i, ++temp_it) {
            chunk_addrs.insert(temp_it->first);
        }
        
        emitter.SetChunkRange(chunk_addrs);

        std::string chunk_name = prefix + "_chunk_" + std::to_string(chunk_idx) + ".cpp";
        std::string chunk_path = (fs::path(output_dir) / chunk_name).string();
        std::ofstream out(chunk_path);
        
        out << "#include \"cpu_context.h\"\n#include \"memory_map.h\"\n#include \"optimization.h\"\n#include \"ds_debug.h\"\n#include \"hw_bios.h\"\n\n";
        out << "void " << prefix << "_chunk_" << chunk_idx << "(CPU_Context* ctx) {\n";
        out << "    switch (ctx->dispatch_pc) {\n";
        
        auto sw_it = it;
        for (int i = 0; i < CHUNK_SIZE && sw_it != instructions.end(); ++i, ++sw_it) {
            out << "        case 0x" << std::hex << std::uppercase << sw_it->first << ": goto block_0x" << sw_it->first << ";\n";
        }
        out << "        default: return;\n";
        out << "    }\n";
        
        for (int i = 0; i < CHUNK_SIZE && it != instructions.end(); ++i, ++it) {
            uint32_t addr = it->first;
            auto next_it = std::next(it);
            out << "block_0x" << std::hex << std::uppercase << addr << ":\n";
            out << CEmitter::EmitDSAddr(addr);
            std::string code = emitter.EmitInstruction(it->second.insn);
            out << code;

            // Guard against sparse decode maps: if the next decoded instruction is
            // not the architectural fallthrough, do not fall through to an
            // unrelated block label in this chunk.
            {
                uint32_t fallthrough_addr = addr + static_cast<uint32_t>(it->second.insn->size);
                if (next_it == instructions.end()) {
                    out << "    ctx->r[15] = ctx->r[14]; return;\n";
                } else if (next_it->first != fallthrough_addr) {
                    // Sparse decode hole: continue from architectural fallthrough.
                    // Dispatching to the next decoded key can jump into unrelated code/data islands.
                    out << "    ctx->r[15] = 0x" << std::hex << std::uppercase
                        << fallthrough_addr << "; return;\n";
                }
            }
        }

        if (it != instructions.end()) {
            uint32_t next_addr = it->first;
            out << "    ctx->r[15] = 0x" << std::hex << next_addr << "; return;\n";
        }
        out << "}\n";
        chunk_idx++;
    }

    std::string reg_name = prefix + "_reg.cpp";
    std::ofstream reg( (fs::path(output_dir) / reg_name).string() );
    reg << "#include \"hw_overlay.h\"\n#include \"memory_map.h\"\n\n";
    for (int i = 0; i < chunk_idx; ++i) {
        reg << "void " << prefix << "_chunk_" << i << "(CPU_Context* ctx);\n";
    }
    reg << "\nvoid " << prefix << "_register(NDSMemory* mem) {\n";
    auto reg_it = instructions.begin();
    for (int i = 0; i < chunk_idx; ++i) {
        for (int j = 0; j < CHUNK_SIZE && reg_it != instructions.end(); ++j, ++reg_it) {
            uint32_t addr = reg_it->first;
            if (overlay_id >= 0) {
                reg << "    mem->GetOverlayManager().RegisterOverlayFunction(" << std::dec << overlay_id << ", 0x" << std::hex << addr << ", " << prefix << "_chunk_" << i << ");\n";
            } else {
                reg << "    mem->GetOverlayManager().RegisterStaticFunction(0x" << std::hex << addr << ", " << prefix << "_chunk_" << i << ");\n";
            }
        }
    }
    reg << "}\n";

    return 0;
}
