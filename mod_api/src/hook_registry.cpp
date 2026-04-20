#include "hook_registry.h"

#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace ModAPI {

namespace {
using HookFn = void (*)();

struct HookSet {
    HookFn pre = nullptr;
    HookFn post = nullptr;
};

std::unordered_map<uint32_t, HookSet> active_hooks;
std::vector<void*> module_handles;
std::mutex hook_mutex;
std::once_flag unload_registration_once;

HookFn ToHook(void* callback) {
    return reinterpret_cast<HookFn>(callback);
}

void UnloadModules() {
    for (void* handle : module_handles) {
        if (handle == nullptr) {
            continue;
        }

#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
    }
    module_handles.clear();
}
}

void Init() {
    std::lock_guard<std::mutex> lock(hook_mutex);
    active_hooks.clear();

    std::call_once(unload_registration_once, []() {
        std::atexit(UnloadModules);
    });
}

void RegisterHook(uint32_t address, void* callback) {
    std::lock_guard<std::mutex> lock(hook_mutex);
    active_hooks[address].pre = ToHook(callback);
}

void RegisterPostHook(uint32_t address, void* callback) {
    std::lock_guard<std::mutex> lock(hook_mutex);
    active_hooks[address].post = ToHook(callback);
}

void ExecutePreHook(uint32_t address) {
    HookFn hook = nullptr;
    {
        std::lock_guard<std::mutex> lock(hook_mutex);
        const auto it = active_hooks.find(address);
        if (it != active_hooks.end()) {
            hook = it->second.pre;
        }
    }

    if (hook != nullptr) {
        hook();
    }
}

void ExecutePostHook(uint32_t address) {
    HookFn hook = nullptr;
    {
        std::lock_guard<std::mutex> lock(hook_mutex);
        const auto it = active_hooks.find(address);
        if (it != active_hooks.end()) {
            hook = it->second.post;
        }
    }

    if (hook != nullptr) {
        hook();
    }
}

bool HasHook(uint32_t address) {
    std::lock_guard<std::mutex> lock(hook_mutex);
    const auto it = active_hooks.find(address);
    return it != active_hooks.end() && (it->second.pre != nullptr || it->second.post != nullptr);
}

void ClearHooks() {
    std::lock_guard<std::mutex> lock(hook_mutex);
    active_hooks.clear();
}

void LoadModDLL(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return;
    }

#if defined(__linux__) || defined(__APPLE__)
    void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        return;
    }

    auto* init = reinterpret_cast<HookFn>(dlsym(handle, "RecompModInit"));
    if (init == nullptr) {
        init = reinterpret_cast<HookFn>(dlsym(handle, "ModInit"));
    }

    {
        std::lock_guard<std::mutex> lock(hook_mutex);
        module_handles.push_back(handle);
    }

    if (init != nullptr) {
        init();
    }
#elif defined(_WIN32)
    HMODULE handle = LoadLibraryA(path);
    if (handle == nullptr) {
        return;
    }

    auto* init = reinterpret_cast<HookFn>(GetProcAddress(handle, "RecompModInit"));
    if (init == nullptr) {
        init = reinterpret_cast<HookFn>(GetProcAddress(handle, "ModInit"));
    }

    {
        std::lock_guard<std::mutex> lock(hook_mutex);
        module_handles.push_back(handle);
    }

    if (init != nullptr) {
        init();
    }
#else
    (void)path;
#endif
}

}
