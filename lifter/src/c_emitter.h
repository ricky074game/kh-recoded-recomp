#pragma once

// ============================================================================
// c_emitter.h — C++ Code Emitter for Lifted ARM Instructions
//
// Converts decoded Capstone ARM instructions into equivalent C++ statements
// that operate on a CPU_Context struct. Each ARM instruction type has a
// dedicated emitter method. The emitter also handles:
//   - Conditional execution (via CheckConditionCode)
//   - Barrel shifter operands (LSL, LSR, ASR, ROR)
//   - PC-relative loads (resolved statically from literal pools)
//   - Flag updates for 'S' suffix instructions
//   - DS_ADDR() debug macros for crash-to-ROM-address mapping
// ============================================================================

#include <string>
#include <sstream>
#include <set>
#include <utility>
#include <capstone/capstone.h>

class ARMDecoder;

class CEmitter {
private:
    ARMDecoder* decoder;
    std::set<uint32_t> current_chunk_addresses;

    // ---- Operand Processing ----
    std::string ProcessOperand(cs_arm* arm, int op_index);
    std::string EmitCondition(arm_cc cc);
    std::string EmitShift(cs_arm* arm, int op_index, const std::string& base_val);

    // ---- Data Processing Emitters ----
    void EmitADD(cs_insn* insn, std::stringstream& out);
    void EmitADC(cs_insn* insn, std::stringstream& out);
    void EmitSUB(cs_insn* insn, std::stringstream& out);
    void EmitLSL(cs_insn* insn, std::stringstream& out);
    void EmitLSR(cs_insn* insn, std::stringstream& out);
    void EmitASR(cs_insn* insn, std::stringstream& out);
    void EmitROR(cs_insn* insn, std::stringstream& out);
    void EmitMOV(cs_insn* insn, std::stringstream& out);
    void EmitMVN(cs_insn* insn, std::stringstream& out);
    void EmitCMP(cs_insn* insn, std::stringstream& out);
    void EmitCMN(cs_insn* insn, std::stringstream& out);
    void EmitAND(cs_insn* insn, std::stringstream& out);
    void EmitORR(cs_insn* insn, std::stringstream& out);
    void EmitEOR(cs_insn* insn, std::stringstream& out);
    void EmitBIC(cs_insn* insn, std::stringstream& out);
    void EmitRSB(cs_insn* insn, std::stringstream& out);
    void EmitRSC(cs_insn* insn, std::stringstream& out);
    void EmitSBC(cs_insn* insn, std::stringstream& out);
    void EmitTST(cs_insn* insn, std::stringstream& out);
    void EmitTEQ(cs_insn* insn, std::stringstream& out);

    // ---- Multiply Emitters ----
    void EmitMUL(cs_insn* insn, std::stringstream& out);
    void EmitMLA(cs_insn* insn, std::stringstream& out);
    void EmitSMLAWB(cs_insn* insn, std::stringstream& out);
    void EmitSMLAWT(cs_insn* insn, std::stringstream& out);

    // ---- Memory Access Emitters ----
    void EmitLDR(cs_insn* insn, std::stringstream& out);
    void EmitSTR(cs_insn* insn, std::stringstream& out);
    void EmitLDRH(cs_insn* insn, std::stringstream& out);
    void EmitSTRH(cs_insn* insn, std::stringstream& out);
    void EmitLDRB(cs_insn* insn, std::stringstream& out);
    void EmitSTRB(cs_insn* insn, std::stringstream& out);
    void EmitLDRSH(cs_insn* insn, std::stringstream& out);
    void EmitLDRSB(cs_insn* insn, std::stringstream& out);
    void EmitSWP(cs_insn* insn, std::stringstream& out);
    void EmitSWPB(cs_insn* insn, std::stringstream& out);

    // ---- Branch Emitters ----
    void EmitB(cs_insn* insn, std::stringstream& out);
    void EmitBL(cs_insn* insn, std::stringstream& out);
    void EmitBX(cs_insn* insn, std::stringstream& out);

    // ---- Stack Emitters ----
    void EmitPUSH(cs_insn* insn, std::stringstream& out);
    void EmitPOP(cs_insn* insn, std::stringstream& out);

    // ---- Block Transfer Emitters ----
    void EmitLDM(cs_insn* insn, std::stringstream& out);
    void EmitSTM(cs_insn* insn, std::stringstream& out);

    // ---- System/Coprocessor Emitters ----
    void EmitMCR(cs_insn* insn, std::stringstream& out);
    void EmitMRC(cs_insn* insn, std::stringstream& out);
    void EmitMSR(cs_insn* insn, std::stringstream& out);
    void EmitMRS(cs_insn* insn, std::stringstream& out);
    void EmitSVC(cs_insn* insn, std::stringstream& out);

public:
    explicit CEmitter(ARMDecoder* dec = nullptr) : decoder(dec) {}

    void SetChunkRange(std::set<uint32_t> addresses) {
        current_chunk_addresses = std::move(addresses);
    }

    // ---- Primary Interface ----
    std::string EmitInstruction(cs_insn* insn);

    // Emits a DS_ADDR() debug macro for a given ROM address.
    static std::string EmitDSAddr(uint32_t rom_address);

    // ---- Legacy Test Helpers ----
    std::string GenerateAdd(int rd, int rn, int rm) {
        return "ctx->r[" + std::to_string(rd) + "] = ctx->r[" +
               std::to_string(rn) + "] + ctx->r[" + std::to_string(rm) + "];";
    }

    std::string emit(int r) {
        return "ctx->r[" + std::to_string(r) + "] = 0;";
    }

    // --- Phase 1: Replaced Stubs ---
    void EmitARMv4T_Thumb(const cs_insn* insn, std::stringstream& out);
    void EmitCoproc_CP15(const cs_insn* insn, std::stringstream& out);
    void EmitBlockDataTransfer(const cs_insn* insn, std::stringstream& out);
    void HandleSBitFlags(const cs_insn* insn, std::stringstream& out);
};
