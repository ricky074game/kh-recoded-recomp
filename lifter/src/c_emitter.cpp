#include "c_emitter.h"
#include "arm_decoder.h"
#include <iomanip>

// ============================================================================
// c_emitter.cpp — C++ Code Emitter Implementation
// ============================================================================

// ---- Helper: Convert ARM register enum to index (0-15) ----
static int RegIndex(unsigned int reg) {
    if (reg >= ARM_REG_R0 && reg <= ARM_REG_R12) return reg - ARM_REG_R0;
    if (reg == ARM_REG_SB) return 9;
    if (reg == ARM_REG_SL) return 10;
    if (reg == ARM_REG_FP) return 11;
    if (reg == ARM_REG_IP) return 12;
    if (reg == ARM_REG_SP || reg == ARM_REG_R13) return 13;
    if (reg == ARM_REG_LR || reg == ARM_REG_R14) return 14;
    if (reg == ARM_REG_PC || reg == ARM_REG_R15) return 15;
    return -1;
}

static std::string OperandToExpression(const cs_arm_op& operand) {
    std::string base_expr = "0";

    if (operand.type == ARM_OP_REG) {
        int reg = RegIndex(operand.reg);
        if (reg >= 0) {
            base_expr = "ctx->r[" + std::to_string(reg) + "]";
        }
    } else if (operand.type == ARM_OP_IMM) {
        std::stringstream ss;
        ss << "0x" << std::hex << static_cast<uint32_t>(operand.imm);
        base_expr = ss.str();
    }

    if (operand.shift.type != ARM_SFT_INVALID) {
        unsigned int shift_val = operand.shift.value;
        switch (operand.shift.type) {
            case ARM_SFT_LSL:
                if (shift_val >= 32) {
                    return "0u";
                }
                return "(" + base_expr + " << " + std::to_string(shift_val) + ")";
            case ARM_SFT_LSR:
                if (shift_val >= 32) {
                    return "0u";
                }
                return "((uint32_t)(" + base_expr + ") >> " + std::to_string(shift_val) + ")";
            case ARM_SFT_ASR:
                if (shift_val >= 32) {
                    return "((" + base_expr + " & 0x80000000u) ? 0xFFFFFFFFu : 0u)";
                }
                return "((int32_t)(" + base_expr + ") >> " + std::to_string(shift_val) + ")";
            case ARM_SFT_ROR: {
                if ((shift_val & 31u) == 0u) {
                    return base_expr;
                }
                std::string sv = std::to_string(shift_val);
                return "((" + base_expr + " >> " + sv + ") | (" + base_expr + " << (32 - " + sv + ")))";
            }
            default:
                break;
        }
    }

    return base_expr;
}

static std::string BuildMemOffsetExpr(const cs_arm_op& mem_operand) {
    std::string offset_expr = "0";

    if (mem_operand.type != ARM_OP_MEM) {
        return offset_expr;
    }

    if (mem_operand.mem.index != ARM_REG_INVALID) {
        int index_reg = RegIndex(mem_operand.mem.index);
        if (index_reg >= 0) {
            offset_expr = "ctx->r[" + std::to_string(index_reg) + "]";

            if (mem_operand.mem.lshift > 0) {
                offset_expr = "(" + offset_expr + " << " + std::to_string(mem_operand.mem.lshift) + ")";
            }

            if (mem_operand.mem.scale == -1) {
                offset_expr = "-(" + offset_expr + ")";
            } else if (mem_operand.mem.scale != 0 && mem_operand.mem.scale != 1) {
                offset_expr = "(" + offset_expr + " * " + std::to_string(mem_operand.mem.scale) + ")";
            }
        }
    }

    if (mem_operand.mem.disp != 0) {
        const int disp = mem_operand.mem.disp;
        const int abs_disp = (disp < 0) ? -disp : disp;
        if (offset_expr == "0") {
            offset_expr = (disp < 0) ? ("-" + std::to_string(abs_disp)) : std::to_string(abs_disp);
        } else if (disp < 0) {
            offset_expr = "(" + offset_expr + " - " + std::to_string(abs_disp) + ")";
        } else {
            offset_expr = "(" + offset_expr + " + " + std::to_string(abs_disp) + ")";
        }
    }

    if (mem_operand.subtracted && offset_expr != "0") {
        offset_expr = "-(" + offset_expr + ")";
    }

    return offset_expr;
}

static std::string BuildPostIndexOffsetExpr(cs_arm* arm, int mem_operand_index) {
    if (arm->post_index && arm->op_count > mem_operand_index + 1) {
        const cs_arm_op& wb_operand = arm->operands[mem_operand_index + 1];
        if (wb_operand.type == ARM_OP_REG || wb_operand.type == ARM_OP_IMM) {
            std::string wb_expr = OperandToExpression(wb_operand);
            if (wb_operand.subtracted) {
                wb_expr = "-(" + wb_expr + ")";
            }
            return wb_expr;
        }
    }

    return BuildMemOffsetExpr(arm->operands[mem_operand_index]);
}

static std::string AddOffsetExpr(const std::string& base_expr, const std::string& offset_expr) {
    if (offset_expr == "0") {
        return base_expr;
    }
    return "(" + base_expr + " + " + offset_expr + ")";
}

static bool IsThumbDecodedInstruction(const ARMDecoder* decoder, const cs_insn* insn) {
    if (decoder != nullptr) {
        const auto& decoded = decoder->GetDecodedInstructions();
        auto it = decoded.find(static_cast<uint32_t>(insn->address));
        if (it != decoded.end()) {
            return it->second.mode == CS_MODE_THUMB;
        }
    }

    // Fallback heuristic when decode metadata is unavailable.
    return insn->size == 2;
}

// ---- DS_ADDR Debug Macro ----
std::string CEmitter::EmitDSAddr(uint32_t rom_address) {
    std::stringstream ss;
    ss << "DS_ADDR(0x" << std::hex << std::uppercase << std::setfill('0')
       << std::setw(8) << rom_address << ");\n";
    return ss.str();
}

// ---- Condition Code Wrapping ----
std::string CEmitter::EmitCondition(arm_cc cc) {
    if (cc == ARM_CC_AL || cc == ARM_CC_INVALID) return "";
    return "    if (CheckConditionCode(ctx, " + std::to_string(cc) + ")) {\n";
}

// ---- Barrel Shifter ----
std::string CEmitter::EmitShift(cs_arm* arm, int op_index, const std::string& base_val) {
    if (arm->operands[op_index].shift.type != ARM_SFT_INVALID) {
        unsigned int shift_val = arm->operands[op_index].shift.value;
        switch (arm->operands[op_index].shift.type) {
            case ARM_SFT_LSL:
                if (shift_val >= 32) return "0u";
                return "(" + base_val + " << " + std::to_string(shift_val) + ")";
            case ARM_SFT_LSR:
                if (shift_val >= 32) return "0u";
                return "(uint32_t)(" + base_val + ") >> " + std::to_string(shift_val);
            case ARM_SFT_ASR:
                if (shift_val >= 32) {
                    return "((" + base_val + " & 0x80000000u) ? 0xFFFFFFFFu : 0u)";
                }
                return "(int32_t)(" + base_val + ") >> " + std::to_string(shift_val);
            case ARM_SFT_ROR: {
                if ((shift_val & 31u) == 0u) return base_val;
                std::string sv = std::to_string(shift_val);
                return "((" + base_val + " >> " + sv + ") | (" + base_val + " << (32 - " + sv + ")))";
            }
            default: break;
        }
    }
    return base_val;
}

// ---- Operand Processing ----
std::string CEmitter::ProcessOperand(cs_arm* arm, int op_index) {
    std::string val = "0";
    if (arm->operands[op_index].type == ARM_OP_REG) {
        int reg = RegIndex(arm->operands[op_index].reg);
        if (reg >= 0) {
            val = "ctx->r[" + std::to_string(reg) + "]";
        }
    } else if (arm->operands[op_index].type == ARM_OP_IMM) {
        std::stringstream ss;
        ss << "0x" << std::hex << static_cast<uint32_t>(arm->operands[op_index].imm);
        val = ss.str();
    }
    return "(" + EmitShift(arm, op_index, val) + ")";
}

// ============================================================================
// Data Processing Instruction Emitters
// ============================================================================

