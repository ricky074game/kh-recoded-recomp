import sys
import os
sys.path.append('scratch')
from capstone import *

def disasm(code, addr, is_thumb):
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB if is_thumb else CS_MODE_ARM)
    for i in md.disasm(code, addr):
        print(f"0x{i.address:x}:\t{i.mnemonic}\t{i.op_str}")

print("0x0203A85A (Thumb)")
disasm(bytes.fromhex("10 48 10 4B 01 60 10 49 41 60 10 49 81 60 10 49"), 0x0203A85A, True)

print("\n0x0202ABA8 (Thumb)")
disasm(bytes.fromhex("08 B5 05 49 00 20 48 80 88 80 08 80 03 49 30 22"), 0x0202ABA8, True)

print("\n0x0202ABFC (ARM)")
disasm(bytes.fromhex("B0 00 D2 E1 00 00 81 E1 22 06 20 E0 22 06 00 E0"), 0x0202ABFC, False)

print("\n0x02019D20 (ARM)")
disasm(bytes.fromhex("04 10 80 E5 14 10 80 E5 00 00 56 E3 07 00 00 0A"), 0x02019D20, False)
