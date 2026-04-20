#include "memory_map.h"
#include <iostream>

void RegisterAllLiftedFunctions(NDSMemory* mem) {
    (void)mem;
    std::cout << "[Runtime] Generated lifted registration not found; runtime started without lifted blocks." << std::endl;
}
