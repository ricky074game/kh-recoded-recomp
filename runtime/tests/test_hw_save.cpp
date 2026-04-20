#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "hw_save.h"
#include "memory_map.h"

// Test save directory within the workspace
static const std::string TEST_SAVE_DIR = std::filesystem::temp_directory_path().string() + "/kh-recoded-recomp-test-saves";
static std::string TestSavePath(const std::string& name) {
    return TEST_SAVE_DIR + "/" + name;
}

// ============================================================================
// AUXSPICNT Register Tests
// ============================================================================

TEST(SaveSPI, SPICNT_WriteRead) {
    AUXSPIControl ctrl;
    ctrl.Write(0xE043); // enable=1, irq=1, slot=1, cs_hold=1, baud=3
    EXPECT_TRUE(ctrl.enable);
    EXPECT_TRUE(ctrl.irq_enable);
    EXPECT_TRUE(ctrl.slot_mode);
    EXPECT_TRUE(ctrl.cs_hold);
    EXPECT_EQ(ctrl.baudrate, 3);
    uint16_t val = ctrl.Read();
    EXPECT_TRUE(val & (1 << 15)); // enable
    EXPECT_TRUE(val & (1 << 6));  // cs_hold
}

TEST(SaveSPI, SPICNT_Disabled) {
    AUXSPIControl ctrl;
    ctrl.Write(0x0000);
    EXPECT_FALSE(ctrl.enable);
    EXPECT_FALSE(ctrl.cs_hold);
}

// ============================================================================
// SaveChip Initialization Tests
// ============================================================================

TEST(SaveChip, Init_Flash512KB) {
    SaveChip chip;
    chip.SetType(SaveType::Flash_512KB);
    EXPECT_EQ(chip.capacity, 524288u);
    EXPECT_EQ(chip.data.size(), 524288u);
    // Erased state should be 0xFF
    EXPECT_EQ(chip.data[0], 0xFF);
    EXPECT_EQ(chip.data[524287], 0xFF);
}

TEST(SaveChip, Init_EEPROM_512B) {
    SaveChip chip;
    chip.SetType(SaveType::EEPROM_512B);
    EXPECT_EQ(chip.capacity, 512u);
}

TEST(SaveChip, Init_FRAM_32KB) {
    SaveChip chip;
    chip.SetType(SaveType::FRAM_32KB);
    EXPECT_EQ(chip.capacity, 32768u);
}

// ============================================================================
// SPI Command Protocol Tests
// ============================================================================

// Helper: enable SPI with CS hold
static void EnableSPI(SaveChip& chip) {
    chip.WriteSPICNT(0x8040); // enable=1, cs_hold=1
}
static void ReleaseSPI(SaveChip& chip) {
    chip.WriteSPICNT(0x8000); // enable=1, cs_hold=0 (last byte)
}

TEST(SaveSPI, WREN_WRDI) {
    SaveChip chip;
    chip.SetType(SaveType::Flash_256KB);
    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::WREN);
    EXPECT_TRUE(chip.write_enabled);
    ReleaseSPI(chip);

    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::WRDI);
    EXPECT_FALSE(chip.write_enabled);
    ReleaseSPI(chip);
}

TEST(SaveSPI, RDSR_WriteEnableBit) {
    SaveChip chip;
    chip.SetType(SaveType::Flash_256KB);

    // Read status — WEL should be 0
    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::RDSR);
    chip.WriteSPIData(0x00); // dummy read
    EXPECT_EQ(chip.ReadSPIData() & 0x02, 0);
    ReleaseSPI(chip);

    // Enable writes and check WEL
    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::WREN);
    ReleaseSPI(chip);

    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::RDSR);
    chip.WriteSPIData(0x00);
    EXPECT_EQ(chip.ReadSPIData() & 0x02, 0x02); // WEL set
    ReleaseSPI(chip);
}

