#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <capstone/capstone.h>

static std::vector<uint8_t> ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }
    const auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

static void DumpDisasm(const std::vector<uint8_t>& data,
                       uint32_t base,
                       uint32_t addr,
                       cs_mode mode,
                       size_t count) {
    if (addr < base) {
        return;
    }
    const size_t offset = static_cast<size_t>(addr - base);
    if (offset >= data.size()) {
        return;
    }

    csh handle = 0;
    if (cs_open(CS_ARCH_ARM, mode, &handle) != CS_ERR_OK) {
        std::cerr << "cs_open failed\n";
        return;
    }
    cs_option(handle, CS_OPT_DETAIL, CS_OPT_OFF);

    cs_insn* insn = nullptr;
    const uint8_t* code = data.data() + offset;
    const size_t size = std::min<size_t>(64, data.size() - offset);
    const size_t n = cs_disasm(handle, code, size, addr, count, &insn);

    std::cout << (mode == CS_MODE_ARM ? "ARM " : "THMB") << " @ 0x"
              << std::hex << std::setw(8) << std::setfill('0') << addr << "\n";
    if (n == 0) {
        std::cout << "  <no decode>\n";
    } else {
        for (size_t i = 0; i < n; ++i) {
            std::cout << "  0x" << std::hex << std::setw(8) << std::setfill('0')
                      << static_cast<uint32_t>(insn[i].address) << ": "
                      << insn[i].mnemonic << " " << insn[i].op_str << "\n";
        }
    }
    std::cout << "\n";

    cs_free(insn, n);
    cs_close(&handle);
}

int main() {
    const uint32_t base = 0x02000000u;
    const auto data = ReadFile("recoded/arm9.bin");
    if (data.empty()) {
        std::cerr << "failed to read arm9.bin\n";
        return 1;
    }

    const uint32_t addrs[] = {
        0x02025464u,
        0x02025470u,
        0x020254B8u,
        0x020254E8u,
        0x0202502Eu,
        0x02025040u,
        0x020252F6u,
        0x020288A0u,
        0x0202A1E4u,
        0x0202A1F8u,
        0x0202A230u,
        0x0202A240u,
        0x0202A28Eu,
        0x0202A714u,
        0x0202A424u,
        0x0202A448u,
        0x0202ABC8u,
        0x02010690u,
        0x020109E4u,
        0x02010A08u,
    };

    for (uint32_t addr : addrs) {
        DumpDisasm(data, base, addr, CS_MODE_ARM, 6);
        DumpDisasm(data, base, addr, CS_MODE_THUMB, 8);
    }

    return 0;
}