void CEmitter::EmitADD(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = RegIndex(arm->operands[0].reg);
    if (dest < 0) {
        return;
    }
    std::string src1;
    std::string val2;
    if (arm->op_count >= 3) {
        src1 = ProcessOperand(arm, 1);
        val2 = ProcessOperand(arm, 2);
    } else if (arm->op_count >= 2) {
        src1 = "ctx->r[" + std::to_string(dest) + "]";
        val2 = ProcessOperand(arm, 1);
    } else {
        return;
    }

    if (arm->update_flags) {
        out << "    CalculateFlags_ADD(ctx, " << src1 << ", " << val2 << ");\n";
    }
    out << "    ctx->r[" << dest << "] = " << src1 << " + " << val2 << ";\n";
}

void CEmitter::EmitADC(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = RegIndex(arm->operands[0].reg);
    if (dest < 0) {
        return;
    }
    std::string src1 = ProcessOperand(arm, 1);
    std::string val2 = ProcessOperand(arm, 2);
    const std::string carry_expr = "((ctx->cpsr & CPSRFlags::C) ? 1u : 0u)";

    if (arm->update_flags) {
        out << "    CalculateFlags_ADD(ctx, " << src1 << ", (" << val2
            << " + " << carry_expr << "));\n";
    }
    out << "    ctx->r[" << dest << "] = " << src1 << " + " << val2
        << " + " << carry_expr << ";\n";
}

void CEmitter::EmitSUB(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = RegIndex(arm->operands[0].reg);
    if (dest < 0) {
        return;
    }
    std::string src1;
    std::string val2;
    if (arm->op_count >= 3) {
        src1 = ProcessOperand(arm, 1);
        val2 = ProcessOperand(arm, 2);
    } else if (arm->op_count >= 2) {
        src1 = "ctx->r[" + std::to_string(dest) + "]";
        val2 = ProcessOperand(arm, 1);
    } else {
        return;
    }

    if (arm->update_flags) {
        out << "    CalculateFlags_SUB(ctx, " << src1 << ", " << val2 << ");\n";
    }
    out << "    ctx->r[" << dest << "] = " << src1 << " - " << val2 << ";\n";
}

void CEmitter::EmitLSL(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    if (arm->op_count < 2 || arm->operands[0].type != ARM_OP_REG) return;

    int dest = RegIndex(arm->operands[0].reg);
    bool custom_imm_lsl = false;
    uint32_t custom_shift = 0;
    std::string custom_src;
    std::string shifted;
    if (arm->op_count >= 3 && arm->operands[1].type == ARM_OP_REG &&
        arm->operands[1].shift.type == ARM_SFT_INVALID &&
        arm->operands[2].type == ARM_OP_IMM) {
        custom_imm_lsl = true;
        custom_src = OperandToExpression(arm->operands[1]);
        custom_shift = static_cast<uint32_t>(arm->operands[2].imm) & 0xFFu;
        if (custom_shift >= 32u) {
            shifted = "0u";
        } else {
            shifted = "(" + custom_src + " << " + std::to_string(custom_shift) + ")";
        }
    } else {
        shifted = ProcessOperand(arm, 1);
    }

    out << "    ctx->r[" << dest << "] = " << shifted << ";\n";
    if (arm->update_flags) {
        out << "    CalculateFlags_MOV(ctx, ctx->r[" << dest << "]);\n";
        if (custom_imm_lsl && custom_shift != 0u) {
            if (custom_shift <= 32u) {
                const uint32_t bit = 32u - custom_shift;
                out << "    if ((" << custom_src << " >> " << bit
                    << ") & 1u) ctx->cpsr |= CPSRFlags::C; else ctx->cpsr &= ~CPSRFlags::C;\n";
            } else {
                out << "    ctx->cpsr &= ~CPSRFlags::C;\n";
            }
        }
    }
}

void CEmitter::EmitLSR(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    if (arm->op_count < 2 || arm->operands[0].type != ARM_OP_REG) return;

    int dest = RegIndex(arm->operands[0].reg);
    std::string shifted = ProcessOperand(arm, 1);

    out << "    ctx->r[" << dest << "] = " << shifted << ";\n";
    if (arm->update_flags) {
        out << "    CalculateFlags_MOV(ctx, ctx->r[" << dest << "]);\n";
    }
}

void CEmitter::EmitASR(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    if (arm->op_count < 2 || arm->operands[0].type != ARM_OP_REG) return;

    int dest = RegIndex(arm->operands[0].reg);
    std::string shifted = ProcessOperand(arm, 1);

    out << "    ctx->r[" << dest << "] = " << shifted << ";\n";
    if (arm->update_flags) {
        out << "    CalculateFlags_MOV(ctx, ctx->r[" << dest << "]);\n";
    }
}

void CEmitter::EmitROR(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    if (arm->op_count < 2 || arm->operands[0].type != ARM_OP_REG) return;

    int dest = RegIndex(arm->operands[0].reg);
    std::string shifted = ProcessOperand(arm, 1);

    out << "    ctx->r[" << dest << "] = " << shifted << ";\n";
    if (arm->update_flags) {
        out << "    CalculateFlags_MOV(ctx, ctx->r[" << dest << "]);\n";
    }
}

void CEmitter::EmitRSB(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = RegIndex(arm->operands[0].reg);
    int src1 = RegIndex(arm->operands[1].reg);
    std::string val2 = ProcessOperand(arm, 2);

    if (arm->update_flags) {
        out << "    CalculateFlags_SUB(ctx, " << val2 << ", ctx->r[" << src1 << "]);\n";
    }
    out << "    ctx->r[" << dest << "] = " << val2 << " - ctx->r[" << src1 << "];\n";
}

void CEmitter::EmitRSC(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = RegIndex(arm->operands[0].reg);
    std::string src1 = ProcessOperand(arm, 1);
    std::string val2 = ProcessOperand(arm, 2);
    const std::string carry_expr = "((ctx->cpsr & CPSRFlags::C) ? 1u : 0u)";
    const std::string borrow_expr = "(1u - " + carry_expr + ")";
    const std::string subtrahend_expr = "(" + src1 + " + " + borrow_expr + ")";

    if (arm->update_flags) {
        out << "    CalculateFlags_SUB(ctx, " << val2 << ", " << subtrahend_expr << ");\n";
    }
    out << "    ctx->r[" << dest << "] = " << val2 << " - " << src1
        << " - " << borrow_expr << ";\n";
}

void CEmitter::EmitSBC(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = RegIndex(arm->operands[0].reg);
    if (dest < 0) {
        return;
    }

    std::string src1;
    std::string val2;
    if (arm->op_count >= 3) {
        src1 = ProcessOperand(arm, 1);
        val2 = ProcessOperand(arm, 2);
    } else if (arm->op_count >= 2) {
        src1 = "ctx->r[" + std::to_string(dest) + "]";
        val2 = ProcessOperand(arm, 1);
    } else {
        return;
    }

    const std::string carry_expr = "((ctx->cpsr & CPSRFlags::C) ? 1u : 0u)";
    const std::string borrow_expr = "(1u - " + carry_expr + ")";
    const std::string subtrahend_expr = "(" + val2 + " + " + borrow_expr + ")";

    if (arm->update_flags) {
        out << "    CalculateFlags_SUB(ctx, " << src1 << ", " << subtrahend_expr << ");\n";
    }
    out << "    ctx->r[" << dest << "] = " << src1 << " - " << val2
        << " - " << borrow_expr << ";\n";
}

void CEmitter::EmitMOV(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = RegIndex(arm->operands[0].reg);
    std::string val = ProcessOperand(arm, 1);

    if (arm->update_flags) {
        out << "    CalculateFlags_MOV(ctx, " << val << ");\n";
    }
    out << "    ctx->r[" << dest << "] = " << val << ";\n";
}

void CEmitter::EmitMVN(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = RegIndex(arm->operands[0].reg);
    std::string val = ProcessOperand(arm, 1);

    if (arm->update_flags) {
        out << "    CalculateFlags_MOV(ctx, ~(" << val << "));\n";
    }
    out << "    ctx->r[" << dest << "] = ~(" << val << ");\n";
}

void CEmitter::EmitAND(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = RegIndex(arm->operands[0].reg);
    int src1 = RegIndex(arm->operands[1].reg);
    std::string val2 = ProcessOperand(arm, 2);

    out << "    ctx->r[" << dest << "] = ctx->r[" << src1 << "] & " << val2 << ";\n";
    if (arm->update_flags) {
        out << "    CalculateFlags_MOV(ctx, ctx->r[" << dest << "]);\n";
    }
}

void CEmitter::EmitORR(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = RegIndex(arm->operands[0].reg);
    int src1 = RegIndex(arm->operands[1].reg);
    std::string val2 = ProcessOperand(arm, 2);

    out << "    ctx->r[" << dest << "] = ctx->r[" << src1 << "] | " << val2 << ";\n";
    if (arm->update_flags) {
        out << "    CalculateFlags_MOV(ctx, ctx->r[" << dest << "]);\n";
    }
}