TEST(SaveSPI, WriteAndRead_Flash) {
    SaveChip chip;
    chip.SetType(SaveType::Flash_256KB);

    // Write enable
    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::WREN);
    ReleaseSPI(chip);

    // Write 3 bytes at address 0x000100
    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::WRITE);
    chip.WriteSPIData(0x00); // addr high
    chip.WriteSPIData(0x01); // addr mid
    chip.WriteSPIData(0x00); // addr low
    chip.WriteSPIData(0xAA); // data[0]
    chip.WriteSPIData(0xBB); // data[1]
    chip.WriteSPIData(0xCC); // data[2]
    ReleaseSPI(chip);

    // Read back
    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::READ);
    chip.WriteSPIData(0x00);
    chip.WriteSPIData(0x01);
    chip.WriteSPIData(0x00);
    chip.WriteSPIData(0x00); // dummy → read data[0]
    EXPECT_EQ(chip.ReadSPIData(), 0xAA);
    chip.WriteSPIData(0x00); // dummy → read data[1]
    EXPECT_EQ(chip.ReadSPIData(), 0xBB);
    chip.WriteSPIData(0x00); // dummy → read data[2]
    EXPECT_EQ(chip.ReadSPIData(), 0xCC);
    ReleaseSPI(chip);
}

TEST(SaveSPI, WriteAndRead_EEPROM_8KB) {
    SaveChip chip;
    chip.SetType(SaveType::EEPROM_8KB);

    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::WREN);
    ReleaseSPI(chip);

    // Write at address 0x0010 (2-byte address)
    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::WRITE);
    chip.WriteSPIData(0x00); // addr high
    chip.WriteSPIData(0x10); // addr low
    chip.WriteSPIData(0x42); // data
    ReleaseSPI(chip);

    // Read back
    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::READ);
    chip.WriteSPIData(0x00);
    chip.WriteSPIData(0x10);
    chip.WriteSPIData(0x00); // dummy
    EXPECT_EQ(chip.ReadSPIData(), 0x42);
    ReleaseSPI(chip);
}

TEST(SaveSPI, WriteBlocked_WithoutWREN) {
    SaveChip chip;
    chip.SetType(SaveType::Flash_256KB);
    // Don't send WREN — write should be blocked
    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::WRITE);
    // Should stay idle since write_enabled=false
    EXPECT_EQ(chip.state, SPIState::Idle);
    ReleaseSPI(chip);
}

TEST(SaveSPI, SequentialRead) {
    SaveChip chip;
    chip.SetType(SaveType::Flash_256KB);

    // Pre-fill some data
    chip.data[0] = 0x10;
    chip.data[1] = 0x20;
    chip.data[2] = 0x30;
    chip.data[3] = 0x40;

    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::READ);
    chip.WriteSPIData(0x00); chip.WriteSPIData(0x00); chip.WriteSPIData(0x00);
    for (uint8_t expected : {0x10u, 0x20u, 0x30u, 0x40u}) {
        chip.WriteSPIData(0x00);
        EXPECT_EQ(chip.ReadSPIData(), expected);
    }
    ReleaseSPI(chip);
}

TEST(SaveSPI, RDID_JEDECId) {
    SaveChip chip;
    chip.SetType(SaveType::Flash_512KB);
    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::RDID);
    uint8_t byte0 = chip.ReadSPIData(); // Manufacturer
    chip.WriteSPIData(0x00);
    uint8_t byte1 = chip.ReadSPIData(); // Device high
    chip.WriteSPIData(0x00);
    uint8_t byte2 = chip.ReadSPIData(); // Device low
    // ST M45PE40: 0x204013
    EXPECT_EQ(byte0, 0x20);
    EXPECT_EQ(byte1, 0x40);
    EXPECT_EQ(byte2, 0x13);
    ReleaseSPI(chip);
}

TEST(SaveSPI, SectorErase_4K) {
    SaveChip chip;
    chip.SetType(SaveType::Flash_256KB);
    // Fill first 8KB with data
    std::memset(chip.data.data(), 0x55, 8192);

    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::WREN);
    ReleaseSPI(chip);

    // Erase 4KB sector at address 0x000000
    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::SE_4K);
    chip.WriteSPIData(0x00); chip.WriteSPIData(0x00); chip.WriteSPIData(0x00);
    ReleaseSPI(chip);

    // First 4KB erased (0xFF), next 4KB untouched (0x55)
    EXPECT_EQ(chip.data[0], 0xFF);
    EXPECT_EQ(chip.data[4095], 0xFF);
    EXPECT_EQ(chip.data[4096], 0x55);
    EXPECT_TRUE(chip.IsDirty());
}

