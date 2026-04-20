#include <gtest/gtest.h>
#include "memory_map.h"
#include <stdexcept>

// ============================================================================
// Memory Map Tests — All regions, alignment, boundaries, and endianness
// ============================================================================

class MemoryMapTest : public ::testing::Test {
protected:
    NDSMemory mem;
};

// ===================== 8-bit Access =====================

TEST_F(MemoryMapTest, MainRAM_ReadWrite8) {
    mem.Write8(0x02001000, 0xAB);
    EXPECT_EQ(mem.Read8(0x02001000), 0xAB);
}

TEST_F(MemoryMapTest, WRAM_ReadWrite8) {
    mem.Write8(0x037F8100, 0xCD);
    EXPECT_EQ(mem.Read8(0x037F8100), 0xCD);
}

TEST_F(MemoryMapTest, DTCM_ReadWrite8) {
    mem.Write8(0x027E0100, 0xEF);
    EXPECT_EQ(mem.Read8(0x027E0100), 0xEF);
}

TEST_F(MemoryMapTest, ITCM_ReadWrite8) {
    mem.Write8(0x01000100, 0x42);
    EXPECT_EQ(mem.Read8(0x01000100), 0x42);
}

TEST_F(MemoryMapTest, PaletteRAM_ReadWrite8) {
    mem.Write8(0x05000000, 0x77);
    EXPECT_EQ(mem.Read8(0x05000000), 0x77);
}

TEST_F(MemoryMapTest, VRAM_ReadWrite8) {
    mem.Write8(0x06000000, 0x99);
    EXPECT_EQ(mem.Read8(0x06000000), 0x99);
}

TEST_F(MemoryMapTest, OAM_ReadWrite8) {
    mem.Write8(0x07000000, 0xBB);
    EXPECT_EQ(mem.Read8(0x07000000), 0xBB);
}

// ===================== 16-bit Access =====================

TEST_F(MemoryMapTest, MainRAM_ReadWrite16) {
    mem.Write16(0x02001000, 0xABCD);
    EXPECT_EQ(mem.Read16(0x02001000), 0xABCD);
}

TEST_F(MemoryMapTest, UnalignedAccess16_Throws) {
    EXPECT_THROW(mem.Read16(0x02001001), std::runtime_error);
    EXPECT_THROW(mem.Write16(0x02001001, 0xABCD), std::runtime_error);
}

TEST_F(MemoryMapTest, PaletteRAM_ReadWrite16) {
    mem.Write16(0x05000100, 0x1234);
    EXPECT_EQ(mem.Read16(0x05000100), 0x1234);
}

TEST_F(MemoryMapTest, OAM_ReadWrite16) {
    mem.Write16(0x07000010, 0xFEDC);
    EXPECT_EQ(mem.Read16(0x07000010), 0xFEDC);
}

// ===================== 32-bit Access =====================

TEST_F(MemoryMapTest, MainRAM_ReadWrite32) {
    mem.Write32(0x02001000, 0xDEADBEEF);
    EXPECT_EQ(mem.Read32(0x02001000), 0xDEADBEEF);
}

TEST_F(MemoryMapTest, UnalignedAccess32_Throws) {
    EXPECT_THROW(mem.Read32(0x02001001), std::runtime_error);
    EXPECT_THROW(mem.Read32(0x02001002), std::runtime_error);
    EXPECT_THROW(mem.Read32(0x02001003), std::runtime_error);
    EXPECT_THROW(mem.Write32(0x02001001, 0), std::runtime_error);
    EXPECT_THROW(mem.Write32(0x02001002, 0), std::runtime_error);
    EXPECT_THROW(mem.Write32(0x02001003, 0), std::runtime_error);
}

TEST_F(MemoryMapTest, VRAM_ReadWrite32) {
    mem.Write32(0x06000000, 0x12345678);
    EXPECT_EQ(mem.Read32(0x06000000), 0x12345678);
}

TEST_F(MemoryMapTest, OAM_ReadWrite32) {
    mem.Write32(0x07000000, 0xABCDEF01);
    EXPECT_EQ(mem.Read32(0x07000000), 0xABCDEF01);
}

