import sys
import os
import struct

with open('recoded/arm9.bin', 'rb') as f:
    data = f.read()

def get_const(addr):
    offset = addr - 0x02000000
    if offset < 0 or offset >= len(data): return 0
    return struct.unpack('<I', data[offset:offset+4])[0]

print("Constants for 0x0203A85A:")
# 0x203a85a + 4 (pc) + 0x40 = 0x203a89e. Wait, pc alignment. In Thumb, pc = (addr + 4) & ~3.
pc = (0x0203A85A + 4) & ~3 # 0x203a85c
print(f"[pc+0x40] -> {hex(get_const(pc + 0x40))}")
print(f"[pc+0x44] -> {hex(get_const(pc + 0x44))}")
print(f"[pc+0x48] -> {hex(get_const(pc + 0x48))}")
print(f"[pc+0x4C] -> {hex(get_const(pc + 0x4c))}")

print("Constants for 0x0202ABA8:")
pc = (0x0202ABA8 + 4) & ~3
print(f"[pc+0x14] -> {hex(get_const(pc + 0x14))}")
print(f"[pc+0x0C] -> {hex(get_const(pc + 0x0c))}")