void CEmitter::EmitEOR(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = RegIndex(arm->operands[0].reg);
    int src1 = RegIndex(arm->operands[1].reg);
    std::string val2 = ProcessOperand(arm, 2);

    out << "    ctx->r[" << dest << "] = ctx->r[" << src1 << "] ^ " << val2 << ";\n";
    if (arm->update_flags) {
        out << "    CalculateFlags_MOV(ctx, ctx->r[" << dest << "]);\n";
    }
}

void CEmitter::EmitBIC(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = RegIndex(arm->operands[0].reg);
    int src1 = RegIndex(arm->operands[1].reg);
    std::string val2 = ProcessOperand(arm, 2);

    out << "    ctx->r[" << dest << "] = ctx->r[" << src1 << "] & ~(" << val2 << ");\n";
    if (arm->update_flags) {
        out << "    CalculateFlags_MOV(ctx, ctx->r[" << dest << "]);\n";
    }
}

void CEmitter::EmitCMP(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int src1 = RegIndex(arm->operands[0].reg);
    std::string val2 = ProcessOperand(arm, 1);
    out << "    CalculateFlags_SUB(ctx, ctx->r[" << src1 << "], " << val2 << ");\n";
}

void CEmitter::EmitCMN(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int src1 = RegIndex(arm->operands[0].reg);
    std::string val2 = ProcessOperand(arm, 1);
    out << "    CalculateFlags_ADD(ctx, ctx->r[" << src1 << "], " << val2 << ");\n";
}

void CEmitter::EmitTST(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int src1 = RegIndex(arm->operands[0].reg);
    std::string val2 = ProcessOperand(arm, 1);
    out << "    CalculateFlags_MOV(ctx, ctx->r[" << src1 << "] & " << val2 << ");\n";
}

void CEmitter::EmitTEQ(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int src1 = RegIndex(arm->operands[0].reg);
    std::string val2 = ProcessOperand(arm, 1);
    out << "    CalculateFlags_MOV(ctx, ctx->r[" << src1 << "] ^ " << val2 << ");\n";
}

// ============================================================================
// Multiply Emitters
// ============================================================================

void CEmitter::EmitMUL(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = RegIndex(arm->operands[0].reg);
    int src1 = RegIndex(arm->operands[1].reg);
    int src2 = RegIndex(arm->operands[2].reg);
    out << "    ctx->r[" << dest << "] = ctx->r[" << src1 << "] * ctx->r[" << src2 << "];\n";
    if (arm->update_flags) {
        out << "    CalculateFlags_MOV(ctx, ctx->r[" << dest << "]);\n";
    }
}

void CEmitter::EmitMLA(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = RegIndex(arm->operands[0].reg);
    int src1 = RegIndex(arm->operands[1].reg);
    int src2 = RegIndex(arm->operands[2].reg);
    int acc  = RegIndex(arm->operands[3].reg);
    out << "    ctx->r[" << dest << "] = ctx->r[" << src1 << "] * ctx->r["
        << src2 << "] + ctx->r[" << acc << "];\n";
    if (arm->update_flags) {
        out << "    CalculateFlags_MOV(ctx, ctx->r[" << dest << "]);\n";
    }
}

void CEmitter::EmitSMLAWB(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    if (arm->op_count < 4) {
        return;
    }

    int dest = RegIndex(arm->operands[0].reg);
    int src_word = RegIndex(arm->operands[1].reg);
    int src_half = RegIndex(arm->operands[2].reg);
    int acc = RegIndex(arm->operands[3].reg);
    if (dest < 0 || src_word < 0 || src_half < 0 || acc < 0) {
        return;
    }

    out << "    {\n";
    out << "    int64_t _mul = static_cast<int64_t>(static_cast<int32_t>(ctx->r[" << src_word
        << "])) * static_cast<int64_t>(static_cast<int16_t>(ctx->r[" << src_half << "] & 0xFFFFu));\n";
    out << "    int64_t _acc = static_cast<int64_t>(static_cast<int32_t>(ctx->r[" << acc << "]));\n";
    out << "    ctx->r[" << dest << "] = static_cast<uint32_t>(static_cast<int32_t>((_mul >> 16) + _acc));\n";
    out << "    }\n";
}

void CEmitter::EmitSMLAWT(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    if (arm->op_count < 4) {
        return;
    }

    int dest = RegIndex(arm->operands[0].reg);
    int src_word = RegIndex(arm->operands[1].reg);
    int src_half = RegIndex(arm->operands[2].reg);
    int acc = RegIndex(arm->operands[3].reg);
    if (dest < 0 || src_word < 0 || src_half < 0 || acc < 0) {
        return;
    }

    out << "    {\n";
    out << "    int64_t _mul = static_cast<int64_t>(static_cast<int32_t>(ctx->r[" << src_word
        << "])) * static_cast<int64_t>(static_cast<int16_t>((ctx->r[" << src_half << "] >> 16) & 0xFFFFu));\n";
    out << "    int64_t _acc = static_cast<int64_t>(static_cast<int32_t>(ctx->r[" << acc << "]));\n";
    out << "    ctx->r[" << dest << "] = static_cast<uint32_t>(static_cast<int32_t>((_mul >> 16) + _acc));\n";
    out << "    }\n";
}

// ============================================================================
// Memory Access Emitters
// ============================================================================

void CEmitter::EmitLDR(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = RegIndex(arm->operands[0].reg);

    if (arm->operands[1].type == ARM_OP_MEM) {
        const cs_arm_op& mem_op = arm->operands[1];
        // Check for PC-relative load (literal pool)
        if (mem_op.mem.base == ARM_REG_PC && decoder != nullptr && !arm->writeback) {
            uint32_t current_addr = insn->address;
            bool is_thumb = (current_addr & 1) != 0;
            uint32_t pc_val = ARMDecoder::ResolvePCRelative(
                current_addr, mem_op.mem.disp, is_thumb);
            uint32_t hardcoded_value = decoder->Read32FromROM(pc_val);
            out << "    ctx->r[" << dest << "] = 0x" << std::hex << std::uppercase
                << std::setfill('0') << std::setw(8) << hardcoded_value << ";\n";
        } else {
            int base_reg = RegIndex(mem_op.mem.base);
            std::string base_expr = "ctx->r[" + std::to_string(base_reg) + "]";
            std::string mem_offset = BuildMemOffsetExpr(mem_op);
            std::string addr_expr = arm->post_index
                ? base_expr
                : AddOffsetExpr(base_expr, mem_offset);

            out << "    ctx->r[" << dest << "] = ctx->mem->Read32(" << addr_expr << ");\n";

            if (arm->writeback || arm->post_index) {
                std::string wb_offset = arm->post_index
                    ? BuildPostIndexOffsetExpr(arm, 1)
                    : mem_offset;
                if (wb_offset != "0") {
                    out << "    ctx->r[" << base_reg << "] = "
                        << AddOffsetExpr(base_expr, wb_offset) << ";\n";
                }
            }
        }
    }
}


void CEmitter::EmitLDRH(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = RegIndex(arm->operands[0].reg);
    if (arm->operands[1].type == ARM_OP_MEM) {
        const cs_arm_op& mem_op = arm->operands[1];
        int base_reg = RegIndex(mem_op.mem.base);
        std::string base_expr = "ctx->r[" + std::to_string(base_reg) + "]";
        std::string mem_offset = BuildMemOffsetExpr(mem_op);
        std::string addr_expr = arm->post_index
            ? base_expr
            : AddOffsetExpr(base_expr, mem_offset);

        out << "    ctx->r[" << dest << "] = ctx->mem->Read16(" << addr_expr << ");\n";

        if (arm->writeback || arm->post_index) {
            std::string wb_offset = arm->post_index
                ? BuildPostIndexOffsetExpr(arm, 1)
                : mem_offset;
            if (wb_offset != "0") {
                out << "    ctx->r[" << base_reg << "] = "
                    << AddOffsetExpr(base_expr, wb_offset) << ";\n";
            }
        }
    }
}

void CEmitter::EmitSTRH(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int src = RegIndex(arm->operands[0].reg);
    if (arm->operands[1].type == ARM_OP_MEM) {
        const cs_arm_op& mem_op = arm->operands[1];
        int base_reg = RegIndex(mem_op.mem.base);
        std::string base_expr = "ctx->r[" + std::to_string(base_reg) + "]";
        std::string mem_offset = BuildMemOffsetExpr(mem_op);
        std::string addr_expr = arm->post_index
            ? base_expr
            : AddOffsetExpr(base_expr, mem_offset);

        out << "    ctx->mem->Write16(" << addr_expr << ", ctx->r[" << src << "] & 0xFFFF);\n";

        if (arm->writeback || arm->post_index) {
            std::string wb_offset = arm->post_index
                ? BuildPostIndexOffsetExpr(arm, 1)
                : mem_offset;
            if (wb_offset != "0") {
                out << "    ctx->r[" << base_reg << "] = "
                    << AddOffsetExpr(base_expr, wb_offset) << ";\n";
            }
        }
    }
}

