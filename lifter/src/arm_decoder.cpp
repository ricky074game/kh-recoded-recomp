#include "arm_decoder.h"
#include <iostream>
#include <algorithm>

namespace {
uint64_t MakeVisitKey(uint32_t address, cs_mode mode) {
    return (static_cast<uint64_t>(address) << 1) |
           ((mode == CS_MODE_THUMB) ? 1ULL : 0ULL);
}
}

ARMDecoder::ARMDecoder() : handle_arm(0), handle_thumb(0), base_address(0) {}

ARMDecoder::~ARMDecoder() {
    if (handle_arm) cs_close(&handle_arm);
    if (handle_thumb) cs_close(&handle_thumb);

    for (auto& pair : decoded_instructions) {
        if (pair.second.insn) {
            cs_free(pair.second.insn, 1);
        }
    }
}

bool ARMDecoder::Initialize() {
    if (cs_open(CS_ARCH_ARM, CS_MODE_ARM, &handle_arm) != CS_ERR_OK) {
        return false;
    }
    cs_option(handle_arm, CS_OPT_DETAIL, CS_OPT_ON);

    if (cs_open(CS_ARCH_ARM, CS_MODE_THUMB, &handle_thumb) != CS_ERR_OK) {
        return false;
    }
    cs_option(handle_thumb, CS_OPT_DETAIL, CS_OPT_ON);

    return true;
}

void ARMDecoder::LoadROM(const std::vector<uint8_t>& data, uint32_t base_addr) {
    rom_data = data;
    base_address = base_addr;
}

uint32_t ARMDecoder::Read32FromROM(uint32_t address) const {
    if (address < base_address || address + 4 > base_address + rom_data.size()) {
        return 0; // Out of bounds
    }
    uint32_t offset = address - base_address;
    return *(uint32_t*)(&rom_data[offset]);
}

cs_insn* ARMDecoder::DecodeInstruction(uint32_t address, cs_mode mode) {
    if (address < base_address || address >= base_address + rom_data.size()) {
        return nullptr;
    }

    uint32_t offset = address - base_address;
    const uint8_t* code = &rom_data[offset];

    size_t remaining_bytes = rom_data.size() - offset;
    size_t code_size = (mode == CS_MODE_THUMB) ? std::min((size_t)4, remaining_bytes) : 4;

    cs_insn* insn;
    size_t count = cs_disasm((mode == CS_MODE_THUMB) ? handle_thumb : handle_arm,
                             code, code_size, address, 1, &insn);
    if (count > 0) {
        return insn;
    }
    return nullptr;
}

bool ARMDecoder::IsBranchInstruction(cs_insn* insn) {
    switch (insn->id) {
        case ARM_INS_B:
        case ARM_INS_BL:
        case ARM_INS_BX:
        case ARM_INS_BLX:
        case ARM_INS_CBZ:
        case ARM_INS_CBNZ:
            return true;
        default:
            return false;
    }
}

uint32_t ARMDecoder::ExtractBranchTarget(cs_insn* insn, uint32_t current_addr) {
    cs_arm* arm = &(insn->detail->arm);
    (void)current_addr;
    for (int i = 0; i < arm->op_count; ++i) {
        if (arm->operands[i].type == ARM_OP_IMM) {
            return static_cast<uint32_t>(arm->operands[i].imm);
        }
    }
    return 0xFFFFFFFF;
}

