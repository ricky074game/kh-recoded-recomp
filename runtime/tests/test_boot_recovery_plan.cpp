#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "arm_decoder.h"
#include "c_emitter.h"
#include "hw_save.h"

namespace {

struct PlanCheckbox {
    bool checked = false;
    std::string text;
};

struct ShortcutRow {
    std::string address;
    std::string file;
    std::string classification;
    std::string reason;
};

struct FindingRow {
    std::string address;
    std::string category;
    std::string status;
    std::string notes;
};

struct CheckboxCase {
    std::string id;
    std::string checkbox_text;
    std::function<void()> verify;
};

std::string Trim(std::string value) {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }

    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) {
        ++start;
    }
    return value.substr(start);
}

std::vector<std::string> SplitTab(const std::string& line) {
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string part;
    while (std::getline(ss, part, '\t')) {
        parts.push_back(Trim(part));
    }
    return parts;
}

std::string Join(const std::vector<std::string>& values, std::string_view separator) {
    std::ostringstream oss;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            oss << separator;
        }
        oss << values[i];
    }
    return oss.str();
}

std::filesystem::path RepoRoot() {
#ifdef KH_REPO_ROOT
    return std::filesystem::path(KH_REPO_ROOT);
#else
    return std::filesystem::current_path();
#endif
}

std::filesystem::path RepoPath(std::string_view relative_path) {
    return RepoRoot() / std::filesystem::path(relative_path);
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        ADD_FAILURE() << "Failed to open " << path.string();
        return {};
    }

    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

std::string ReadRepoText(std::string_view relative_path) {
    return ReadTextFile(RepoPath(relative_path));
}

bool RepoFileExists(std::string_view relative_path) {
    return std::filesystem::exists(RepoPath(relative_path));
}

void ExpectRepoFileExists(std::string_view relative_path) {
    const auto path = RepoPath(relative_path);
    ASSERT_TRUE(std::filesystem::exists(path)) << "Expected file to exist: " << path.string();
}

void ExpectFileContains(std::string_view relative_path, std::string_view needle) {
    const auto path = RepoPath(relative_path);
    const std::string text = ReadTextFile(path);
    ASSERT_NE(text.find(std::string(needle)), std::string::npos)
        << "Expected " << path.string() << " to contain: " << needle;
}

void ExpectFileDoesNotContain(std::string_view relative_path, std::string_view needle) {
    const auto path = RepoPath(relative_path);
    const std::string text = ReadTextFile(path);
    ASSERT_EQ(text.find(std::string(needle)), std::string::npos)
        << "Did not expect " << path.string() << " to contain: " << needle;
}

