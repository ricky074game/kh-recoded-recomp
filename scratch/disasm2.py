import sys
import os
sys.path.append('scratch')
from capstone import *

def disasm(code, addr, is_thumb):
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB if is_thumb else CS_MODE_ARM)
    for i in md.disasm(code, addr):
        print(f"0x{i.address:x}:\t{i.mnemonic}\t{i.op_str}")

print("\n0x02019D20 (ARM)")
disasm(bytes.fromhex("04 10 80 E5 14 10 80 E5 00 00 56 E3 07 00 00 0A 00 40 8D E2 04 00 A0 E1 94 B3 FF EB 00 00 50 E3 FB FF FF 1A 04 00 A0 E1 06 10 A0 E1 20 AF FF EB 00 00 55 E3 03 00 00 0A 05 00 A0 E1 97 B3 FF EB 00 00 50 E3 FB FF FF 1A 18 10 9F E5 01 00 A0 E3 00 00 81 E5 02 00 A0 E3 08 00 01 E5 40 D0 8D E2 70 80 BD E8 40 04 00 04 48 04 00 04"), 0x02019D20, False)

print("\n0x01FFA1F4 (ARM)")
disasm(bytes.fromhex("04 10 9F E5 00 00 81 E5 1E FF 2F E1 A0 FE FF 01 78 40 2D E9 04 D0 4D E2 01 40 A0 E1 00 50 A0 E1 01 0C 54 E3 28 00 00 2A 40 11 9F E5 00 20 91 E5 00 00 52 E3 12 00 00 0A 34 61 9F E5 00 C0 96 E5 00 30 9C E5 24 21 83 E0 C0 00 52 E3 09 00 00 8A 04 10 8C E2 04 20 A0 E1 03 11 81 E0 4D F9 FF EB 00 10 96 E5 04 D0 8D E2 00 00 91 E5 24 01 80 E0 00 00 81 E5 78 80 BD E8 00 00 91 E5 00 00 50 E3"), 0x01FFA1F4, False)
