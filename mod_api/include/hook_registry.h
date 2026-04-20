#pragma once
#include <cstdint>

namespace ModAPI {
    void Init();
    void RegisterHook(uint32_t address, void* callback);
    void RegisterPostHook(uint32_t address, void* callback);
    void ExecutePreHook(uint32_t address);
    void ExecutePostHook(uint32_t address);
    bool HasHook(uint32_t address);
    void ClearHooks();
    void LoadModDLL(const char* path);
}