void CEmitter::EmitLDRB(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = RegIndex(arm->operands[0].reg);
    if (arm->operands[1].type == ARM_OP_MEM) {
        const cs_arm_op& mem_op = arm->operands[1];
        int base_reg = RegIndex(mem_op.mem.base);
        std::string base_expr = "ctx->r[" + std::to_string(base_reg) + "]";
        std::string mem_offset = BuildMemOffsetExpr(mem_op);
        std::string addr_expr = arm->post_index
            ? base_expr
            : AddOffsetExpr(base_expr, mem_offset);

        out << "    ctx->r[" << dest << "] = ctx->mem->Read8(" << addr_expr << ");\n";

        if (arm->writeback || arm->post_index) {
            std::string wb_offset = arm->post_index
                ? BuildPostIndexOffsetExpr(arm, 1)
                : mem_offset;
            if (wb_offset != "0") {
                out << "    ctx->r[" << base_reg << "] = "
                    << AddOffsetExpr(base_expr, wb_offset) << ";\n";
            }
        }
    }
}

void CEmitter::EmitSTRB(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int src = RegIndex(arm->operands[0].reg);
    if (arm->operands[1].type == ARM_OP_MEM) {
        const cs_arm_op& mem_op = arm->operands[1];
        int base_reg = RegIndex(mem_op.mem.base);
        std::string base_expr = "ctx->r[" + std::to_string(base_reg) + "]";
        std::string mem_offset = BuildMemOffsetExpr(mem_op);
        std::string addr_expr = arm->post_index
            ? base_expr
            : AddOffsetExpr(base_expr, mem_offset);

        out << "    ctx->mem->Write8(" << addr_expr << ", ctx->r[" << src << "] & 0xFF);\n";

        if (arm->writeback || arm->post_index) {
            std::string wb_offset = arm->post_index
                ? BuildPostIndexOffsetExpr(arm, 1)
                : mem_offset;
            if (wb_offset != "0") {
                out << "    ctx->r[" << base_reg << "] = "
                    << AddOffsetExpr(base_expr, wb_offset) << ";\n";
            }
        }
    }
}

void CEmitter::EmitLDRSH(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = RegIndex(arm->operands[0].reg);
    if (arm->operands[1].type == ARM_OP_MEM) {
        const cs_arm_op& mem_op = arm->operands[1];
        int base_reg = RegIndex(mem_op.mem.base);
        std::string base_expr = "ctx->r[" + std::to_string(base_reg) + "]";
        std::string mem_offset = BuildMemOffsetExpr(mem_op);
        std::string addr_expr = arm->post_index
            ? base_expr
            : AddOffsetExpr(base_expr, mem_offset);

        out << "    ctx->r[" << dest << "] = (uint32_t)(int32_t)(int16_t)ctx->mem->Read16(" << addr_expr << ");\n";

        if (arm->writeback || arm->post_index) {
            std::string wb_offset = arm->post_index
                ? BuildPostIndexOffsetExpr(arm, 1)
                : mem_offset;
            if (wb_offset != "0") {
                out << "    ctx->r[" << base_reg << "] = "
                    << AddOffsetExpr(base_expr, wb_offset) << ";\n";
            }
        }
    }
}

void CEmitter::EmitLDRSB(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = RegIndex(arm->operands[0].reg);
    if (arm->operands[1].type == ARM_OP_MEM) {
        const cs_arm_op& mem_op = arm->operands[1];
        int base_reg = RegIndex(mem_op.mem.base);
        std::string base_expr = "ctx->r[" + std::to_string(base_reg) + "]";
        std::string mem_offset = BuildMemOffsetExpr(mem_op);
        std::string addr_expr = arm->post_index
            ? base_expr
            : AddOffsetExpr(base_expr, mem_offset);

        out << "    ctx->r[" << dest << "] = (uint32_t)(int32_t)(int8_t)ctx->mem->Read8(" << addr_expr << ");\n";

        if (arm->writeback || arm->post_index) {
            std::string wb_offset = arm->post_index
                ? BuildPostIndexOffsetExpr(arm, 1)
                : mem_offset;
            if (wb_offset != "0") {
                out << "    ctx->r[" << base_reg << "] = "
                    << AddOffsetExpr(base_expr, wb_offset) << ";\n";
            }
        }
    }
}

void CEmitter::EmitSTR(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int src = RegIndex(arm->operands[0].reg);

    if (arm->operands[1].type == ARM_OP_MEM) {
        const cs_arm_op& mem_op = arm->operands[1];
        int base_reg = RegIndex(mem_op.mem.base);
        std::string base_expr = "ctx->r[" + std::to_string(base_reg) + "]";
        std::string mem_offset = BuildMemOffsetExpr(mem_op);
        std::string addr_expr = arm->post_index
            ? base_expr
            : AddOffsetExpr(base_expr, mem_offset);

        out << "    ctx->mem->Write32(" << addr_expr << ", ctx->r[" << src << "]);\n";

        if (arm->writeback || arm->post_index) {
            std::string wb_offset = arm->post_index
                ? BuildPostIndexOffsetExpr(arm, 1)
                : mem_offset;
            if (wb_offset != "0") {
                out << "    ctx->r[" << base_reg << "] = "
                    << AddOffsetExpr(base_expr, wb_offset) << ";\n";
            }
        }
    }
}

void CEmitter::EmitSWP(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    if (arm->op_count < 3 ||
        arm->operands[0].type != ARM_OP_REG ||
        arm->operands[1].type != ARM_OP_REG ||
        arm->operands[2].type != ARM_OP_MEM) {
        return;
    }

    const int dest = RegIndex(arm->operands[0].reg);
    const int src = RegIndex(arm->operands[1].reg);
    const cs_arm_op& mem_op = arm->operands[2];
    const int base_reg = RegIndex(mem_op.mem.base);
    if (dest < 0 || src < 0 || base_reg < 0) {
        return;
    }

    const std::string base_expr = "ctx->r[" + std::to_string(base_reg) + "]";
    const std::string mem_offset = BuildMemOffsetExpr(mem_op);
    const std::string addr_expr = AddOffsetExpr(base_expr, mem_offset);

    out << "    { uint32_t _tmp = ctx->mem->Read32(" << addr_expr << ");\n";
    out << "    ctx->mem->Write32(" << addr_expr << ", ctx->r[" << src << "]);\n";
    out << "    ctx->r[" << dest << "] = _tmp;\n";
    out << "    }\n";
}

void CEmitter::EmitSWPB(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    if (arm->op_count < 3 ||
        arm->operands[0].type != ARM_OP_REG ||
        arm->operands[1].type != ARM_OP_REG ||
        arm->operands[2].type != ARM_OP_MEM) {
        return;
    }

    const int dest = RegIndex(arm->operands[0].reg);
    const int src = RegIndex(arm->operands[1].reg);
    const cs_arm_op& mem_op = arm->operands[2];
    const int base_reg = RegIndex(mem_op.mem.base);
    if (dest < 0 || src < 0 || base_reg < 0) {
        return;
    }

    const std::string base_expr = "ctx->r[" + std::to_string(base_reg) + "]";
    const std::string mem_offset = BuildMemOffsetExpr(mem_op);
    const std::string addr_expr = AddOffsetExpr(base_expr, mem_offset);

    out << "    { uint32_t _tmp = ctx->mem->Read8(" << addr_expr << ");\n";
    out << "    ctx->mem->Write8(" << addr_expr << ", ctx->r[" << src << "] & 0xFFu);\n";
    out << "    ctx->r[" << dest << "] = _tmp;\n";
    out << "    }\n";
}

// ============================================================================
// Branch Emitters
// ============================================================================

void CEmitter::EmitB(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    if (arm->op_count > 0 && arm->operands[0].type == ARM_OP_IMM) {
        uint32_t target = static_cast<uint32_t>(arm->operands[0].imm);
        if (current_chunk_addresses.count(target)) {
            out << "    goto block_0x" << std::hex << std::uppercase << target << ";\n";
        } else {
            out << "    ctx->r[15] = 0x" << std::hex << target << "; return;\n";
        }
    }
}

