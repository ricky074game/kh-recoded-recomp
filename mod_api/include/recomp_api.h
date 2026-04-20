#pragma once

// ============================================================================
// recomp_api.h — Kingdom Hearts Re:coded Modding API
//
// Provides a hook registry that allows external modules (DLLs/SOs) to inject
// Pre-Hooks and Post-Hooks around any lifted ARM function block. This enables
// mods to intercept game logic, modify register state, replace function
// behavior, or add new features without modifying generated code.
//
// Usage:
//   #include "recomp_api.h"
//
//   // In a mod's initialization:
//   HookRegistry::Get().RegisterPreHook(0x02004A10, [](CPU_Context* ctx) {
//       ctx->r[0] = 999; // Override parameter
//   });
// ============================================================================

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <string>

// Forward declaration
struct CPU_Context;

// ---- Hook Callback Types ----
// Pre-hooks execute before the block. Return false to skip the original block.
using PreHookFn  = std::function<bool(CPU_Context*)>;

// Post-hooks execute after the block. Can modify return values in registers.
using PostHookFn = std::function<void(CPU_Context*)>;

// ---- Mod Info Structure ----
struct ModInfo {
    std::string name;
    std::string version;
    std::string author;
    void* handle = nullptr; // dlopen / LoadLibrary handle
};

// ---- Hook Registry (Singleton) ----
class HookRegistry {
public:
    // Get the global singleton instance.
    static HookRegistry& Get() {
        static HookRegistry instance;
        return instance;
    }

    // Register a pre-hook at a specific ROM address.
    void RegisterPreHook(uint32_t address, PreHookFn hook) {
        std::lock_guard<std::mutex> lock(mutex_);
        pre_hooks_[address].push_back(std::move(hook));
    }

    // Register a post-hook at a specific ROM address.
    void RegisterPostHook(uint32_t address, PostHookFn hook) {
        std::lock_guard<std::mutex> lock(mutex_);
        post_hooks_[address].push_back(std::move(hook));
    }

    // Execute all pre-hooks for an address. Returns false if any hook
    // requests that the original block be skipped.
    bool RunPreHooks(uint32_t address, CPU_Context* ctx) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pre_hooks_.find(address);
        if (it == pre_hooks_.end()) return true;

        for (auto& hook : it->second) {
            if (!hook(ctx)) return false; // Skip original
        }
        return true; // Execute original
    }

    // Execute all post-hooks for an address.
    void RunPostHooks(uint32_t address, CPU_Context* ctx) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = post_hooks_.find(address);
        if (it == post_hooks_.end()) return;

        for (auto& hook : it->second) {
            hook(ctx);
        }
    }

    // Check if any hooks are registered for an address.
    bool HasHooks(uint32_t address) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pre_hooks_.count(address) > 0 || post_hooks_.count(address) > 0;
    }

    // Remove all hooks for an address.
    void ClearHooks(uint32_t address) {
        std::lock_guard<std::mutex> lock(mutex_);
        pre_hooks_.erase(address);
        post_hooks_.erase(address);
    }

    // Remove all hooks.
    void ClearAllHooks() {
        std::lock_guard<std::mutex> lock(mutex_);
        pre_hooks_.clear();
        post_hooks_.clear();
    }

    // Loaded mod tracking
    void RegisterMod(const ModInfo& info) {
        std::lock_guard<std::mutex> lock(mutex_);
        loaded_mods_.push_back(info);
    }

    const std::vector<ModInfo>& GetLoadedMods() const {
        return loaded_mods_;
    }

private:
    HookRegistry() = default;
    HookRegistry(const HookRegistry&) = delete;
    HookRegistry& operator=(const HookRegistry&) = delete;

    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, std::vector<PreHookFn>>  pre_hooks_;
    std::unordered_map<uint32_t, std::vector<PostHookFn>> post_hooks_;
    std::vector<ModInfo> loaded_mods_;
};

// ---- Mod Entry Point Typedef ----
// Every mod DLL/SO must export this function.
// Called once when the mod is loaded.
using ModEntryFn = void(*)(HookRegistry&);

#define RECOMP_MOD_EXPORT extern "C"
#define RECOMP_MOD_ENTRY  RECOMP_MOD_EXPORT void RecompModInit(HookRegistry& registry)
