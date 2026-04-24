#include <gtest/gtest.h>

#include "cpu_context.h"
#include "hw_bios.h"
#include "memory_map.h"

TEST(HWBiosTest, SWIDispatchDoesNotThrow) {
    EXPECT_NO_THROW(HwBios::HandleARM9_SWI(0x11));
    EXPECT_NO_THROW(HwBios::HandleARM9_SWI(0x09));
    EXPECT_NO_THROW(HwBios::HandleARM9_SWI(0x0D));
    EXPECT_NO_THROW(HwBios::HandleARM7_SWI(0x06)); // Halt test
}

TEST(HWBiosTest, SWSI11LZDecompressUsesR0R1MemoryPointers) {
    NDSMemory mem;
    CPU_Context ctx;
    ctx.mem = &mem;

    const uint32_t src = 0x02001000;
    const uint32_t dst = 0x02002000;

    const uint8_t compressed[] = {
        0x10, 0x06, 0x00, 0x00,
        0x10,
        'A', 'B', 'C',
        0x00, 0x02,
    };

    for (size_t i = 0; i < sizeof(compressed); ++i) {
        mem.Write8(src + static_cast<uint32_t>(i), compressed[i]);
    }

    ctx.r[0] = src;
    ctx.r[1] = dst;
    HwBios::SetActiveContext(&ctx);

    HwBios::HandleARM9_SWI(0x11);

    EXPECT_EQ(mem.Read8(dst + 0), 'A');
    EXPECT_EQ(mem.Read8(dst + 1), 'B');
    EXPECT_EQ(mem.Read8(dst + 2), 'C');
    EXPECT_EQ(mem.Read8(dst + 3), 'A');
    EXPECT_EQ(mem.Read8(dst + 4), 'B');
    EXPECT_EQ(mem.Read8(dst + 5), 'C');

    HwBios::SetActiveContext(nullptr);
}

TEST(HWBiosTest, SWI12WritesUsingHalfwordAccess) {
    NDSMemory mem;
    CPU_Context ctx;
    ctx.mem = &mem;

    const uint32_t src = 0x02003000;
    const uint32_t dst = 0x02004000;

    const uint8_t compressed[] = {
        0x10, 0x06, 0x00, 0x00,
        0x10,
        'A', 'B', 'C',
        0x00, 0x02,
    };

    for (size_t i = 0; i < sizeof(compressed); ++i) {
        mem.Write8(src + static_cast<uint32_t>(i), compressed[i]);
    }

    ctx.r[0] = src;
    ctx.r[1] = dst;
    HwBios::SetActiveContext(&ctx);

    HwBios::HandleARM9_SWI(0x12);

    EXPECT_EQ(mem.Read16(dst + 0), static_cast<uint16_t>('A' | ('B' << 8)));
    EXPECT_EQ(mem.Read16(dst + 2), static_cast<uint16_t>('C' | ('A' << 8)));
    EXPECT_EQ(mem.Read16(dst + 4), static_cast<uint16_t>('B' | ('C' << 8)));

    HwBios::SetActiveContext(nullptr);
}

TEST(HWBiosTest, SWI10BitUnPackExpands2bppTo8bpp) {
    NDSMemory mem;
    CPU_Context ctx;
    ctx.mem = &mem;

    const uint32_t src = 0x02005000;
    const uint32_t dst = 0x02006000;
    const uint32_t info = 0x02007000;

    // Packed 2bpp units in LSB-first order: 0,1,2,3.
    mem.Write8(src, 0xE4);

    mem.Write16(info + 0, 1);   // source length in bytes
    mem.Write8(info + 2, 2);    // source unit width
    mem.Write8(info + 3, 8);    // destination unit width
    mem.Write32(info + 4, 0);   // no offset

    ctx.r[0] = src;
    ctx.r[1] = dst;
    ctx.r[2] = info;
    HwBios::SetActiveContext(&ctx);

    HwBios::HandleARM9_SWI(0x10);

    EXPECT_EQ(mem.Read8(dst + 0), 0u);
    EXPECT_EQ(mem.Read8(dst + 1), 1u);
    EXPECT_EQ(mem.Read8(dst + 2), 2u);
    EXPECT_EQ(mem.Read8(dst + 3), 3u);

    HwBios::SetActiveContext(nullptr);
}