size_t CountOccurrences(std::string_view relative_path, std::string_view needle) {
    const std::string text = ReadRepoText(relative_path);
    if (text.empty() || needle.empty()) {
        return 0;
    }

    size_t count = 0;
    size_t pos = 0;
    while ((pos = text.find(std::string(needle), pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

std::vector<PlanCheckbox> LoadPlanCheckboxes() {
    const std::string text = ReadRepoText("docs/superpowers/plans/2026-04-24-boot-to-playable-recovery-plan.md");
    std::vector<PlanCheckbox> checkboxes;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        line = Trim(line);
        if (line.rfind("- [", 0) != 0 || line.size() < 6) {
            continue;
        }

        const bool checked = line[3] == 'x' || line[3] == 'X';
        const size_t close = line.find("] ");
        if (close == std::string::npos || close + 2 > line.size()) {
            continue;
        }

        checkboxes.push_back({checked, line.substr(close + 2)});
    }
    return checkboxes;
}

std::vector<ShortcutRow> LoadShortcutRows() {
    const std::string text = ReadRepoText("docs/superpowers/validation/2026-04-24-boot-helper-shortcuts.tsv");
    std::vector<ShortcutRow> rows;
    std::stringstream ss(text);
    std::string line;
    bool first = true;
    while (std::getline(ss, line)) {
        line = Trim(line);
        if (line.empty()) {
            continue;
        }
        if (first) {
            first = false;
            continue;
        }

        const auto cols = SplitTab(line);
        if (cols.size() != 4) {
            ADD_FAILURE() << "Malformed shortcut inventory row: " << line;
            continue;
        }

        rows.push_back({cols[0], cols[1], cols[2], cols[3]});
    }
    return rows;
}

std::vector<FindingRow> LoadFindingRows() {
    const std::string text = ReadRepoText("docs/superpowers/validation/2026-04-24-boot-control-flow-findings.tsv");
    std::vector<FindingRow> rows;
    std::stringstream ss(text);
    std::string line;
    bool first = true;
    while (std::getline(ss, line)) {
        line = Trim(line);
        if (line.empty()) {
            continue;
        }
        if (first) {
            first = false;
            continue;
        }

        const auto cols = SplitTab(line);
        if (cols.size() != 4) {
            ADD_FAILURE() << "Malformed control-flow findings row: " << line;
            continue;
        }

        rows.push_back({cols[0], cols[1], cols[2], cols[3]});
    }
    return rows;
}

const ShortcutRow* FindShortcutRow(const std::vector<ShortcutRow>& rows, std::string_view address) {
    for (const auto& row : rows) {
        if (row.address == address) {
            return &row;
        }
    }
    return nullptr;
}

const FindingRow* FindFindingRow(const std::vector<FindingRow>& rows, std::string_view address) {
    for (const auto& row : rows) {
        if (row.address == address) {
            return &row;
        }
    }
    return nullptr;
}

void ExpectShortcutInventoryHas(std::string_view address) {
    const auto rows = LoadShortcutRows();
    const ShortcutRow* row = FindShortcutRow(rows, address);
    ASSERT_NE(row, nullptr) << "Missing shortcut inventory row for " << address;
}

void ExpectShortcutClassification(std::string_view address, std::string_view classification) {
    const auto rows = LoadShortcutRows();
    const ShortcutRow* row = FindShortcutRow(rows, address);
    ASSERT_NE(row, nullptr) << "Missing shortcut inventory row for " << address;
    EXPECT_EQ(row->classification, classification);
}

void ExpectFindingCategory(std::string_view address, std::string_view category) {
    const auto rows = LoadFindingRows();
    const FindingRow* row = FindFindingRow(rows, address);
    ASSERT_NE(row, nullptr) << "Missing control-flow findings row for " << address;
    EXPECT_EQ(row->category, category);
}

void ExpectFindingNoteContains(std::string_view address, std::string_view needle) {
    const auto rows = LoadFindingRows();
    const FindingRow* row = FindFindingRow(rows, address);
    ASSERT_NE(row, nullptr) << "Missing control-flow findings row for " << address;
    EXPECT_NE(row->notes.find(std::string(needle)), std::string::npos)
        << "Expected finding note for " << address << " to contain: " << needle;
}

::testing::AssertionResult NoActiveShortcutRows() {
    const auto rows = LoadShortcutRows();
    std::vector<std::string> active;
    for (const auto& row : rows) {
        if (row.classification == "temporary" || row.classification == "wrong_path") {
            active.push_back(row.address + " (" + row.classification + ")");
        }
    }

    if (!active.empty()) {
        return ::testing::AssertionFailure()
            << "Active bring-up shortcuts remain: " << Join(active, ", ");
    }
    return ::testing::AssertionSuccess();
}

::testing::AssertionResult ShortcutInventoryCoversTrackedBootShims() {
    const auto rows = LoadShortcutRows();
    const std::vector<std::string> required = {
        "0x01FF80D4",
        "0x01FF80E4",
        "0x020254E8",
        "0x020254B8",
        "0x0202ABFC",
        "0x0204D9E4",
        "0x0204D050",
        "0x0204D150",
        "0x02029B64",
        "0x02014090",
        "0x0202B454"
    };

    std::vector<std::string> missing;
    for (const auto& address : required) {
        if (FindShortcutRow(rows, address) == nullptr) {
            missing.push_back(address);
        }
    }

    if (!missing.empty()) {
        return ::testing::AssertionFailure()
            << "Shortcut inventory is missing tracked addresses: " << Join(missing, ", ");
    }

    return ::testing::AssertionSuccess();
}

::testing::AssertionResult FindingsCoverCategories() {
    const auto rows = LoadFindingRows();
    std::set<std::string> categories;
    for (const auto& row : rows) {
        categories.insert(row.category);
    }

    const std::vector<std::string> required = {
        "branch_target_resolution",
        "thumb_island",
        "seed_gap",
        "literal_fallthrough"
    };

    std::vector<std::string> missing;
    for (const auto& category : required) {
        if (categories.find(category) == categories.end()) {
            missing.push_back(category);
        }
    }

    if (!missing.empty()) {
        return ::testing::AssertionFailure()
            << "Control-flow findings are missing categories: " << Join(missing, ", ");
    }

    return ::testing::AssertionSuccess();
}

::testing::AssertionResult AllExplicitSeedsHaveCommentedRationale() {
    const auto path = RepoPath("lifter/src/main.cpp");
    const std::string text = ReadTextFile(path);
    std::stringstream ss(text);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(Trim(line));
    }

    std::vector<std::string> unannotated;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& current = lines[i];
        if (current.find("decoder.AnalyzeControlFlow(0x") == std::string::npos) {
            continue;
        }

        bool has_comment = false;
        size_t nonempty_seen = 0;
        for (size_t j = i; j > 0 && nonempty_seen < 4; --j) {
            const std::string& previous = lines[j - 1];
            if (previous.empty()) {
                continue;
            }
            ++nonempty_seen;
            if (previous.rfind("//", 0) == 0) {
                has_comment = true;
                break;
            }
        }

        if (!has_comment) {
            unannotated.push_back(current);
        }
    }

    if (!unannotated.empty()) {
        return ::testing::AssertionFailure()
            << "Explicit AnalyzeControlFlow seeds need nearby rationale comments: "
            << Join(unannotated, " | ");
    }

    return ::testing::AssertionSuccess();
}

std::string EmitThumbInstruction(const std::vector<uint8_t>& code, uint32_t address) {
    ARMDecoder decoder;
    if (!decoder.Initialize()) {
        ADD_FAILURE() << "Failed to initialize ARMDecoder";
        return {};
    }

    decoder.LoadROM(code, address);
    cs_insn* insn = decoder.DecodeInstruction(address, CS_MODE_THUMB);
    if (insn == nullptr) {
        ADD_FAILURE() << "Failed to decode Thumb instruction at 0x" << std::hex << address;
        return {};
    }

    CEmitter emitter(&decoder);
    std::string out = emitter.EmitInstruction(insn);
    cs_free(insn, 1);
    return out;
}

void ExpectThumbShiftFixPresent() {
    {
        const std::vector<uint8_t> code = {0x60, 0x0A}; // lsrs r0, r4, #9
        const std::string out = EmitThumbInstruction(code, 0x02000D90);
        ASSERT_NE(out.find("ctx->r[0] = ((uint32_t)(ctx->r[4]) >> 9);"), std::string::npos);
        ASSERT_NE(out.find("CalculateFlags_MOV(ctx, ctx->r[0]);"), std::string::npos);
    }

    {
        const std::vector<uint8_t> code = {0x01, 0x11}; // asrs r1, r0, #4
        const std::string out = EmitThumbInstruction(code, 0x02000DA0);
        ASSERT_NE(out.find("ctx->r[1] = ((int32_t)(ctx->r[0]) >> 4);"), std::string::npos);
        ASSERT_NE(out.find("CalculateFlags_MOV(ctx, ctx->r[1]);"), std::string::npos);
    }
}

void ExpectStandaloneThumbShiftAuditPresent() {
    const std::vector<uint8_t> code = {0x00, 0x41}; // asrs r0, r0
    const std::string out = EmitThumbInstruction(code, 0x02000DB0);
    ASSERT_NE(out.find("ctx->r[0] = ((((ctx->r[0]) & 0xFFu) >= 32u) ? ((ctx->r[0] & 0x80000000u) ? 0xFFFFFFFFu : 0u) : ((int32_t)(ctx->r[0]) >> ((ctx->r[0]) & 0xFFu)));"), std::string::npos);
    ASSERT_NE(out.find("CalculateFlags_MOV(ctx, ctx->r[0]);"), std::string::npos);
}

void ExpectGeneratedArm9ContainsShiftFix() {
    ExpectFileContains("runtime/src/generated/arm9_chunk_0.cpp", "DS_ADDR(0x020254E8);");
    ExpectFileContains("runtime/src/generated/arm9_chunk_0.cpp", "ctx->r[0] = ((uint32_t)(ctx->r[4]) >> 9);");
}

void ExpectDecoderLiteralPoolBranchCoverage() {
    ARMDecoder decoder;
    ASSERT_TRUE(decoder.Initialize());

    // 0x02000000: ldr r0, [pc, #0]
    // 0x02000004: bx  r0
    // 0x02000008: .word 0x02000011 (Thumb target)
    // 0x02000010: movs r0, #1
    std::vector<uint8_t> data = {
        0x00, 0x00, 0x9F, 0xE5,
        0x10, 0xFF, 0x2F, 0xE1,
        0x11, 0x00, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x00,
        0x01, 0x20, 0x00, 0x00
    };

    decoder.LoadROM(data, 0x02000000);
    decoder.AnalyzeControlFlow(0x02000000);

    const auto& instructions = decoder.GetDecodedInstructions();
    auto thumb_it = instructions.find(0x02000010);
    ASSERT_NE(thumb_it, instructions.end()) << "Indirect literal-pool BX target was not decoded";
    EXPECT_EQ(thumb_it->second.mode, CS_MODE_THUMB);
}

void ExpectSaveRoundTripWorks() {
    const auto root = std::filesystem::temp_directory_path() / "kh-recovery-plan-save-roundtrip";
    std::filesystem::remove_all(root);

    const auto save_path = root / "slot0" / "system.sav";
    SaveChip writer;
    writer.SetType(SaveType::EEPROM_8KB);
    writer.data[0] = 0xDE;
    writer.data[1] = 0xAD;
    writer.data[42] = 0x42;
    ASSERT_TRUE(writer.SaveToFile(save_path.string()));
    ASSERT_TRUE(std::filesystem::exists(save_path));

    SaveChip reader;
    reader.SetType(SaveType::EEPROM_8KB);
    ASSERT_TRUE(reader.LoadFromFile(save_path.string()));
    EXPECT_EQ(reader.data[0], 0xDE);
    EXPECT_EQ(reader.data[1], 0xAD);
    EXPECT_EQ(reader.data[42], 0x42);

    std::filesystem::remove_all(root);
}

void ExpectSaveFailureHandlingWorks() {
    const auto root = std::filesystem::temp_directory_path() / "kh-recovery-plan-save-failure";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    {
        SaveChip chip;
        chip.SetType(SaveType::EEPROM_8KB);
        EXPECT_FALSE(chip.LoadFromFile((root / "missing.sav").string()));
        EXPECT_EQ(chip.data[0], 0xFF);
    }

    {
        const auto truncated_path = root / "truncated.sav";
        std::ofstream out(truncated_path, std::ios::binary | std::ios::trunc);
        out.put(static_cast<char>(0x12));
        out.put(static_cast<char>(0x34));
        out.close();

        SaveChip chip;
        chip.SetType(SaveType::EEPROM_8KB);
        ASSERT_TRUE(chip.LoadFromFile(truncated_path.string()));
        EXPECT_EQ(chip.data[0], 0x12);
        EXPECT_EQ(chip.data[1], 0x34);
        EXPECT_EQ(chip.data[2], 0xFF);
    }

    std::filesystem::remove_all(root);
}

void ExpectSaveCreatesDirectories() {
    const auto root = std::filesystem::temp_directory_path() / "kh-recovery-plan-save-dirs";
    std::filesystem::remove_all(root);

    const auto nested = root / "profiles" / "user0" / "slot0.sav";
    SaveChip chip;
    chip.SetType(SaveType::Flash_256KB);
    chip.data[0] = 0x77;
    ASSERT_TRUE(chip.SaveToFile(nested.string()));
    ASSERT_TRUE(std::filesystem::exists(nested));

    std::filesystem::remove_all(root);
}

void ExpectSmokeEvidence(std::string_view relative_path, std::string_view marker) {
    const auto path = RepoPath(relative_path);
    ASSERT_TRUE(std::filesystem::exists(path))
        << "Expected dedicated smoke coverage file: " << path.string();
    const std::string text = ReadTextFile(path);
    ASSERT_NE(text.find(std::string(marker)), std::string::npos)
        << "Expected " << path.string() << " to contain smoke marker: " << marker;
}

void ExpectBootSmokeCoverage() {
    const auto findings = LoadFindingRows();
    const auto shortcuts = LoadShortcutRows();

    ASSERT_NE(FindFindingRow(findings, "0x0202A240"), nullptr);
    ASSERT_NE(FindFindingRow(findings, "0x020235B2"), nullptr);
    ASSERT_NE(FindFindingRow(findings, "0x020069D8"), nullptr);
    ASSERT_NE(FindShortcutRow(shortcuts, "0x0202ABFC"), nullptr);
    ASSERT_NE(FindShortcutRow(shortcuts, "0x01FF80D4"), nullptr);
}

void ExpectHotRegionEmitterMatchesGeneratedOutput() {
    const std::vector<uint8_t> code = {0x60, 0x0A}; // lsrs r0, r4, #9
    const std::string emitted = EmitThumbInstruction(code, 0x02000D90);
    ASSERT_NE(emitted.find("ctx->r[0] = ((uint32_t)(ctx->r[4]) >> 9);"), std::string::npos);

    const std::string generated = ReadRepoText("runtime/src/generated/arm9_chunk_0.cpp");
    ASSERT_NE(generated.find("DS_ADDR(0x020254EA);"), std::string::npos);
    ASSERT_NE(generated.find("ctx->r[0] = ((uint32_t)(ctx->r[4]) >> 9);"), std::string::npos)
        << "Generated ARM9 output no longer matches the expected hot-region shift fix";
}

bool AnyPatternMatchesInFile(std::string_view relative_path, const std::vector<std::string>& needles) {
    if (!RepoFileExists(relative_path)) {
        return false;
    }

    const std::string text = ReadRepoText(relative_path);
    for (const auto& needle : needles) {
        if (text.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<CheckboxCase> BuildCheckboxCases() {
#define PLAN_CASE(ID, TEXT, BODY) CheckboxCase{ID, TEXT, [] BODY}

    return {
        PLAN_CASE("CurrentKnownBlockers_ConfirmStartupDataPresentAndLoaded", R"(Confirm that startup data is present and being loaded.)", {
            ExpectFileContains("runtime/src/core/main.cpp", "\"arm9.bin\"");
            ExpectFileContains("runtime/src/core/main.cpp", "\"y9.bin\"");
            ExpectFileContains("runtime/src/core/main.cpp", "g_title_screen_loader.Load");
        }),
        PLAN_CASE("CurrentKnownBlockers_ConfirmFrontendFallbackOnlyMasksDeeperBootFailure", R"(Confirm that the frontend fallback title path is only masking a deeper boot failure.)", {
            ExpectFileContains("runtime/src/core/main.cpp", "no_display_owner");
            ExpectFileContains("runtime/src/core/main.cpp", "no_3d_geometry");
            ExpectFileContains("runtime/src/core/main.cpp", "Presented title screen fallback because boot is still headless.");
        }),
        PLAN_CASE("CurrentKnownBlockers_IdentifyOneRealLifterBugAffectingEarlyArm9Boot", R"(Identify one real lifter bug affecting early ARM9 boot.)", {
            ExpectThumbShiftFixPresent();
        }),
        PLAN_CASE("CurrentKnownBlockers_FinishTracingRemainingEarlyBootHelperChainFailures", R"(Finish tracing the remaining early boot helper chain failures.)", {
            ASSERT_TRUE(ShortcutInventoryCoversTrackedBootShims());
            ExpectShortcutInventoryHas("0x01FF80D4");
            ExpectShortcutInventoryHas("0x01FF80E4");
            ExpectFindingCategory("0x020069D8", "branch_target_resolution");
            ExpectShortcutInventoryHas("0x0204D150");
        }),
        PLAN_CASE("CurrentKnownBlockers_RemoveStartupShortcutsOnceRealBehaviorWorks", R"(Remove startup shortcuts once real behavior works.)", {
            EXPECT_TRUE(NoActiveShortcutRows());
        }),

        PLAN_CASE("RecoveryStrategy_PhaseARealBootPathExecutesCorrectly", R"(Phase A: make the real boot path execute correctly)", {
            ExpectThumbShiftFixPresent();
            EXPECT_TRUE(NoActiveShortcutRows());
            ExpectFileDoesNotContain("runtime/src/core/main.cpp", "g_title_screen_loader.Render(g_renderer);");
        }),
        PLAN_CASE("RecoveryStrategy_PhaseBRemoveFakeFallbackStartupDependencies", R"(Phase B: remove the fake/fallback startup dependencies)", {
            ExpectFileDoesNotContain("runtime/src/core/main.cpp", "g_title_screen_loader.Render(g_renderer);");
            ExpectFileDoesNotContain("runtime/src/core/title_screen_loader.cpp", "PRESS START");
            ExpectFileDoesNotContain("runtime/src/core/title_screen_loader.cpp", "fallback");
        }),
        PLAN_CASE("RecoveryStrategy_PhaseCReachTitleScreenSaveInitAndCutscenes", R"(Phase C: reach title screen, save init, and cutscenes)", {
            ExpectSmokeEvidence("runtime/tests/test_title_boot.cpp", "Confirm the game itself renders title/cutscene output into the DS display path.");
            ExpectSmokeEvidence("runtime/tests/test_cutscene_flow.cpp", "Verify that intro and title-adjacent cutscenes run without being skipped.");
            ExpectFileContains("runtime/src/core/main.cpp", "save_chip.Initialize(");
        }),
        PLAN_CASE("RecoveryStrategy_PhaseDReachStableGameplay", R"(Phase D: reach stable gameplay)", {
            ExpectSmokeEvidence("runtime/tests/test_gameplay_smoke.cpp", "Confirm overlay transitions after title work.");
            ExpectSmokeEvidence("runtime/tests/test_gameplay_smoke.cpp", "Confirm player control begins without softlock or black screen.");
        }),
        PLAN_CASE("RecoveryStrategy_PhaseEHardenCorrectnessAndRegressions", R"(Phase E: harden correctness and regressions)", {
            ExpectFileContains("runtime/src/core/main.cpp", "RunDebugWatchdog");
            ExpectFileContains("runtime/src/core/main.cpp", "LogRuntimeSnapshot");
            ExpectHotRegionEmitterMatchesGeneratedOutput();
            ExpectRepoFileExists("docs/superpowers/validation/2026-04-24-boot-helper-shortcuts.tsv");
        }),

        PLAN_CASE("PhaseA_TaskA1_FixThumbImmediateShiftEmission", R"(Fix Thumb immediate `lsrs` / `asrs` emission.)", {
            ExpectThumbShiftFixPresent();
        }),
        PLAN_CASE("PhaseA_TaskA1_AddEmitterRegressionTestsForImmediateShiftForms", R"(Add emitter regression tests for those forms.)", {
            ExpectFileContains("lifter/tests/test_emitter.cpp", "EmitLSR_ThumbImmediateUsesExplicitShiftOperand");
            ExpectFileContains("lifter/tests/test_emitter.cpp", "EmitASR_ThumbImmediateUsesExplicitShiftOperand");
        }),
        PLAN_CASE("PhaseA_TaskA1_AuditOtherStandaloneThumbShiftForms", R"(Audit other standalone Thumb shift/operand forms used in the same boot cluster.)", {
            ExpectStandaloneThumbShiftAuditPresent();
        }),
        PLAN_CASE("PhaseA_TaskA1_AddTestsForNewlyFoundBadInstructionForms", R"(Add tests for any newly found bad instruction forms.)", {
            ExpectFileContains("lifter/tests/test_emitter.cpp", "EmitLSR_ThumbImmediateUsesExplicitShiftOperand");
            ExpectFileContains("lifter/tests/test_emitter.cpp", "EmitASR_ThumbImmediateUsesExplicitShiftOperand");
            ExpectFileContains("lifter/tests/test_emitter.cpp", "EmitASR_ThumbRegisterShiftUsesDestinationAsSource");
        }),
        PLAN_CASE("PhaseA_TaskA1_RegenerateArm9OutputAndVerifyChangedBlocks", R"(Regenerate ARM9 output after each lifter fix and verify the changed blocks.)", {
            ExpectGeneratedArm9ContainsShiftFix();
        }),

        PLAN_CASE("PhaseA_TaskA2_InvestigateControlFlowDataHoles", R"(Investigate why control flow reaches non-code/data holes like `0x0202A240` and `0x020235B2`.)", {
            ExpectFindingCategory("0x0202A240", "literal_fallthrough");
            ExpectFindingCategory("0x020235B2", "thumb_island");
        }),
        PLAN_CASE("PhaseA_TaskA2_DistinguishBetweenRootCauseBuckets", R"(Distinguish between:)", {
            EXPECT_TRUE(FindingsCoverCategories());
        }),
        PLAN_CASE("PhaseA_TaskA2_DistinguishBadBranchTargetResolution", R"(bad branch target resolution)", {
            ExpectFindingCategory("0x020254E8", "branch_target_resolution");
            ExpectFindingCategory("0x020069D8", "branch_target_resolution");
        }),
        PLAN_CASE("PhaseA_TaskA2_DistinguishMissedThumbIslands", R"(missed Thumb islands)", {
            ExpectFindingCategory("0x020235B2", "thumb_island");
            ExpectStandaloneThumbShiftAuditPresent();
        }),
        PLAN_CASE("PhaseA_TaskA2_DistinguishLegitimateCodeNotBeingSeeded", R"(legitimate code not being seeded)", {
            ExpectFindingCategory("0x020235B3", "seed_gap");
            ExpectFindingCategory("0x02029F4D", "seed_gap");
        }),
        PLAN_CASE("PhaseA_TaskA2_DistinguishBogusFallthroughIntoLiteralRegions", R"(bogus fallthrough into literal/data regions)", {
            ExpectFindingCategory("0x0202A240", "literal_fallthrough");
            ExpectDecoderLiteralPoolBranchCoverage();
        }),
        PLAN_CASE("PhaseA_TaskA2_AddDecoderTestsForBranchLiteralPoolPatterns", R"(Add decoder tests for branch/literal-pool patterns that match the failing startup regions.)", {
            ExpectDecoderLiteralPoolBranchCoverage();
        }),
        PLAN_CASE("PhaseA_TaskA2_ImproveCFGSeedingOnlyWithEvidence", R"(Improve CFG seeding or branch resolution only where justified by evidence.)", {
            EXPECT_TRUE(AllExplicitSeedsHaveCommentedRationale());
        }),
        PLAN_CASE("PhaseA_TaskA2_AvoidBlindStaticGapRecovery", R"(Avoid solving data-hole bugs by blindly adding more static-gap recovery.)", {
            EXPECT_EQ(CountOccurrences("runtime/src/hw/hw_overlay.cpp", "next lifted static"), 0u)
                << "Generic next-static recovery is still present in hw_overlay.cpp";
        }),

        PLAN_CASE("PhaseA_TaskA3_ReauditHelperTargets01FF80D4And01FF80E4", R"(Re-audit the `0x01FF80D4` and `0x01FF80E4` handling.)", {
            ExpectFileContains("runtime/src/hw/hw_overlay.cpp", "if (exec_addr == 0x01FF80D4)");
            ExpectFileContains("runtime/src/hw/hw_overlay.cpp", "if (exec_addr == 0x01FF80E4)");
            ExpectShortcutInventoryHas("0x01FF80D4");
            ExpectShortcutInventoryHas("0x01FF80E4");
        }),
        PLAN_CASE("PhaseA_TaskA3_DetermineRegisteredHelperStubClasses", R"(Determine which currently registered helper stubs are:)", {
            EXPECT_TRUE(ShortcutInventoryCoversTrackedBootShims());
        }),
        PLAN_CASE("PhaseA_TaskA3_ClassifyHelpersCorrectEnoughToKeep", R"(correct enough to keep)", {
            const auto rows = LoadShortcutRows();
            bool found = false;
            for (const auto& row : rows) {
                if (row.classification == "keep") {
                    found = true;
                    break;
                }
            }
            EXPECT_TRUE(found);
        }),
        PLAN_CASE("PhaseA_TaskA3_ClassifyHelpersTemporaryButHarmless", R"(temporary but harmless)", {
            const auto rows = LoadShortcutRows();
            bool found = false;
            for (const auto& row : rows) {
                if (row.classification == "temporary") {
                    found = true;
                    break;
                }
            }
            EXPECT_TRUE(found);
        }),
        PLAN_CASE("PhaseA_TaskA3_ClassifyHelpersActivelyWrongPath", R"(actively pushing boot down the wrong path)", {
            const auto rows = LoadShortcutRows();
            bool found = false;
            for (const auto& row : rows) {
                if (row.classification == "wrong_path") {
                    found = true;
                    break;
                }
            }
            EXPECT_TRUE(found);
        }),
        PLAN_CASE("PhaseA_TaskA3_ReplaceShortcutReturnsWithRealSemantics", R"(Replace shortcut returns with real semantics where possible.)", {
            EXPECT_TRUE(NoActiveShortcutRows());
        }),
        PLAN_CASE("PhaseA_TaskA3_RemoveOrNarrowSpecialCaseBootSkips", R"(Remove or narrow special-case boot skips once the underlying path works.)", {
            ExpectFileDoesNotContain("runtime/src/hw/hw_overlay.cpp", "0x02000C9C");
            ExpectFileDoesNotContain("runtime/src/hw/hw_overlay.cpp", "0x02000CB4");
            ExpectFileDoesNotContain("runtime/src/hw/hw_overlay.cpp", "0x02000D78");
        }),

        PLAN_CASE("PhaseA_TaskA4_AddSwi10BitUnpack", R"(Add `SWI 0x10 BitUnPack`.)", {
            ExpectFileContains("runtime/src/hw/hw_bios.cpp", "case 0x10: // BitUnPack");
            ExpectFileContains("runtime/tests/test_hw_bios.cpp", "SWI10BitUnPackExpands2bppTo8bpp");
        }),
        PLAN_CASE("PhaseA_TaskA4_TraceRemainingPreTitleSwis", R"(Trace any remaining SWIs used before title screen.)", {
            ExpectFileContains("runtime/src/hw/hw_bios.cpp", "case 0x04: // IntrWait");
            ExpectFileContains("runtime/src/hw/hw_bios.cpp", "case 0x06: // Halt");
            ExpectFileContains("runtime/src/hw/hw_bios.cpp", "case 0x09: // Div");
            ExpectFileContains("runtime/src/hw/hw_bios.cpp", "case 0x0B: // CpuSet");
            ExpectFileContains("runtime/src/hw/hw_bios.cpp", "case 0x0C: // CpuFastSet");
            ExpectFileContains("runtime/src/hw/hw_bios.cpp", "case 0x0D: // Sqrt");
            ExpectFileContains("runtime/src/hw/hw_bios.cpp", "case 0x11: // LZ77UnCompWrite8");
            ExpectFileContains("runtime/src/hw/hw_bios.cpp", "case 0x12: // LZ77UnCompWrite16");
        }),
        PLAN_CASE("PhaseA_TaskA4_ValidateBootHardwareCoverage", R"(Validate DMA, IPC, timers, IRQ, GX, and VRAM behavior against boot expectations.)", {
            ExpectRepoFileExists("runtime/tests/test_hw_dma.cpp");
            ExpectRepoFileExists("runtime/tests/test_hw_ipc.cpp");
            ExpectRepoFileExists("runtime/tests/test_hw_timers.cpp");
            ExpectRepoFileExists("runtime/tests/test_hw_irq.cpp");
            ExpectRepoFileExists("runtime/tests/test_hw_gxengine.cpp");
            ExpectRepoFileExists("runtime/tests/test_hw_2d_engine.cpp");
            ExpectRepoFileExists("runtime/tests/test_memory_map.cpp");
        }),
        PLAN_CASE("PhaseA_TaskA4_ConfirmDisplayInitWritesInRealPath", R"(Confirm display-init register writes happen in the real path.)", {
            ExpectFileContains("runtime/src/core/memory_map.cpp", "first DISPCNT-A write");
            ExpectFileContains("runtime/src/core/memory_map.cpp", "first DISPCNT-B write");
            ExpectFileDoesNotContain("runtime/src/core/main.cpp", "Presented title screen fallback because boot is still headless.");
        }),
        PLAN_CASE("PhaseA_TaskA4_AddTargetedTestsForMissingHardwareSemantics", R"(Add targeted tests for each missing hardware semantic found during boot.)", {
            ExpectFileContains("runtime/tests/test_hw_bios.cpp", "SWI10BitUnPackExpands2bppTo8bpp");
            ExpectRepoFileExists("runtime/tests/test_hw_dma.cpp");
            ExpectRepoFileExists("runtime/tests/test_hw_ipc.cpp");
            ExpectRepoFileExists("runtime/tests/test_hw_timers.cpp");
            ExpectRepoFileExists("runtime/tests/test_hw_irq.cpp");
            ExpectRepoFileExists("runtime/tests/test_hw_gxengine.cpp");
            ExpectRepoFileExists("runtime/tests/test_hw_overlay.cpp");
        }),

        PLAN_CASE("PhaseB_TaskB1_KeepFallbackTitleRenderingDebugOnly", R"(Keep fallback title rendering only as a debug tool, not as normal boot behavior.)", {
            ExpectFileDoesNotContain("runtime/src/core/main.cpp", "g_title_screen_loader.Render(g_renderer);");
        }),
        PLAN_CASE("PhaseB_TaskB1_GameRendersTitleAndCutsceneIntoDSPath", R"(Confirm the game itself renders title/cutscene output into the DS display path.)", {
            ExpectSmokeEvidence("runtime/tests/test_title_boot.cpp", "Confirm the game itself renders title/cutscene output into the DS display path.");
        }),
        PLAN_CASE("PhaseB_TaskB1_FrontendPresentsRealGameFrames", R"(Confirm the frontend presents real game frames, not a placeholder/headless state.)", {
            ExpectFileDoesNotContain("runtime/src/core/title_screen_loader.cpp", "PRESS START");
            ExpectFileDoesNotContain("runtime/src/core/title_screen_loader.cpp", "fallback");
            ExpectFileDoesNotContain("runtime/src/core/title_screen_loader.cpp", "placeholder");
        }),

        PLAN_CASE("PhaseB_TaskB2_TraceActualSaveInitializationPath", R"(Trace the game's actual save initialization path.)", {
            const bool has_runtime_call =
                AnyPatternMatchesInFile("runtime/src/core/main.cpp", {"save_chip.Initialize(", "GetSaveChip().Initialize("}) ||
                AnyPatternMatchesInFile("runtime/src/core/runtime_qt_frontend.cpp", {"save_chip.Initialize(", "GetSaveChip().Initialize("}) ||
                AnyPatternMatchesInFile("runtime/src/core/memory_map.cpp", {"save_chip.Initialize(", "GetSaveChip().Initialize("});
            EXPECT_TRUE(has_runtime_call) << "No runtime-owned save initialization call is wired yet.";
        }),
        PLAN_CASE("PhaseB_TaskB2_VerifyExpectedSaveFilesAndDirectories", R"(Verify that required files/directories are created exactly where the game expects them.)", {
            ExpectSaveCreatesDirectories();
            const bool has_runtime_call =
                AnyPatternMatchesInFile("runtime/src/core/main.cpp", {"save_chip.Initialize(", "GetSaveChip().Initialize("}) ||
                AnyPatternMatchesInFile("runtime/src/core/runtime_qt_frontend.cpp", {"save_chip.Initialize(", "GetSaveChip().Initialize("});
            EXPECT_TRUE(has_runtime_call) << "Directory creation exists at the SaveChip layer, but the runtime still does not wire the real game path.";
        }),
        PLAN_CASE("PhaseB_TaskB2_ConfirmFirstBootSaveCreationAndSubsequentLoad", R"(Confirm first-boot save creation and subsequent save loading.)", {
            ExpectSaveRoundTripWorks();
        }),
        PLAN_CASE("PhaseB_TaskB2_ValidateMissingOrInvalidSaveHandling", R"(Validate failure handling if a save is missing or invalid.)", {
            ExpectSaveFailureHandlingWorks();
        }),

        PLAN_CASE("PhaseB_TaskB3_VerifyIntroAndTitleAdjacentCutscenes", R"(Verify that intro and title-adjacent cutscenes run without being skipped.)", {
            ExpectSmokeEvidence("runtime/tests/test_cutscene_flow.cpp", "Verify that intro and title-adjacent cutscenes run without being skipped.");
        }),
        PLAN_CASE("PhaseB_TaskB3_ConfirmAudioVideoTimingSyncForCutscenes", R"(Confirm audio/video/timing stay synchronized enough for those sequences.)", {
            ExpectSmokeEvidence("runtime/tests/test_cutscene_flow.cpp", "Confirm audio/video/timing stay synchronized enough for those sequences.");
        }),
        PLAN_CASE("PhaseB_TaskB3_FixCutsceneModeTransitionsAndResourceLoads", R"(Fix any mode transitions or resource loads used specifically by cutscene paths.)", {
            ExpectSmokeEvidence("runtime/tests/test_cutscene_flow.cpp", "Fix any mode transitions or resource loads used specifically by cutscene paths.");
        }),

        PLAN_CASE("PhaseC_TaskC1_ConfirmOverlayTransitionsAfterTitle", R"(Confirm overlay transitions after title work.)", {
            ExpectSmokeEvidence("runtime/tests/test_gameplay_smoke.cpp", "Confirm overlay transitions after title work.");
        }),
        PLAN_CASE("PhaseC_TaskC1_ConfirmWorldMissionLoadPaths", R"(Confirm world/mission load paths work.)", {
            ExpectSmokeEvidence("runtime/tests/test_gameplay_smoke.cpp", "Confirm world/mission load paths work.");
        }),
        PLAN_CASE("PhaseC_TaskC1_ConfirmPlayerControlBeginsWithoutSoftlock", R"(Confirm player control begins without softlock or black screen.)", {
            ExpectSmokeEvidence("runtime/tests/test_gameplay_smoke.cpp", "Confirm player control begins without softlock or black screen.");
        }),

        PLAN_CASE("PhaseC_TaskC2_ValidatePlayerInput", R"(Validate player input.)", {
            ExpectFileContains("runtime/tests/test_hw_input.cpp", "VirtualButtonPress");
            ExpectFileContains("runtime/tests/test_hw_input.cpp", "TouchScaling");
            ExpectFileContains("runtime/tests/test_hw_input.cpp", "AnalogInputMapsToDPad");
        }),
        PLAN_CASE("PhaseC_TaskC2_ValidateTimingSensitiveGameplayLogic", R"(Validate timing-sensitive gameplay logic.)", {
            ExpectRepoFileExists("runtime/tests/test_hw_timers.cpp");
            ExpectRepoFileExists("runtime/tests/test_hw_irq.cpp");
        }),
        PLAN_CASE("PhaseC_TaskC2_ValidateBattleAndUITransitions", R"(Validate battle/UI transitions.)", {
            ExpectSmokeEvidence("runtime/tests/test_gameplay_smoke.cpp", "Validate battle/UI transitions.");
        }),
        PLAN_CASE("PhaseC_TaskC2_ValidateMenuFlowsAndPauseStates", R"(Validate menu flows and pause/state changes.)", {
            ExpectSmokeEvidence("runtime/tests/test_gameplay_smoke.cpp", "Validate menu flows and pause/state changes.");
        }),
        PLAN_CASE("PhaseC_TaskC2_ValidateAssetStreamingAndOverlaySwapsDuringPlay", R"(Validate asset streaming and overlay swaps during play.)", {
            ExpectRepoFileExists("runtime/tests/test_hw_overlay.cpp");
            ExpectFileContains("runtime/tests/test_hw_overlay.cpp", "DispatchOverlayOverlap");
            ExpectRepoFileExists("runtime/tests/test_vfs.cpp");
        }),

        PLAN_CASE("PhaseD_TaskD1_BootFromCleanState", R"(Boot from clean state)", {
            ExpectSmokeEvidence("runtime/tests/test_playability_smoke.cpp", "Boot from clean state");
        }),
        PLAN_CASE("PhaseD_TaskD1_IntroCutscenePath", R"(Intro/cutscene path)", {
            ExpectSmokeEvidence("runtime/tests/test_playability_smoke.cpp", "Intro/cutscene path");
        }),
        PLAN_CASE("PhaseD_TaskD1_TitleScreen", R"(Title screen)", {
            ExpectSmokeEvidence("runtime/tests/test_playability_smoke.cpp", "Title screen");
        }),
        PLAN_CASE("PhaseD_TaskD1_NewGame", R"(New game)", {
            ExpectSmokeEvidence("runtime/tests/test_playability_smoke.cpp", "New game");
        }),
        PLAN_CASE("PhaseD_TaskD1_SaveCreation", R"(Save creation)", {
            ExpectSmokeEvidence("runtime/tests/test_playability_smoke.cpp", "Save creation");
        }),
        PLAN_CASE("PhaseD_TaskD1_LoadExistingSave", R"(Load existing save)", {
            ExpectSmokeEvidence("runtime/tests/test_playability_smoke.cpp", "Load existing save");
        }),
        PLAN_CASE("PhaseD_TaskD1_EnterGameplay", R"(Enter gameplay)", {
            ExpectSmokeEvidence("runtime/tests/test_playability_smoke.cpp", "Enter gameplay");
        }),
        PLAN_CASE("PhaseD_TaskD1_MoveAttackInteractTransitionScenes", R"(Move, attack, interact, transition scenes)", {
            ExpectSmokeEvidence("runtime/tests/test_playability_smoke.cpp", "Move, attack, interact, transition scenes");
        }),
        PLAN_CASE("PhaseD_TaskD1_OpenMenus", R"(Open menus)", {
            ExpectSmokeEvidence("runtime/tests/test_playability_smoke.cpp", "Open menus");
        }),
        PLAN_CASE("PhaseD_TaskD1_SurviveExtendedPlaySession", R"(Survive extended play session)", {
            ExpectSmokeEvidence("runtime/tests/test_playability_smoke.cpp", "Survive extended play session");
        }),

        PLAN_CASE("PhaseD_TaskD2_FirstBoot", R"(First boot)", {
            ExpectSmokeEvidence("runtime/tests/test_playability_smoke.cpp", "First boot");
        }),
        PLAN_CASE("PhaseD_TaskD2_ReturningBootWithExistingSave", R"(Returning boot with existing save)", {
            ExpectSmokeEvidence("runtime/tests/test_playability_smoke.cpp", "Returning boot with existing save");
        }),
        PLAN_CASE("PhaseD_TaskD2_AtLeastOneCombatSequence", R"(At least one combat sequence)", {
            ExpectSmokeEvidence("runtime/tests/test_playability_smoke.cpp", "At least one combat sequence");
        }),
        PLAN_CASE("PhaseD_TaskD2_AtLeastOneRoomOrSceneTransition", R"(At least one room/scene transition)", {
            ExpectSmokeEvidence("runtime/tests/test_playability_smoke.cpp", "At least one room/scene transition");
        }),
        PLAN_CASE("PhaseD_TaskD2_AtLeastOneCutsceneAfterBoot", R"(At least one cutscene after boot)", {
            ExpectSmokeEvidence("runtime/tests/test_playability_smoke.cpp", "At least one cutscene after boot");
        }),
        PLAN_CASE("PhaseD_TaskD2_AtLeastOneSaveLoadRoundTrip", R"(At least one save/load round trip)", {
            ExpectSmokeEvidence("runtime/tests/test_playability_smoke.cpp", "At least one save/load round trip");
        }),

        PLAN_CASE("PhaseD_TaskD3_ReviewBootHelperShortcutsAndDebugRecoveries", R"(Review all boot/helper shortcuts and debug-only recoveries.)", {
            EXPECT_TRUE(ShortcutInventoryCoversTrackedBootShims());
        }),
        PLAN_CASE("PhaseD_TaskD3_RemoveNoLongerNeededShortcuts", R"(Remove those no longer needed.)", {
            EXPECT_TRUE(NoActiveShortcutRows());
        }),
        PLAN_CASE("PhaseD_TaskD3_IsolateTemporaryWorkaroundsBehindCommentsAndFlags", R"(Clearly isolate any unavoidable temporary workarounds behind comments and flags.)", {
            const auto rows = LoadShortcutRows();
            ASSERT_FALSE(rows.empty());
            for (const auto& row : rows) {
                EXPECT_FALSE(row.reason.empty()) << "Missing reason for shortcut " << row.address;
                ExpectFileContains(row.file, row.address);
            }
        }),

        PLAN_CASE("ValidationAndTooling_KeepWatchdogRunsForStartupStalls", R"(Watchdog runs for startup stalls)", {
            ExpectFileContains("runtime/src/core/main.cpp", "RunDebugWatchdog");
            ExpectFileContains("runtime/src/core/main.cpp", "KH_DEBUG_WATCHDOG");
        }),
        PLAN_CASE("ValidationAndTooling_KeepRuntimeSnapshotsForBootStateGlobals", R"(Runtime snapshots for boot-state globals)", {
            ExpectFileContains("runtime/src/core/main.cpp", "LogRuntimeSnapshot");
            ExpectFileContains("runtime/src/core/main.cpp", "boot_state_fde0");
            ExpectFileContains("runtime/src/core/main.cpp", "boot_flag_603c8");
        }),
        PLAN_CASE("ValidationAndTooling_KeepCapstoneComparisonAgainstGeneratedBlocks", R"(Capstone comparison against generated blocks)", {
            ExpectHotRegionEmitterMatchesGeneratedOutput();
        }),
        PLAN_CASE("ValidationAndTooling_KeepFocusedRegressionTestsInLifterAndRuntime", R"(Focused regression tests in lifter and runtime)", {
            ExpectRepoFileExists("lifter/tests/test_emitter.cpp");
            ExpectRepoFileExists("lifter/tests/test_decoder.cpp");
            ExpectRepoFileExists("runtime/tests/test_hw_bios.cpp");
            ExpectRepoFileExists("runtime/tests/test_hw_overlay.cpp");
            ExpectRepoFileExists("runtime/tests/test_hw_save.cpp");
            ExpectRepoFileExists("runtime/tests/test_hw_input.cpp");
        }),

        PLAN_CASE("ValidationAndTooling_AddBootSmokeTestForKnownUnmappedTargets", R"(A repeatable "boot smoke test" that fails when startup falls into known unmapped targets.)", {
            ExpectBootSmokeCoverage();
        }),
        PLAN_CASE("ValidationAndTooling_AddDecodedInstructionVsEmittedCodeHarness", R"(A small comparison harness for decoded instruction vs emitted code in hot regions.)", {
            ExpectHotRegionEmitterMatchesGeneratedOutput();
        }),
        PLAN_CASE("ValidationAndTooling_AddTrackedHelperShortcutList", R"(A tracked list of currently intentional helper shortcuts and why they still exist.)", {
            const auto rows = LoadShortcutRows();
            ASSERT_FALSE(rows.empty());
            for (const auto& row : rows) {
                EXPECT_FALSE(row.reason.empty()) << "Shortcut " << row.address << " is missing a reason";
            }
        }),

        PLAN_CASE("ImmediateNextSteps_KeepThumbShiftFixAndRegeneratedArm9Output", R"(Keep the Thumb shift fix and regenerated ARM9 output.)", {
            ExpectThumbShiftFixPresent();
            ExpectGeneratedArm9ContainsShiftFix();
        }),
        PLAN_CASE("ImmediateNextSteps_Investigate0202A240AsControlFlowDataHole", R"(Investigate the `0x0202A240` path as a control-flow/data-hole problem, not a missing asset problem.)", {
            ExpectFindingCategory("0x0202A240", "literal_fallthrough");
            ExpectFindingNoteContains("0x0202A240", "not a missing asset load");
        }),
        PLAN_CASE("ImmediateNextSteps_ReconstructChainThatReaches020235B2", R"(Reconstruct the real chain that currently reaches `0x020235B2`.)", {
            const auto rows = LoadFindingRows();
            const FindingRow* row = FindFindingRow(rows, "0x020235B2");
            ASSERT_NE(row, nullptr);
            EXPECT_EQ(row->status, "resolved") << "The 0x020235B2 chain is still unresolved: " << row->notes;
        }),
        PLAN_CASE("ImmediateNextSteps_AddDecoderTestsOrTargetedSeedingOnlyWhenProvenCode", R"(Add decoder tests or targeted seeding only if the target is proven to be real executable code.)", {
            ExpectDecoderLiteralPoolBranchCoverage();
            EXPECT_TRUE(AllExplicitSeedsHaveCommentedRationale());
        }),
        PLAN_CASE("ImmediateNextSteps_Revisit01FF80D4OnlyAfterHelperChainValid", R"(Revisit the `0x01FF80D4` shortcut only after the helper chain behind it is actually valid.)", {
            ExpectShortcutClassification("0x01FF80D4", "wrong_path");
            const auto rows = LoadShortcutRows();
            const ShortcutRow* row = FindShortcutRow(rows, "0x01FF80D4");
            ASSERT_NE(row, nullptr);
            EXPECT_NE(row->reason.find("unresolved helper chain"), std::string::npos);
        }),
    };

#undef PLAN_CASE
}

const std::vector<CheckboxCase>& AllCheckboxCases() {
    static const std::vector<CheckboxCase> cases = BuildCheckboxCases();
    return cases;
}

} // namespace

TEST(BootRecoveryPlanCoverage, EveryCheckboxHasAnExecutableCase) {
    const auto plan_checkboxes = LoadPlanCheckboxes();
    const auto& cases = AllCheckboxCases();

    ASSERT_EQ(plan_checkboxes.size(), cases.size())
        << "Plan checkbox count changed; update test coverage.";

    std::set<std::string> plan_texts;
    for (const auto& checkbox : plan_checkboxes) {
        plan_texts.insert(checkbox.text);
    }

    std::set<std::string> case_texts;
    for (const auto& test_case : cases) {
        case_texts.insert(test_case.checkbox_text);
    }

    std::vector<std::string> missing_cases;
    for (const auto& checkbox_text : plan_texts) {
        if (case_texts.find(checkbox_text) == case_texts.end()) {
            missing_cases.push_back(checkbox_text);
        }
    }

    std::vector<std::string> orphan_cases;
    for (const auto& case_text : case_texts) {
        if (plan_texts.find(case_text) == plan_texts.end()) {
            orphan_cases.push_back(case_text);
        }
    }

    EXPECT_TRUE(missing_cases.empty()) << "Missing checkbox cases: " << Join(missing_cases, " | ");
    EXPECT_TRUE(orphan_cases.empty()) << "Case text no longer present in the plan: " << Join(orphan_cases, " | ");
}

class BootRecoveryPlanCheckboxTest : public ::testing::TestWithParam<CheckboxCase> {};

TEST_P(BootRecoveryPlanCheckboxTest, EnforcesCheckboxContract) {
    const auto& checkbox = GetParam();
    SCOPED_TRACE(checkbox.checkbox_text);
    checkbox.verify();
}

INSTANTIATE_TEST_SUITE_P(
    EveryCheckboxHasExecutableContract,
    BootRecoveryPlanCheckboxTest,
    ::testing::ValuesIn(AllCheckboxCases()),
    [](const ::testing::TestParamInfo<CheckboxCase>& info) { return info.param.id; });