void ARMDecoder::AnalyzeControlFlow(uint32_t entry_point) {
    cs_mode initial_mode = IsThumb(entry_point) ? CS_MODE_THUMB : CS_MODE_ARM;
    uint32_t initial_addr = ResolveTarget(entry_point);

    analysis_queue.push_back({initial_addr, initial_mode});

    auto enqueue_target = [&](uint32_t target_addr, bool force_thumb = false) {
        if (target_addr == 0xFFFFFFFF) {
            return;
        }
        cs_mode target_mode = force_thumb ? CS_MODE_THUMB : (IsThumb(target_addr) ? CS_MODE_THUMB : CS_MODE_ARM);
        uint32_t real_target = ResolveTarget(target_addr);
        if (real_target < base_address || real_target >= base_address + rom_data.size()) {
            return;
        }
        if (visited_addresses.find(MakeVisitKey(real_target, target_mode)) == visited_addresses.end()) {
            analysis_queue.push_back({real_target, target_mode});
        }
    };

    auto loads_pc_via_stack_or_ldm = [](cs_insn* insn) {
        switch (insn->id) {
            case ARM_INS_POP:
            case ARM_INS_LDM:
            case ARM_INS_LDMIB: 
            case ARM_INS_LDMDA:
            case ARM_INS_LDMDB:
                break;
            default:
                return false;
        }

        cs_arm* arm = &(insn->detail->arm);
        for (int i = 0; i < arm->op_count; ++i) {
            if (arm->operands[i].type == ARM_OP_REG && arm->operands[i].reg == ARM_REG_PC) {
                return true;
            }
        }
        return false;
    };

    while (!analysis_queue.empty()) {
        auto current = analysis_queue.back();
        analysis_queue.pop_back();

        uint32_t current_addr = current.first;
        cs_mode current_mode = current.second;

        while (current_addr < base_address + rom_data.size()) {
            uint64_t visit_key = MakeVisitKey(current_addr, current_mode);
            if (visited_addresses.find(visit_key) != visited_addresses.end()) {
                break;
            }

            cs_insn* insn = DecodeInstruction(current_addr, current_mode);
            if (!insn) break;

            visited_addresses.insert(visit_key);

            auto existing = decoded_instructions.find(current_addr);
            if (existing != decoded_instructions.end() && existing->second.insn != nullptr) {
                if (existing->second.mode != current_mode) {
                    // When both modes decode the same address, prefer Thumb.
                    // This avoids ARM-mode overwrites of known Thumb regions.
                    if (existing->second.mode == CS_MODE_THUMB && current_mode == CS_MODE_ARM) {
                        cs_free(insn, 1);
                        // Do not continue linear ARM decode through a known Thumb region.
                        break;
                    }
                    if (existing->second.mode == CS_MODE_ARM && current_mode == CS_MODE_THUMB) {
                        cs_free(existing->second.insn, 1);
                        decoded_instructions[current_addr] = {current_addr, current_mode, insn};
                    }
                } else {
                    cs_free(existing->second.insn, 1);
                    decoded_instructions[current_addr] = {current_addr, current_mode, insn};
                }
            } else {
                decoded_instructions[current_addr] = {current_addr, current_mode, insn};
            }

            if (IsBranchInstruction(insn)) {
                uint32_t target_addr = ExtractBranchTarget(insn, current_addr);
                if (target_addr != 0xFFFFFFFF) {
                    bool force_thumb = false;
                    if (current_mode == CS_MODE_THUMB && insn->id != ARM_INS_BLX) {
                        // Thumb immediate branches keep Thumb state even when target bit0 is clear.
                        force_thumb = true;
                    } else if (insn->id == ARM_INS_BLX) {
                        cs_arm* arm = &(insn->detail->arm);
                        if (arm->op_count > 0 && arm->operands[0].type == ARM_OP_IMM) {
                            force_thumb = true;
                        }
                    }
                    enqueue_target(target_addr, force_thumb);
                }

                // Resolve common boot pattern: ldr Rx, [pc, #imm] ... bx Rx
                if ((insn->id == ARM_INS_BX || insn->id == ARM_INS_BLX) && current_mode == CS_MODE_ARM) {
                    cs_arm* arm = &(insn->detail->arm);
                    if (arm->op_count > 0 && arm->operands[0].type == ARM_OP_REG) {
                        int branch_reg = arm->operands[0].reg;

                        for (int back = 1; back <= 2; ++back) {
                            uint32_t prev_addr = current_addr - (4u * static_cast<uint32_t>(back));
                            auto prev_it = decoded_instructions.find(prev_addr);
                            if (prev_it == decoded_instructions.end() || !prev_it->second.insn) {
                                continue;
                            }

                            cs_insn* prev_insn = prev_it->second.insn;
                            if (prev_insn->id != ARM_INS_LDR) {
                                continue;
                            }

                            cs_arm* prev_arm = &(prev_insn->detail->arm);
                            if (prev_arm->op_count < 2) {
                                continue;
                            }
                            if (prev_arm->operands[0].type != ARM_OP_REG || prev_arm->operands[0].reg != branch_reg) {
                                continue;
                            }
                            if (prev_arm->operands[1].type != ARM_OP_MEM || prev_arm->operands[1].mem.base != ARM_REG_PC) {
                                continue;
                            }

                            bool prev_thumb = (prev_it->second.mode == CS_MODE_THUMB) || IsThumb(prev_insn->address);
                            uint32_t literal_addr = ResolvePCRelative(
                                static_cast<uint32_t>(prev_insn->address),
                                prev_arm->operands[1].mem.disp,
                                prev_thumb);
                            uint32_t literal_target = Read32FromROM(literal_addr);
                            enqueue_target(literal_target);
                            break;
                        }
                    }
                }

                if (insn->id == ARM_INS_B || insn->id == ARM_INS_BX) {
                    cs_arm* arm = &(insn->detail->arm);
                    if (arm->cc == ARM_CC_AL || arm->cc == ARM_CC_INVALID) break;
                }
            }
            if (loads_pc_via_stack_or_ldm(insn)) {
                 break;
            }
            current_addr += insn->size;
        }
    }
}