void CEmitter::EmitBL(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    if (arm->op_count > 0 && arm->operands[0].type == ARM_OP_IMM) {
        out << "    ctx->r[14] = 0x" << std::hex << std::uppercase
            << (insn->address + insn->size) << "; // LR = return address\n";
        uint32_t target = static_cast<uint32_t>(arm->operands[0].imm);
        out << "    ctx->r[15] = 0x" << std::hex << target << "; return;\n";
    } else if (arm->op_count > 0 && arm->operands[0].type == ARM_OP_REG) {
        // BLX with register
        int reg = RegIndex(arm->operands[0].reg);
        out << "    ctx->r[14] = 0x" << std::hex << std::uppercase
            << (insn->address + insn->size) << "; // LR = return address\n";
        out << "    ctx->r[15] = ctx->r[" << std::dec << reg << "]; return;\n";
    }
}

void CEmitter::EmitBX(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    if (arm->op_count > 0 && arm->operands[0].type == ARM_OP_REG) {
        int reg = RegIndex(arm->operands[0].reg);
        // If it's a direct bx lr (return point), we can just return from the C++ function!
        // But for static recompilation, if it is an overlay branch, we might need to be careful.
        // Usually BX LR returns to our caller logic directly.
        if (reg == 14) {
            out << "    ctx->r[15] = ctx->r[14]; return;\n";
        } else {
            out << "    ctx->r[15] = ctx->r[" << reg << "]; return;\n";
        }
    }
}

// ============================================================================
// Stack Emitters (PUSH / POP are aliases for STMDB/LDMIA on SP)
// ============================================================================

void CEmitter::EmitPUSH(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    for (int i = 0; i < arm->op_count; ++i) {
        if (arm->operands[i].type == ARM_OP_REG) {
            int reg = RegIndex(arm->operands[i].reg);
            out << "    ctx->r[13] -= 4;\n";
            out << "    ctx->mem->Write32(ctx->r[13], ctx->r[" << reg << "]);\n";
        }
    }
}

void CEmitter::EmitPOP(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    bool pops_pc = false;
    for (int i = 0; i < arm->op_count; ++i) {
        if (arm->operands[i].type == ARM_OP_REG) {
            int reg = RegIndex(arm->operands[i].reg);
            out << "    ctx->r[" << reg << "] = ctx->mem->Read32(ctx->r[13]);\n";
            out << "    ctx->r[13] += 4;\n";
            if (reg == 15) {
                pops_pc = true;
            }
        }
    }
    if (pops_pc) {
        // POP {..., pc} transfers control via restored PC.
        out << "    return;\n";
    }
}

// ============================================================================
// Block Transfer Emitters (LDM / STM)
// ============================================================================

void CEmitter::EmitLDM(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    if (arm->op_count < 2) return;
    int base_reg = RegIndex(arm->operands[0].reg);
    bool loads_pc = false;

    int reg_count = 0;
    for (int i = 1; i < arm->op_count; ++i) {
        if (arm->operands[i].type == ARM_OP_REG) {
            ++reg_count;
        }
    }
    if (reg_count == 0) return;

    std::string start_expr;
    switch (insn->id) {
        case ARM_INS_LDMIB:
            start_expr = "ctx->r[" + std::to_string(base_reg) + "] + 4";
            break;
        case ARM_INS_LDMDA:
            start_expr = "ctx->r[" + std::to_string(base_reg) + "] - " + std::to_string(4 * (reg_count - 1));
            break;
        case ARM_INS_LDMDB:
            start_expr = "ctx->r[" + std::to_string(base_reg) + "] - " + std::to_string(4 * reg_count);
            break;
        case ARM_INS_LDM:
        default:
            start_expr = "ctx->r[" + std::to_string(base_reg) + "]";
            break;
    }

    out << "    { uint32_t _addr = " << start_expr << ";\n";
    for (int i = 1; i < arm->op_count; ++i) {
        if (arm->operands[i].type == ARM_OP_REG) {
            int reg = RegIndex(arm->operands[i].reg);
            out << "    ctx->r[" << reg << "] = ctx->mem->Read32(_addr); _addr += 4;\n";
            if (reg == 15) {
                loads_pc = true;
            }
        }
    }
    if (arm->writeback) {
        if (insn->id == ARM_INS_LDMDA || insn->id == ARM_INS_LDMDB) {
            out << "    ctx->r[" << base_reg << "] -= " << (4 * reg_count) << ";\n";
        } else {
            out << "    ctx->r[" << base_reg << "] += " << (4 * reg_count) << ";\n";
        }
    }
    out << "    }\n";
    if (loads_pc) {
        // LDM ... {.., pc} is a control-flow transfer; stop block emission here.
        out << "    return;\n";
    }
}

void CEmitter::EmitSTM(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    if (arm->op_count < 2) return;
    int base_reg = RegIndex(arm->operands[0].reg);

    int reg_count = 0;
    for (int i = 1; i < arm->op_count; ++i) {
        if (arm->operands[i].type == ARM_OP_REG) {
            ++reg_count;
        }
    }
    if (reg_count == 0) return;

    std::string start_expr;
    switch (insn->id) {
        case ARM_INS_STMIB:
            start_expr = "ctx->r[" + std::to_string(base_reg) + "] + 4";
            break;
        case ARM_INS_STMDA:
            start_expr = "ctx->r[" + std::to_string(base_reg) + "] - " + std::to_string(4 * (reg_count - 1));
            break;
        case ARM_INS_STMDB:
            start_expr = "ctx->r[" + std::to_string(base_reg) + "] - " + std::to_string(4 * reg_count);
            break;
        case ARM_INS_STM:
        default:
            start_expr = "ctx->r[" + std::to_string(base_reg) + "]";
            break;
    }

    out << "    { uint32_t _addr = " << start_expr << ";\n";
    for (int i = 1; i < arm->op_count; ++i) {
        if (arm->operands[i].type == ARM_OP_REG) {
            int reg = RegIndex(arm->operands[i].reg);
            out << "    ctx->mem->Write32(_addr, ctx->r[" << reg << "]); _addr += 4;\n";
        }
    }
    if (arm->writeback) {
        if (insn->id == ARM_INS_STMDA || insn->id == ARM_INS_STMDB) {
            out << "    ctx->r[" << base_reg << "] -= " << (4 * reg_count) << ";\n";
        } else {
            out << "    ctx->r[" << base_reg << "] += " << (4 * reg_count) << ";\n";
        }
    }
    out << "    }\n";
}

// ============================================================================
// System/Coprocessor Emitters
// ============================================================================
void CEmitter::EmitMCR(cs_insn* insn, std::stringstream& out) {
    // MCR p15, opc1, Rt, CRn, CRm, opc2
    // Decode the source register (Rt) and mirror key CP15 state updates used by NDS bootstrap code.
    cs_arm* arm = &(insn->detail->arm);
    int rt = -1;

    for (int i = 0; i < arm->op_count; ++i) {
        if (arm->operands[i].type == ARM_OP_REG) {
            rt = RegIndex(arm->operands[i].reg);
            if (rt >= 0) break;
        }
    }

    if (rt < 0) {
        out << "    // MCR decode failed: source register not found\n";
        return;
    }

    const std::string op = insn->op_str;
    if (op.find("p15") == std::string::npos) {
        return;
    }

    if (op.find("c1, c0") != std::string::npos || op.find("c1,c0") != std::string::npos) {
        out << "    ctx->cp15_control = ctx->r[" << rt << "];\n";
    } else if (op.find("c9, c1") != std::string::npos || op.find("c9,c1") != std::string::npos) {
        if (op.find("#1") != std::string::npos || op.find(", 1") != std::string::npos) {
            out << "    ctx->cp15_itcm_base = ctx->r[" << rt << "];\n";
        } else {
            out << "    ctx->cp15_dtcm_base = ctx->r[" << rt << "];\n";
        }
    }

    if (op.find("c7") != std::string::npos) {
        out << "    ctx->cp15.cache_cmd = ctx->r[" << rt << "];\n";
        if (op.find("c5") != std::string::npos || op.find("c14") != std::string::npos) {
            out << "    ctx->mem->InvalidateOverlayCache();\n";
        }
    }
}

