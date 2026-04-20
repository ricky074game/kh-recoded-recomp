# Imports an address_map.json file from the lifter to mark basic blocks / instructions in Ghidra.
# @author Recompilation Team
# @category KingdomHearts
# @keybinding
# @menupath
# @toolbar

import json
from ghidra.program.model.address import AddressFactory
from ghidra.program.model.symbol import SourceType

def run():
    f = askFile("Select address_map.json", "Import")
    if not f:
        return

    filepath = f.getAbsolutePath()
    with open(filepath, 'r') as json_file:
        data = json.load(json_file)

    fm = currentProgram.getFunctionManager()
    symTable = currentProgram.getSymbolTable()
    addrFactory = currentProgram.getAddressFactory()

    monitor.setMessage("Importing DS_ADDR references...")
    monitor.initialize(len(data))
    count = 0

    for hex_addr_str, line_num in data.items():
        if monitor.isCancelled():
            break

        addr_val = int(hex_addr_str, 16)
        ghidra_addr = addrFactory.getDefaultAddressSpace().getAddress(addr_val)

        # Create a label indicating the compiled C++ line number for cross-referencing
        label_str = "CXX_LINE_{}".format(line_num)
        
        try:
            symTable.createLabel(ghidra_addr, label_str, SourceType.USER_DEFINED)
            # You could also trigger analysis or function creation here 
            # if the block represents a function boundary.
        except Exception as e:
            pass # Label already exists or invalid address

        count += 1
        monitor.setProgress(count)

    print("Successfully mapped {} ROM addresses to C++ line labels.".format(count))

if __name__ == "__main__":
    run()