// ===================== Boundary Tests =====================

TEST_F(MemoryMapTest, MainRAM_FirstByte) {
    mem.Write8(0x02000000, 0x01);
    EXPECT_EQ(mem.Read8(0x02000000), 0x01);
}

TEST_F(MemoryMapTest, MainRAM_LastByte) {
    mem.Write8(0x023FFFFF, 0xFF);
    EXPECT_EQ(mem.Read8(0x023FFFFF), 0xFF);
}

TEST_F(MemoryMapTest, MainRAM_LastWord) {
    mem.Write32(0x023FFFFC, 0xCAFEBABE);
    EXPECT_EQ(mem.Read32(0x023FFFFC), 0xCAFEBABE);
}

TEST_F(MemoryMapTest, ITCM_LastByte) {
    mem.Write8(0x01007FFF, 0xAA);
    EXPECT_EQ(mem.Read8(0x01007FFF), 0xAA);
}

TEST_F(MemoryMapTest, OAM_LastWord) {
    mem.Write32(0x070007FC, 0x11223344);
    EXPECT_EQ(mem.Read32(0x070007FC), 0x11223344);
}

TEST_F(MemoryMapTest, PaletteRAM_LastWord) {
    mem.Write32(0x050007FC, 0x55667788);
    EXPECT_EQ(mem.Read32(0x050007FC), 0x55667788);
}

// ===================== Endianness =====================

TEST_F(MemoryMapTest, LittleEndian_Write32_Read8) {
    // NDS is little-endian: 0xDEADBEEF at byte level = EF BE AD DE
    mem.Write32(0x02000100, 0xDEADBEEF);
    EXPECT_EQ(mem.Read8(0x02000100), 0xEF);
    EXPECT_EQ(mem.Read8(0x02000101), 0xBE);
    EXPECT_EQ(mem.Read8(0x02000102), 0xAD);
    EXPECT_EQ(mem.Read8(0x02000103), 0xDE);
}

TEST_F(MemoryMapTest, LittleEndian_Write16_Read8) {
    mem.Write16(0x02000200, 0xABCD);
    EXPECT_EQ(mem.Read8(0x02000200), 0xCD);
    EXPECT_EQ(mem.Read8(0x02000201), 0xAB);
}

TEST_F(MemoryMapTest, LittleEndian_Write8s_Read32) {
    mem.Write8(0x02000300, 0x78);
    mem.Write8(0x02000301, 0x56);
    mem.Write8(0x02000302, 0x34);
    mem.Write8(0x02000303, 0x12);
    EXPECT_EQ(mem.Read32(0x02000300), 0x12345678);
}

// ===================== Hardware IO Stubs =====================

TEST_F(MemoryMapTest, HardwareRead_UnhandledReturnsZero) {
    EXPECT_EQ(mem.Read32(0x04000000), 0u);
}

TEST_F(MemoryMapTest, HardwareWrite_DoesNotCrash) {
    EXPECT_NO_THROW(mem.Write32(0x04000000, 0x12345678));
}

// ===================== Unmapped Memory =====================

TEST_F(MemoryMapTest, UnmappedRead_Throws) {
    EXPECT_THROW(mem.Read32(0x08000000), std::runtime_error);
}

TEST_F(MemoryMapTest, UnmappedWrite_Throws) {
    EXPECT_THROW(mem.Write32(0x08000000, 0), std::runtime_error);
}

// ===================== Direct Buffer Access =====================

TEST_F(MemoryMapTest, DirectMainRAMAccess) {
    uint8_t* ram = mem.GetMainRAM();
    EXPECT_NE(ram, nullptr);
    EXPECT_EQ(mem.GetMainRAMSize(), 4u * 1024 * 1024);
}

TEST_F(MemoryMapTest, DirectVRAMAccess) {
    uint8_t* v = mem.GetVRAM();
    EXPECT_NE(v, nullptr);
}

TEST_F(MemoryMapTest, DirectOAMAccess) {
    uint8_t* o = mem.GetOAM();
    EXPECT_NE(o, nullptr);
}