void CEmitter::EmitMRC(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int rt = -1;

    for (int i = 0; i < arm->op_count; ++i) {
        if (arm->operands[i].type == ARM_OP_REG) {
            rt = RegIndex(arm->operands[i].reg);
            if (rt >= 0) break;
        }
    }

    if (rt < 0) {
        out << "    // MRC decode failed: destination register not found\n";
        return;
    }

    const std::string op = insn->op_str;
    if (op.find("c1, c0") != std::string::npos || op.find("c1,c0") != std::string::npos) {
        out << "    ctx->r[" << rt << "] = ctx->cp15_control;\n";
    } else if (op.find("c9, c1") != std::string::npos || op.find("c9,c1") != std::string::npos) {
        if (op.find("#1") != std::string::npos || op.find(", 1") != std::string::npos) {
            out << "    ctx->r[" << rt << "] = ctx->cp15_itcm_base;\n";
        } else {
            out << "    ctx->r[" << rt << "] = ctx->cp15_dtcm_base;\n";
        }
    } else {
        // Unknown CP15 register read path: return zero as a safe fallback.
        out << "    ctx->r[" << rt << "] = 0;\n";
    }
}

void CEmitter::EmitMSR(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    std::string src_expr;

    for (int i = 0; i < arm->op_count; ++i) {
        if (arm->operands[i].type == ARM_OP_REG) {
            int src = RegIndex(arm->operands[i].reg);
            if (src >= 0) {
                src_expr = "ctx->r[" + std::to_string(src) + "]";
                break;
            }
        } else if (arm->operands[i].type == ARM_OP_IMM) {
            std::stringstream imm;
            imm << "0x" << std::hex << std::uppercase << static_cast<uint32_t>(arm->operands[i].imm);
            src_expr = imm.str();
            break;
        }
    }

    if (src_expr.empty()) {
        out << "    // MSR decode failed: source operand not found\n";
        return;
    }

    const std::string op = insn->op_str;
    const bool cpsr_write = (op.find("cpsr") != std::string::npos);
    const bool control_only = (op.find("cpsr_c") != std::string::npos);

    if (cpsr_write && control_only) {
        out << "    ctx->cpsr = (ctx->cpsr & ~0xFFu) | (" << src_expr << " & 0xFFu);\n";
        out << "    ctx->SwitchMode(ctx->cpsr & CPSRFlags::MODE_MASK);\n";
    } else if (cpsr_write) {
        out << "    ctx->cpsr = " << src_expr << ";\n";
        out << "    ctx->SwitchMode(ctx->cpsr & CPSRFlags::MODE_MASK);\n";
    } else {
        // SPSR writes are mode-dependent.
        out << "    ctx->SetSPSR(" << src_expr << ");\n";
    }
}

void CEmitter::EmitMRS(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    int dest = -1;

    for (int i = 0; i < arm->op_count; ++i) {
        if (arm->operands[i].type == ARM_OP_REG) {
            dest = RegIndex(arm->operands[i].reg);
            if (dest >= 0) break;
        }
    }

    if (dest < 0) {
        out << "    // MRS decode failed: destination register not found\n";
        return;
    }

    const std::string op = insn->op_str;
    if (op.find("spsr") != std::string::npos) {
        out << "    ctx->r[" << dest << "] = ctx->GetSPSR();\n";
    } else {
        out << "    ctx->r[" << dest << "] = ctx->cpsr;\n";
    }
}

void CEmitter::EmitSVC(cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    uint32_t swi_number = 0;

    for (int i = 0; i < arm->op_count; ++i) {
        if (arm->operands[i].type == ARM_OP_IMM) {
            swi_number = static_cast<uint32_t>(arm->operands[i].imm);
            break;
        }
    }

    out << "    HwBios::HandleARM9_SWI(0x" << std::hex << std::uppercase
        << swi_number << ");\n";
}

