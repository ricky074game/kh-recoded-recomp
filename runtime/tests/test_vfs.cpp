#include <gtest/gtest.h>
#include "vfs.h"
#include <fstream>
#include <filesystem>
#include <vector>

// Dummy ROM Header Definition
#pragma pack(push, 1)
struct DummyNDSRomHeader {
    char gameTitle[12];
    char gameCode[4];
    char makerCode[2];
    uint8_t unitCode;
    uint8_t encryptionSeedSelect;
    uint8_t deviceCapacity;
    uint8_t reserved[7];
    uint8_t version;
    uint8_t autostart;
    uint32_t arm9RomOffset;
    uint32_t arm9EntryAddress;
    uint32_t arm9RamAddress;
    uint32_t arm9Size;
    uint32_t arm7RomOffset;
    uint32_t arm7EntryAddress;
    uint32_t arm7RamAddress;
    uint32_t arm7Size;
    uint32_t fntOffset;
    uint32_t fntSize;
    uint32_t fatOffset;
    uint32_t fatSize;
};
#pragma pack(pop)

class VFSTest : public ::testing::Test {
protected:
    std::string testRomPath = std::filesystem::temp_directory_path().string() + "/test_dummy.nds";
    std::string testDataDir = std::filesystem::temp_directory_path().string() + "/test_data_dir";

    void SetUp() override {
        std::filesystem::create_directory(testDataDir);
        std::filesystem::create_directory(testDataDir + "/dummy_dir");

        // Write dummy data files
        std::ofstream file1(testDataDir + "/file1.bin", std::ios::binary);
        file1.write("DATA1", 5);
        file1.close();

        std::ofstream file2(testDataDir + "/dummy_dir/file2.bin", std::ios::binary);
        file2.write("DATA2", 5);
        file2.close();

        // Create dummy ROM
        std::ofstream rom(testRomPath, std::ios::binary);
        DummyNDSRomHeader header = {0};
        header.fatOffset = sizeof(DummyNDSRomHeader);
        header.fatSize = 16; // 2 files * 8 bytes
        header.fntOffset = header.fatOffset + header.fatSize;
        
        // --- Setup FNT Size ---
        // Directory Main Table: 2 entries * 8 bytes = 16 bytes
        // Root dir name table: 1 file (7 bytes) + 1 dir (12 bytes) + end (1 byte) = 20 bytes
        // Sub dir name table: 1 file (7 bytes) + end (1 byte) = 8 bytes
        // Total FNT Size = 16 + 20 + 8 = 44 bytes
        header.fntSize = 44;
        
        rom.write(reinterpret_cast<char*>(&header), sizeof(header));

        // FAT Entries (2 files)
        uint32_t fatData[4] = {0x1000, 0x1005, 0x2000, 0x2005};
        rom.write(reinterpret_cast<char*>(fatData), sizeof(fatData));

        // FNT Entries
        // Directory Main Table
        uint32_t rootOffset = 16; // from fntOffset
        uint16_t rootFirstId = 0;
        uint16_t numDirs = 2;
        rom.write(reinterpret_cast<char*>(&rootOffset), 4);
        rom.write(reinterpret_cast<char*>(&rootFirstId), 2);
        rom.write(reinterpret_cast<char*>(&numDirs), 2);

        uint32_t subOffset = 39; // 16 (table) + 23 (root dir name table) = 39
        uint16_t subFirstId = 1;
        uint16_t subParent = 0xF000;
        rom.write(reinterpret_cast<char*>(&subOffset), 4);
        rom.write(reinterpret_cast<char*>(&subFirstId), 2);
        rom.write(reinterpret_cast<char*>(&subParent), 2);

        // Root Name Table
        uint8_t typeLenFile1 = 9; // file name length 9 "file1.bin"
        rom.write(reinterpret_cast<char*>(&typeLenFile1), 1);
        rom.write("file1.bin", 9);

        uint8_t typeLenDir = 0x80 | 9; // dir name length 9 "dummy_dir"
        rom.write(reinterpret_cast<char*>(&typeLenDir), 1);
        rom.write("dummy_dir", 9);
        uint16_t dirId = 0xF001;
        rom.write(reinterpret_cast<char*>(&dirId), 2);

        uint8_t endByte = 0;
        rom.write(reinterpret_cast<char*>(&endByte), 1);

        // Sub Name Table
        uint8_t typeLenFile2 = 9; // file name length 9 "file2.bin"
        rom.write(reinterpret_cast<char*>(&typeLenFile2), 1);
        rom.write("file2.bin", 9);
        rom.write(reinterpret_cast<char*>(&endByte), 1);

        // For Banner and Hash
        rom.seekp(0x68);
        uint32_t bannerOffset = 0x8000;
        rom.write(reinterpret_cast<char*>(&bannerOffset), sizeof(bannerOffset));

        rom.seekp(0x8000);
        uint16_t bannerVersion = 1;
        uint16_t bannerCrc = 0;
        rom.write(reinterpret_cast<char*>(&bannerVersion), 2);
        rom.write(reinterpret_cast<char*>(&bannerCrc), 2);

        rom.close();

        std::filesystem::copy_file(testRomPath, testDataDir + "/game.nds", std::filesystem::copy_options::overwrite_existing);
    }

