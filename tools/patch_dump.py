import sys
with open('runtime/src/hw/hw_overlay.cpp', 'r') as f:
    content = f.read()

target = """        if (verbose_unmapped) {
            std::cerr << "OverlayManager: FATAL: Dynamic branch to unmapped/inactive memory 0x" 
                      << std::hex << exec_addr << "\\n";
            printf("DEBUG: Failed to dispatch 0x%08X (Target 0x%08X)\\n", exec_addr, exec_addr);
            printf("Is it in static_funcs? %s\\n", static_funcs.find(exec_addr) != static_funcs.end() ? "Yes" : "No");
            printf("Total static_funcs: %zu\\n", static_funcs.size());
        }"""

replacement = """        if (verbose_unmapped) {
            std::cerr << "OverlayManager: FATAL: Dynamic branch to unmapped/inactive memory 0x" 
                      << std::hex << exec_addr << "\\n";
            printf("DEBUG: Failed to dispatch 0x%08X (Target 0x%08X)\\n", exec_addr, exec_addr);
            printf("Is it in static_funcs? %s\\n", static_funcs.find(exec_addr) != static_funcs.end() ? "Yes" : "No");
            printf("Total static_funcs: %zu\\n", static_funcs.size());
            printf("DUMP 0x%08X: ", exec_addr);
            for(int i=0; i<16; i++) {
                try {
                    printf("%02X ", ctx->mem->Read8(exec_addr + i));
                } catch(...) {
                    printf("?? ");
                }
            }
            printf("\\n");
        }"""

content = content.replace(target, replacement)

with open('runtime/src/hw/hw_overlay.cpp', 'w') as f:
    f.write(content)