// ============================================================================
// Primary Dispatch — Emits C++ for a single decoded instruction
// ============================================================================
std::string CEmitter::EmitInstruction(cs_insn* insn) {
    std::stringstream out;
    cs_arm* arm = &(insn->detail->arm);

    // Conditional wrapper
    std::string cond = EmitCondition(arm->cc);
    if (!cond.empty()) out << cond;

    switch (insn->id) {
        // Data Processing
        case ARM_INS_ADD: EmitADD(insn, out); break;
        case ARM_INS_ADC: EmitADC(insn, out); break;
        case ARM_INS_SUB: EmitSUB(insn, out); break;
        case ARM_INS_LSL: EmitLSL(insn, out); break;
        case ARM_INS_LSR: EmitLSR(insn, out); break;
        case ARM_INS_ASR: EmitASR(insn, out); break;
        case ARM_INS_ROR: EmitROR(insn, out); break;
        case ARM_INS_RSB: EmitRSB(insn, out); break;
        case ARM_INS_RSC: EmitRSC(insn, out); break;
        case ARM_INS_MOV: EmitMOV(insn, out); break;
        case ARM_INS_MVN: EmitMVN(insn, out); break;
        case ARM_INS_AND: EmitAND(insn, out); break;
        case ARM_INS_ORR: EmitORR(insn, out); break;
        case ARM_INS_EOR: EmitEOR(insn, out); break;
        case ARM_INS_BIC: EmitBIC(insn, out); break;
        case ARM_INS_CMP: EmitCMP(insn, out); break;
        case ARM_INS_CMN: EmitCMN(insn, out); break;
        case ARM_INS_TST: EmitTST(insn, out); break;
        case ARM_INS_TEQ: EmitTEQ(insn, out); break;
        case ARM_INS_SBC: EmitSBC(insn, out); break;

        // Multiply
        case ARM_INS_MUL: EmitMUL(insn, out); break;
        case ARM_INS_MLA: EmitMLA(insn, out); break;
        case ARM_INS_SMLAWB: EmitSMLAWB(insn, out); break;
        case ARM_INS_SMLAWT: EmitSMLAWT(insn, out); break;

        // Memory
        case ARM_INS_LDR: EmitLDR(insn, out); break;
        case ARM_INS_STR: EmitSTR(insn, out); break;
        case ARM_INS_LDRH: EmitLDRH(insn, out); break;
        case ARM_INS_STRH: EmitSTRH(insn, out); break;
        case ARM_INS_LDRB: EmitLDRB(insn, out); break;
        case ARM_INS_STRB: EmitSTRB(insn, out); break;
        case ARM_INS_LDRSH: EmitLDRSH(insn, out); break;
        case ARM_INS_LDRSB: EmitLDRSB(insn, out); break;
        case ARM_INS_SWP: EmitSWP(insn, out); break;
        case ARM_INS_SWPB: EmitSWPB(insn, out); break;

        // Branches
        case ARM_INS_B:   EmitB(insn, out);   break;
        case ARM_INS_BL:  EmitBL(insn, out);  break;
        case ARM_INS_BX:  EmitBX(insn, out);  break;
        case ARM_INS_BLX: EmitBL(insn, out);  break; // BLX treated like BL

        // Stack
        case ARM_INS_PUSH: EmitPUSH(insn, out); break;
        case ARM_INS_POP:  EmitPOP(insn, out);  break;

        // Block transfers
        case ARM_INS_LDM:   EmitLDM(insn, out); break;
        case ARM_INS_LDMDA: EmitLDM(insn, out); break;
        case ARM_INS_LDMDB: EmitLDM(insn, out); break;
        case ARM_INS_LDMIB: EmitLDM(insn, out); break;
        case ARM_INS_STM:   EmitSTM(insn, out); break;
        case ARM_INS_STMDA: EmitSTM(insn, out); break;
        case ARM_INS_STMDB: EmitSTM(insn, out); break;
        case ARM_INS_STMIB: EmitSTM(insn, out); break;

        // Coprocessor
        case ARM_INS_MCR:   EmitMCR(insn, out); break;
        case ARM_INS_MRC:   EmitMRC(insn, out); break;
        case ARM_INS_MSR:   EmitMSR(insn, out); break;
        case ARM_INS_MRS:   EmitMRS(insn, out); break;
        case ARM_INS_SVC:   EmitSVC(insn, out); break;

        default:
        {
            const std::string mnemonic = insn->mnemonic ? insn->mnemonic : "";
            if (mnemonic == "uxtb") {
                if (arm->op_count >= 2 &&
                    arm->operands[0].type == ARM_OP_REG &&
                    arm->operands[1].type == ARM_OP_REG) {
                    const int dest = RegIndex(arm->operands[0].reg);
                    const int src = RegIndex(arm->operands[1].reg);
                    if (dest >= 0 && src >= 0) {
                        out << "    ctx->r[" << dest << "] = ctx->r[" << src << "] & 0xFFu;\n";
                        break;
                    }
                }
            }
            if (mnemonic == "uxth") {
                if (arm->op_count >= 2 &&
                    arm->operands[0].type == ARM_OP_REG &&
                    arm->operands[1].type == ARM_OP_REG) {
                    const int dest = RegIndex(arm->operands[0].reg);
                    const int src = RegIndex(arm->operands[1].reg);
                    if (dest >= 0 && src >= 0) {
                        out << "    ctx->r[" << dest << "] = ctx->r[" << src << "] & 0xFFFFu;\n";
                        break;
                    }
                }
            }
            if (mnemonic == "rev") {
                if (arm->op_count >= 2 &&
                    arm->operands[0].type == ARM_OP_REG &&
                    arm->operands[1].type == ARM_OP_REG) {
                    const int dest = RegIndex(arm->operands[0].reg);
                    const int src = RegIndex(arm->operands[1].reg);
                    if (dest >= 0 && src >= 0) {
                        out << "    { uint32_t _x = ctx->r[" << src << "];\n";
                        out << "    ctx->r[" << dest << "] = ((_x & 0x000000FFu) << 24) | ((_x & 0x0000FF00u) << 8) | ((_x & 0x00FF0000u) >> 8) | ((_x & 0xFF000000u) >> 24);\n";
                        out << "    }\n";
                        break;
                    }
                }
            }
            if (mnemonic == "rev16") {
                if (arm->op_count >= 2 &&
                    arm->operands[0].type == ARM_OP_REG &&
                    arm->operands[1].type == ARM_OP_REG) {
                    const int dest = RegIndex(arm->operands[0].reg);
                    const int src = RegIndex(arm->operands[1].reg);
                    if (dest >= 0 && src >= 0) {
                        out << "    { uint32_t _x = ctx->r[" << src << "];\n";
                        out << "    ctx->r[" << dest << "] = ((_x & 0x00FF00FFu) << 8) | ((_x & 0xFF00FF00u) >> 8);\n";
                        out << "    }\n";
                        break;
                    }
                }
            }
            if (mnemonic == "revsh") {
                if (arm->op_count >= 2 &&
                    arm->operands[0].type == ARM_OP_REG &&
                    arm->operands[1].type == ARM_OP_REG) {
                    const int dest = RegIndex(arm->operands[0].reg);
                    const int src = RegIndex(arm->operands[1].reg);
                    if (dest >= 0 && src >= 0) {
                        out << "    { uint16_t _h = static_cast<uint16_t>(ctx->r[" << src << "] & 0xFFFFu);\n";
                        out << "    uint16_t _rh = static_cast<uint16_t>((_h << 8) | (_h >> 8));\n";
                        out << "    ctx->r[" << dest << "] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(_rh)));\n";
                        out << "    }\n";
                        break;
                    }
                }
            }
            if (mnemonic.rfind("mls", 0) == 0) {
                if (arm->op_count >= 4 &&
                    arm->operands[0].type == ARM_OP_REG &&
                    arm->operands[1].type == ARM_OP_REG &&
                    arm->operands[2].type == ARM_OP_REG &&
                    arm->operands[3].type == ARM_OP_REG) {
                    const int dest = RegIndex(arm->operands[0].reg);
                    const int rm = RegIndex(arm->operands[1].reg);
                    const int rs = RegIndex(arm->operands[2].reg);
                    const int rn = RegIndex(arm->operands[3].reg);
                    if (dest >= 0 && rm >= 0 && rs >= 0 && rn >= 0) {
                        out << "    ctx->r[" << dest << "] = ctx->r[" << rn
                            << "] - (ctx->r[" << rm << "] * ctx->r[" << rs << "]);\n";
                        break;
                    }
                }
            }
            if (mnemonic.rfind("ldrd", 0) == 0) {
                if (arm->op_count >= 3 &&
                    arm->operands[0].type == ARM_OP_REG &&
                    arm->operands[1].type == ARM_OP_REG &&
                    arm->operands[2].type == ARM_OP_MEM) {
                    const int dest_lo = RegIndex(arm->operands[0].reg);
                    const int dest_hi = RegIndex(arm->operands[1].reg);
                    const cs_arm_op& mem_op = arm->operands[2];
                    const int base_reg = RegIndex(mem_op.mem.base);
                    if (dest_lo >= 0 && dest_hi >= 0 && base_reg >= 0) {
                        const std::string base_expr = "ctx->r[" + std::to_string(base_reg) + "]";
                        const std::string mem_offset = BuildMemOffsetExpr(mem_op);
                        const std::string addr_expr = arm->post_index
                            ? base_expr
                            : AddOffsetExpr(base_expr, mem_offset);

                        out << "    ctx->r[" << dest_lo << "] = ctx->mem->Read32(" << addr_expr << ");\n";
                        out << "    ctx->r[" << dest_hi << "] = ctx->mem->Read32((" << addr_expr << ") + 4);\n";

                        if (arm->writeback || arm->post_index) {
                            const std::string wb_offset = arm->post_index
                                ? BuildPostIndexOffsetExpr(arm, 2)
                                : mem_offset;
                            if (wb_offset != "0") {
                                out << "    ctx->r[" << base_reg << "] = "
                                    << AddOffsetExpr(base_expr, wb_offset) << ";\n";
                            }
                        }
                        break;
                    }
                }
            }
            if (mnemonic.rfind("hvc", 0) == 0) {
                out << "    // Ignored HVC (virtualization call) on this runtime path.\n";
                break;
            }
            if (mnemonic.rfind("cdp", 0) == 0) {
                out << "    // Ignored coprocessor data operation (unsupported on this runtime path).\n";
                break;
            }
            if (mnemonic.rfind("stc", 0) == 0 || mnemonic.rfind("ldc", 0) == 0) {
                out << "    // Ignored coprocessor load/store operation (unsupported on this runtime path).\n";
                break;
            }
            if (mnemonic.rfind("strt", 0) == 0) {
                EmitSTR(insn, out);
                break;
            }
            if (mnemonic.rfind("ldrt", 0) == 0) {
                EmitLDR(insn, out);
                break;
            }
            if (mnemonic.rfind("ldrbt", 0) == 0) {
                EmitLDRB(insn, out);
                break;
            }
            if (mnemonic.rfind("ldrsbt", 0) == 0) {
                EmitLDRSB(insn, out);
                break;
            }
            if (mnemonic.rfind("ldrsht", 0) == 0) {
                EmitLDRSH(insn, out);
                break;
            }
            if (mnemonic.rfind("movt", 0) == 0) {
                if (arm->op_count >= 2 &&
                    arm->operands[0].type == ARM_OP_REG &&
                    arm->operands[1].type == ARM_OP_IMM) {
                    const int dest = RegIndex(arm->operands[0].reg);
                    const uint32_t imm16 = static_cast<uint32_t>(arm->operands[1].imm) & 0xFFFFu;
                    if (dest == 15) {
                        out << "    // Ignored movt to pc on unsupported path.\n";
                    } else if (dest >= 0) {
                        out << "    ctx->r[" << dest << "] = (ctx->r[" << dest
                            << "] & 0x0000FFFFu) | 0x" << std::hex << std::uppercase
                            << (imm16 << 16) << ";\n";
                    }
                    break;
                }
            }
            if (mnemonic == "bxns") {
                EmitBX(insn, out);
                break;
            }
            if (mnemonic == "cbz" || mnemonic == "cbnz") {
                if (arm->op_count >= 2 &&
                    arm->operands[0].type == ARM_OP_REG &&
                    arm->operands[1].type == ARM_OP_IMM) {
                    const int src = RegIndex(arm->operands[0].reg);
                    const uint32_t target = static_cast<uint32_t>(arm->operands[1].imm);
                    if (src >= 0) {
                        out << "    if (ctx->r[" << src << "] "
                            << (mnemonic == "cbz" ? "==" : "!=")
                            << " 0) {\n";
                        if (current_chunk_addresses.count(target)) {
                            out << "    goto block_0x" << std::hex << std::uppercase
                                << target << ";\n";
                        } else {
                            out << "    ctx->r[15] = 0x" << std::hex << std::uppercase
                                << target << "; return;\n";
                        }
                        out << "    }\n";
                        break;
                    }
                }
            }
            if (mnemonic == "adr") {
                if (arm->op_count >= 2 &&
                    arm->operands[0].type == ARM_OP_REG &&
                    arm->operands[1].type == ARM_OP_IMM) {
                    const int dest = RegIndex(arm->operands[0].reg);
                    if (dest >= 0) {
                        const bool is_thumb = IsThumbDecodedInstruction(decoder, insn);
                        const uint32_t pc_base = is_thumb
                            ? ((static_cast<uint32_t>(insn->address) + 4u) & ~3u)
                            : (static_cast<uint32_t>(insn->address) + 8u);
                        const int64_t imm = arm->operands[1].imm;
                        const uint32_t target = static_cast<uint32_t>(
                            static_cast<int64_t>(pc_base) + imm);
                        out << "    ctx->r[" << dest << "] = 0x" << std::hex
                            << std::uppercase << target << ";\n";
                        break;
                    }
                }
            }
            if (mnemonic == "ssat") {
                if (arm->op_count >= 3 &&
                    arm->operands[0].type == ARM_OP_REG &&
                    arm->operands[1].type == ARM_OP_IMM) {
                    const int dest = RegIndex(arm->operands[0].reg);
                    const int sat_bits = static_cast<int>(arm->operands[1].imm);
                    if (dest >= 0 && sat_bits > 0 && sat_bits <= 32) {
                        const std::string src_expr = ProcessOperand(arm, 2);
                        if (sat_bits == 32) {
                            out << "    ctx->r[" << dest << "] = (uint32_t)(int32_t)(" << src_expr << ");\n";
                        } else {
                            const int64_t max_sat = (1ll << (sat_bits - 1)) - 1ll;
                            const int64_t min_sat = -(1ll << (sat_bits - 1));
                            out << "    { int64_t _v = (int64_t)(int32_t)(" << src_expr << ");\n";
                            out << "      if (_v > " << max_sat << "LL) _v = " << max_sat << "LL;\n";
                            out << "      else if (_v < " << min_sat << "LL) _v = " << min_sat << "LL;\n";
                            out << "      ctx->r[" << dest << "] = (uint32_t)(int32_t)_v;\n";
                            out << "    }\n";
                        }
                        break;
                    }
                }
            }
            if (mnemonic == "sxth") {
                if (arm->op_count >= 2 &&
                    arm->operands[0].type == ARM_OP_REG) {
                    const int dest = RegIndex(arm->operands[0].reg);
                    if (dest >= 0) {
                        const std::string src_expr = ProcessOperand(arm, 1);
                        out << "    ctx->r[" << dest << "] = (uint32_t)(int32_t)(int16_t)(" << src_expr << ");\n";
                        break;
                    }
                }
            }
            if (mnemonic.rfind("vld", 0) == 0) {
                int mem_index = -1;
                for (int i = 0; i < arm->op_count; ++i) {
                    if (arm->operands[i].type == ARM_OP_MEM) {
                        mem_index = i;
                        break;
                    }
                }

                if (mem_index >= 0) {
                    const cs_arm_op& mem_op = arm->operands[mem_index];
                    const int base_reg = RegIndex(mem_op.mem.base);
                    if (base_reg >= 0) {
                        const std::string base_expr = "ctx->r[" + std::to_string(base_reg) + "]";
                        std::string wb_offset = arm->post_index
                            ? BuildPostIndexOffsetExpr(arm, mem_index)
                            : BuildMemOffsetExpr(mem_op);

                        if (wb_offset == "0" && arm->op_count > mem_index + 1) {
                            const cs_arm_op& wb_operand = arm->operands[mem_index + 1];
                            if (wb_operand.type == ARM_OP_REG || wb_operand.type == ARM_OP_IMM) {
                                wb_offset = OperandToExpression(wb_operand);
                                if (wb_operand.subtracted) {
                                    wb_offset = "-(" + wb_offset + ")";
                                }
                            }
                        }

                        if (wb_offset != "0") {
                            out << "    ctx->r[" << base_reg << "] = "
                                << AddOffsetExpr(base_expr, wb_offset) << ";\n";
                        }
                    }
                }

                out << "    // Ignored SIMD load (vector register file not modeled on this runtime path).\n";
                break;
            }
            if (!mnemonic.empty() && mnemonic[0] == 'v') {
                out << "    // Ignored SIMD/VFP op (vector register file not modeled on this runtime path).\n";
                break;
            }
            if (mnemonic == "movs") {
                EmitMOV(insn, out);
                break;
            }
            if (mnemonic.rfind("strd", 0) == 0) {
                if (arm->op_count >= 3 &&
                    arm->operands[0].type == ARM_OP_REG &&
                    arm->operands[1].type == ARM_OP_REG &&
                    arm->operands[2].type == ARM_OP_MEM) {
                    const int src_lo = RegIndex(arm->operands[0].reg);
                    const int src_hi = RegIndex(arm->operands[1].reg);
                    const cs_arm_op& mem_op = arm->operands[2];
                    const int base_reg = RegIndex(mem_op.mem.base);
                    const std::string base_expr = "ctx->r[" + std::to_string(base_reg) + "]";
                    const std::string mem_offset = BuildMemOffsetExpr(mem_op);
                    const std::string addr_expr = arm->post_index
                        ? base_expr
                        : AddOffsetExpr(base_expr, mem_offset);

                    out << "    ctx->mem->Write32(" << addr_expr << ", ctx->r[" << src_lo << "]);\n";
                    out << "    ctx->mem->Write32(" << AddOffsetExpr(addr_expr, "4")
                        << ", ctx->r[" << src_hi << "]);\n";

                    if (arm->writeback || arm->post_index) {
                        const std::string wb_offset = arm->post_index
                            ? BuildPostIndexOffsetExpr(arm, 2)
                            : mem_offset;
                        if (wb_offset != "0") {
                            out << "    ctx->r[" << base_reg << "] = "
                                << AddOffsetExpr(base_expr, wb_offset) << ";\n";
                        }
                    }
                    break;
                }
            }
            if (mnemonic.rfind("mcrr", 0) == 0 || mnemonic.rfind("mrrc", 0) == 0) {
                out << "    // Ignored coprocessor register transfer (unsupported on this runtime path).\n";
                break;
            }
            out << "    // UNIMPLEMENTED: " << insn->mnemonic << " " << insn->op_str << "\n";
            out << "    throw std::runtime_error(\"Unimplemented Instruction\");\n";
            break;
        }
    }

    // Close conditional wrapper
    if (!cond.empty()) out << "    }\n";

    return out.str();
}

