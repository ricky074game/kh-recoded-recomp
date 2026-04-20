import gdb
import json
import os

class LookupDSAddr(gdb.Command):
    """
    Look up the NDS ROM address corresponding to a C++ line number in the lifter output.
    Usage: ds-lookup <cpp_file_path> <line_number>
           ds-lookup <line_number> (uses the current frame's source file)
    """

    def __init__(self):
        super(LookupDSAddr, self).__init__("ds-lookup", gdb.COMMAND_USER)
        self.map_cache = {}

    def _load_map(self, map_path):
        if map_path in self.map_cache:
            return self.map_cache[map_path]
        try:
            with open(map_path, 'r') as f:
                data = json.load(f)
            # Create reverse mapping: line -> addr
            # Since multiple addresses might map to the same line (if compressed on one line),
            # we just keep the first one or closest one.
            reverse_map = {}
            for addr, line in data.items():
                if line not in reverse_map:
                    reverse_map[line] = addr
            self.map_cache[map_path] = reverse_map
            return reverse_map
        except Exception as e:
            print(f"Error loading {map_path}: {e}")
            return None

    def invoke(self, arg, from_tty):
        args = gdb.string_to_argv(arg)
        if len(args) == 0:
            print("Usage: ds-lookup [<file.cpp>] <line_number>")
            return

        cpp_file = None
        line_num = -1

        if len(args) == 1:
            try:
                line_num = int(args[0])
                frame = gdb.selected_frame()
                sal = frame.find_sal()
                if not sal.symtab:
                    print("Error: Could not determine current source file.")
                    return
                cpp_file = sal.symtab.fullname()
            except ValueError:
                print("Error: Line number must be an integer.")
                return
        elif len(args) >= 2:
            cpp_file = args[0]
            try:
                line_num = int(args[1])
            except ValueError:
                print("Error: Line number must be an integer.")
                return

        map_path = f"{cpp_file}.map.json"
        if not os.path.exists(map_path):
            print(f"Error: Map file not found at {map_path}")
            return

        reverse_map = self._load_map(map_path)
        if not reverse_map:
            return

        # Find closest line mapping <= requested line
        closest_line = -1
        for l in sorted(reverse_map.keys()):
            if l <= line_num:
                closest_line = l
            else:
                break

        if closest_line != -1:
            addr = reverse_map[closest_line]
            print(f"C++ Line {line_num} in {os.path.basename(cpp_file)} -> ROM Address {addr}")
        else:
            print(f"No ROM address mapping found strictly preceding line {line_num}.")

LookupDSAddr()
print("KH Re:coded DBG: 'ds-lookup' command loaded.")