TEST(SaveSPI, ChipErase) {
    SaveChip chip;
    chip.SetType(SaveType::EEPROM_8KB);
    std::memset(chip.data.data(), 0xAA, chip.capacity);

    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::WREN);
    ReleaseSPI(chip);

    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::CE);
    ReleaseSPI(chip);

    for (uint32_t i = 0; i < chip.capacity; ++i) {
        EXPECT_EQ(chip.data[i], 0xFF);
    }
}

// ============================================================================
// File Persistence Tests
// ============================================================================

class SaveFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::create_directories(TEST_SAVE_DIR);
    }
    void TearDown() override {
        std::filesystem::remove_all(TEST_SAVE_DIR);
    }
};

TEST_F(SaveFileTest, SaveAndLoad) {
    std::string path = TestSavePath("test_save.sav");

    // Create a chip with some data and save
    {
        SaveChip chip;
        chip.SetType(SaveType::EEPROM_8KB);
        chip.data[0] = 0xDE;
        chip.data[1] = 0xAD;
        chip.data[100] = 0xBE;
        chip.data[101] = 0xEF;
        EXPECT_TRUE(chip.SaveToFile(path));
        EXPECT_TRUE(std::filesystem::exists(path));

        auto fsize = std::filesystem::file_size(path);
        EXPECT_EQ(fsize, 8192u); // Full save size written
    }

    // Load into a new chip and verify
    {
        SaveChip chip2;
        chip2.SetType(SaveType::EEPROM_8KB);
        EXPECT_TRUE(chip2.LoadFromFile(path));
        EXPECT_EQ(chip2.data[0], 0xDE);
        EXPECT_EQ(chip2.data[1], 0xAD);
        EXPECT_EQ(chip2.data[100], 0xBE);
        EXPECT_EQ(chip2.data[101], 0xEF);
    }
}

TEST_F(SaveFileTest, BackupCreated) {
    std::string path = TestSavePath("backup_test.sav");

    // First save
    SaveChip chip;
    chip.SetType(SaveType::EEPROM_512B);
    chip.data[0] = 0x11;
    chip.SaveToFile(path);

    // Second save — should create .bak
    chip.data[0] = 0x22;
    chip.SaveToFile(path);

    EXPECT_TRUE(std::filesystem::exists(path + ".bak"));

    // Verify backup contains old data
    std::ifstream bak(path + ".bak", std::ios::binary);
    uint8_t first_byte;
    bak.read(reinterpret_cast<char*>(&first_byte), 1);
    EXPECT_EQ(first_byte, 0x11);
}

TEST_F(SaveFileTest, LoadNonExistent) {
    SaveChip chip;
    chip.SetType(SaveType::EEPROM_8KB);
    // Should return false but not crash
    EXPECT_FALSE(chip.LoadFromFile(TestSavePath("does_not_exist.sav")));
    // Data should remain at erased state
    EXPECT_EQ(chip.data[0], 0xFF);
}

TEST_F(SaveFileTest, FlushIfDirty) {
    std::string path = TestSavePath("flush_test.sav");
    SaveChip chip;
    chip.Initialize(SaveType::Flash_256KB, path);

    EXPECT_FALSE(chip.IsDirty());
    EXPECT_FALSE(chip.FlushIfDirty()); // Not dirty

    // Write some data through SPI
    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::WREN);
    ReleaseSPI(chip);
    EnableSPI(chip);
    chip.WriteSPIData(SPICmd::WRITE);
    chip.WriteSPIData(0x00); chip.WriteSPIData(0x00); chip.WriteSPIData(0x00);
    chip.WriteSPIData(0x42);
    ReleaseSPI(chip);

    EXPECT_TRUE(chip.IsDirty());
    EXPECT_TRUE(chip.FlushIfDirty());
    EXPECT_FALSE(chip.IsDirty()); // Cleared after flush

    // Verify file exists and has the data
    EXPECT_TRUE(std::filesystem::exists(path));
    SaveChip verify;
    verify.SetType(SaveType::Flash_256KB);
    verify.LoadFromFile(path);
    EXPECT_EQ(verify.data[0], 0x42);
}