void CEmitter::EmitARMv4T_Thumb(const cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    if (arm->op_count < 1) return;
    int target_reg = RegIndex(arm->operands[0].reg);

    // BX / BLX: dynamic dispatch logic handles clearing the thumb bit
    if (insn->id == ARM_INS_BLX) {
        out << "    ctx->r[14] = " << (insn->address + 4) << ";\n";
    }

    out << "    Core::Dispatcher::ExecuteDynamicBranch(ctx->r[" << target_reg << "], ctx);\n";
    out << "    return;\n";
}

void CEmitter::EmitCoproc_CP15(const cs_insn* insn, std::stringstream& out) {
    cs_arm* arm = &(insn->detail->arm);
    // MCR / MRC
    if (insn->id == ARM_INS_MCR) {
        EmitMCR(const_cast<cs_insn*>(insn), out);
    } else if (insn->id == ARM_INS_MRC) {
        // MRC: Coprocessor to ARM register
        if (arm->op_count > 0 && arm->operands[0].type == ARM_OP_REG) {
            int rt = RegIndex(arm->operands[0].reg);
            // Default mock: read 0 from CP15
            out << "    ctx->r[" << rt << "] = 0;\n";
        }
    }
}

void CEmitter::EmitBlockDataTransfer(const cs_insn* insn, std::stringstream& out) {
    if (insn->id == ARM_INS_LDM || insn->id == ARM_INS_LDMIB || insn->id == ARM_INS_LDMDA || insn->id == ARM_INS_LDMDB) {
        EmitLDM(const_cast<cs_insn*>(insn), out);
    } else if (insn->id == ARM_INS_STM || insn->id == ARM_INS_STMIB || insn->id == ARM_INS_STMDA || insn->id == ARM_INS_STMDB) {
        EmitSTM(const_cast<cs_insn*>(insn), out);
    } else if (insn->id == ARM_INS_PUSH) {
        EmitPUSH(const_cast<cs_insn*>(insn), out);
    } else if (insn->id == ARM_INS_POP) {
        EmitPOP(const_cast<cs_insn*>(insn), out);
    }
}

void CEmitter::HandleSBitFlags(const cs_insn* insn, std::stringstream& out) {
    // S-bit suffix instructions update CPSR flags (N, Z, C, V)
    cs_arm* arm = &(insn->detail->arm);
    if (!arm->update_flags) return;

    // Based on the instruction type, we update the N and Z flags at minimum
    // C and V are complex and depend on the instruction.
    int dest_reg = -1;
    if (arm->op_count > 0 && arm->operands[0].type == ARM_OP_REG) {
        dest_reg = RegIndex(arm->operands[0].reg);
    }

    if (dest_reg != -1) {
        out << "    ctx->cpsr = (ctx->cpsr & ~(0xF0000000)) |\n"
            << "                ((ctx->r[" << dest_reg << "] & 0x80000000) ? 0x80000000 : 0) |\n" // N flag
            << "                ((ctx->r[" << dest_reg << "] == 0) ? 0x40000000 : 0);\n";         // Z flag
    }
}
