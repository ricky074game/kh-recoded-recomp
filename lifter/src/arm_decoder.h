#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <capstone/capstone.h>

struct InstructionNode {
    uint32_t address;
    cs_mode mode;
    cs_insn* insn;
};

class ARMDecoder {
private:
    csh handle_arm;
    csh handle_thumb;
    std::vector<uint8_t> rom_data;
    uint32_t base_address;

    std::map<uint32_t, InstructionNode> decoded_instructions;
    std::set<uint64_t> visited_addresses;

    // Control Flow structures
    std::vector<std::pair<uint32_t, cs_mode>> analysis_queue;

public:
    ARMDecoder();
    ~ARMDecoder();

    bool Initialize();
    void LoadROM(const std::vector<uint8_t>& data, uint32_t base_addr);

    // Core analysis pass
    void AnalyzeControlFlow(uint32_t entry_point);

    // Helpers
    cs_insn* DecodeInstruction(uint32_t address, cs_mode mode);
    bool IsBranchInstruction(cs_insn* insn);
    uint32_t ExtractBranchTarget(cs_insn* insn, uint32_t current_addr);
    uint32_t Read32FromROM(uint32_t address) const;

    const std::map<uint32_t, InstructionNode>& GetDecodedInstructions() const { return decoded_instructions; }

    // Static analysis helpers for branch/literal-pool handling.
    static bool IsThumb(uint32_t address) { return (address & 1) != 0; }
    static uint32_t ResolveTarget(uint32_t address) { return address & ~1; }
    static uint32_t ResolvePCRelative(uint32_t pc, uint32_t offset, bool is_thumb) {
        uint32_t pc_offset = is_thumb ? 4 : 8;
        return ((pc + pc_offset) & ~3) + offset;
    }
};