    void TearDown() override {
        std::filesystem::remove(testRomPath);
        std::filesystem::remove_all(testDataDir);
    }
};

TEST_F(VFSTest, ParseFNTAndRead) {
    VFS vfs(testDataDir);
    ASSERT_TRUE(vfs.LoadFromRom(testRomPath));

    EXPECT_EQ(vfs.GetPathById(0), "file1.bin");
    EXPECT_EQ(vfs.GetPathById(1), "dummy_dir/file2.bin");

    EXPECT_EQ(vfs.GetIdByPath("file1.bin"), 0);
    EXPECT_EQ(vfs.GetIdByPath("dummy_dir/file2.bin"), 1);

    auto data1 = vfs.ReadFileById(0);
    EXPECT_EQ(data1.size(), 5);
    EXPECT_EQ(std::string(data1.begin(), data1.end()), "DATA1");

    auto data2 = vfs.ReadFileById(1);
    EXPECT_EQ(data2.size(), 5);
    EXPECT_EQ(std::string(data2.begin(), data2.end()), "DATA2");
}

TEST_F(VFSTest, MountRomFs_Valid) {
    std::filesystem::copy_file(testRomPath, testDataDir + "/game.nds", std::filesystem::copy_options::overwrite_existing);
    VFS vfs(testDataDir);
    vfs.LoadFromRom(testRomPath); // Load first before Mount to map FNT
    EXPECT_NO_THROW(vfs.MountRomFs());
    EXPECT_EQ(vfs.GetPathById(0), "file1.bin");
}

TEST_F(VFSTest, MountRomFs_Invalid) {
    VFS vfs(testDataDir + "_nonexistent");
    EXPECT_THROW(vfs.MountRomFs(), std::runtime_error);
}

TEST_F(VFSTest, ReadBanner_Test) {
    std::filesystem::copy_file(testRomPath, testDataDir + "/game.nds", std::filesystem::copy_options::overwrite_existing);
    VFS vfs(testDataDir);
    EXPECT_NO_THROW(vfs.ReadBanner());
}

TEST_F(VFSTest, DecryptSecureArea_Test) {
    std::filesystem::copy_file(testRomPath, testDataDir + "/game.nds", std::filesystem::copy_options::overwrite_existing);
    VFS vfs(testDataDir);
    EXPECT_NO_THROW(vfs.DecryptSecureArea());
}

TEST_F(VFSTest, CalculateRomHash_Test) {
    std::filesystem::copy_file(testRomPath, testDataDir + "/game.nds", std::filesystem::copy_options::overwrite_existing);
    VFS vfs(testDataDir);
    EXPECT_NO_THROW(vfs.CalculateRomHash());
}
