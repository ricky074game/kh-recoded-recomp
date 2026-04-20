// ============================================================================
// mod_loader.cpp — Dynamic Mod Loading
//
// Loads mod shared libraries (.so on Linux, .dll on Windows) at runtime.
// Each mod must export a `RecompModInit(HookRegistry&)` function.
// ============================================================================

#include "recomp_api.h"
#include <iostream>
#include <filesystem>
#include <vector>
#include <string>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

// Platform-agnostic dynamic library loading
static void* LoadDynamicLibrary(const std::string& path) {
#ifdef _WIN32
    return LoadLibraryA(path.c_str());
#else
    return dlopen(path.c_str(), RTLD_NOW);
#endif
}

static void* GetSymbol(void* handle, const std::string& name) {
#ifdef _WIN32
    return (void*)GetProcAddress((HMODULE)handle, name.c_str());
#else
    return dlsym(handle, name.c_str());
#endif
}

static void CloseDynamicLibrary(void* handle) {
#ifdef _WIN32
    FreeLibrary((HMODULE)handle);
#else
    dlclose(handle);
#endif
}

static std::string GetLastDLError() {
#ifdef _WIN32
    DWORD err = GetLastError();
    char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, 0, buf, sizeof(buf), nullptr);
    return std::string(buf);
#else
    const char* err = dlerror();
    return err ? std::string(err) : "Unknown error";
#endif
}

// ============================================================================
// Public API
// ============================================================================

// Load a single mod from a shared library path.
bool LoadMod(const std::string& path) {
    std::cerr << "[ModLoader] Loading: " << path << std::endl;

    void* handle = LoadDynamicLibrary(path);
    if (!handle) {
        std::cerr << "[ModLoader] Failed to load: " << GetLastDLError() << std::endl;
        return false;
    }

    // Look for the entry point
    auto entry = reinterpret_cast<ModEntryFn>(GetSymbol(handle, "RecompModInit"));
    if (!entry) {
        std::cerr << "[ModLoader] No RecompModInit found in: " << path << std::endl;
        CloseDynamicLibrary(handle);
        return false;
    }

    // Register mod info
    ModInfo info;
    info.name = std::filesystem::path(path).stem().string();
    info.version = "1.0";
    info.author = "Unknown";
    info.handle = handle;

    HookRegistry::Get().RegisterMod(info);

    // Call the entry point
    entry(HookRegistry::Get());

    std::cerr << "[ModLoader] Loaded: " << info.name << std::endl;
    return true;
}

// Scan a directory for all .so/.dll files and load them as mods.
int LoadModsFromDirectory(const std::string& dir_path) {
    int count = 0;

    if (!std::filesystem::exists(dir_path)) {
        std::cerr << "[ModLoader] Mod directory does not exist: " << dir_path << std::endl;
        return 0;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
#ifdef _WIN32
        if (ext != ".dll") continue;
#else
        if (ext != ".so") continue;
#endif

        if (LoadMod(entry.path().string())) {
            count++;
        }
    }

    std::cerr << "[ModLoader] Loaded " << count << " mod(s) from: " << dir_path << std::endl;
    return count;
}