TEST_F(SaveFileTest, InitializeWithFile) {
    std::string path = TestSavePath("init_test.sav");

    // Create initial save
    {
        SaveChip chip;
        chip.SetType(SaveType::EEPROM_8KB);
        chip.data[42] = 0x99;
        chip.SaveToFile(path);
    }

    // Initialize from file — should load existing data
    SaveChip chip2;
    chip2.Initialize(SaveType::EEPROM_8KB, path);
    EXPECT_EQ(chip2.data[42], 0x99);
}

// ============================================================================
// Memory Map Integration
// ============================================================================

TEST(SaveSPI, MemMap_AUXSPICNT) {
    NDSMemory m;
    m.save_chip.SetType(SaveType::Flash_256KB);
    m.Write16(0x040001A0, 0x8040); // Enable + CS hold
    uint16_t cnt = m.Read16(0x040001A0);
    EXPECT_TRUE(cnt & (1 << 15)); // Enabled
    EXPECT_TRUE(cnt & (1 << 6));  // CS hold
}

TEST(SaveSPI, MemMap_WriteRead) {
    NDSMemory m;
    m.save_chip.SetType(SaveType::EEPROM_8KB);

    // WREN through memory map
    m.Write16(0x040001A0, 0x8040); // Enable + CS hold
    m.Write8(0x040001A2, SPICmd::WREN);
    m.Write16(0x040001A0, 0x8000); // Release CS

    // WRITE through memory map
    m.Write16(0x040001A0, 0x8040);
    m.Write8(0x040001A2, SPICmd::WRITE);
    m.Write8(0x040001A2, 0x00); // addr high
    m.Write8(0x040001A2, 0x05); // addr low
    m.Write8(0x040001A2, 0x77); // data
    m.Write16(0x040001A0, 0x8000);

    // READ through memory map
    m.Write16(0x040001A0, 0x8040);
    m.Write8(0x040001A2, SPICmd::READ);
    m.Write8(0x040001A2, 0x00);
    m.Write8(0x040001A2, 0x05);
    m.Write8(0x040001A2, 0x00); // dummy
    uint8_t result = m.Read8(0x040001A2);
    m.Write16(0x040001A0, 0x8000);

    EXPECT_EQ(result, 0x77);
}

// --- Phase 3 Test Additions ---
TEST_F(SaveFileTest, FlushToDisk_Test) {
    SaveChip chip;
    chip.SetType(SaveType::EEPROM_8KB);
    chip.save_path = TestSavePath("flush_test_method.sav");
    chip.data[0] = 0xAA;
    chip.dirty = true;
    EXPECT_NO_THROW(chip.FlushToDisk());
    EXPECT_TRUE(std::filesystem::exists(chip.save_path));
}

TEST_F(SaveFileTest, LoadFromDisk_Test) {
    SaveChip chip;
    chip.SetType(SaveType::EEPROM_8KB);
    chip.save_path = TestSavePath("load_test_method.sav");
    chip.data[0] = 0xBB;
    chip.SaveToFile();

    SaveChip chip2;
    chip2.SetType(SaveType::EEPROM_8KB);
    chip2.save_path = TestSavePath("load_test_method.sav");
    EXPECT_NO_THROW(chip2.LoadFromDisk());
    EXPECT_EQ(chip2.data[0], 0xBB);
}

TEST_F(SaveFileTest, EraseSector_Test) {
    SaveChip chip;
    chip.SetType(SaveType::Flash_256KB);
    std::memset(chip.data.data(), 0x55, 8192);
    EXPECT_NO_THROW(chip.EraseSector(0x000000));
    EXPECT_EQ(chip.data[0], 0xFF);
    EXPECT_EQ(chip.data[4095], 0xFF);
    EXPECT_EQ(chip.data[4096], 0x55);
}

TEST_F(SaveFileTest, ComputeSaveChecksum_Test) {
    SaveChip chip;
    chip.SetType(SaveType::EEPROM_8KB);
    EXPECT_NO_THROW(chip.ComputeSaveChecksum());
}

TEST_F(SaveFileTest, FormatSaveChip_Test) {
    SaveChip chip;
    chip.SetType(SaveType::EEPROM_8KB);
    chip.data[0] = 0x11;
    EXPECT_NO_THROW(chip.FormatSaveChip());
    EXPECT_EQ(chip.data[0], 0xFF);
}
