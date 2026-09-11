#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <minhook/MinHook.h>
#include <lepus/utils/logger.hpp>

namespace lepus::hook {

struct MemoryPattern {
    std::string pattern_bytes;
    std::string mask;
};

class MemoryScanner {
public:
    static uintptr_t find_pattern(HMODULE module, const char* signature, const char* mask) {
        if (!module) return 0;

        MODULEINFO mod_info{};
        if (!GetModuleInformation(GetCurrentProcess(), module, &mod_info, sizeof(MODULEINFO))) {
            return 0;
        }

        uintptr_t base = reinterpret_cast<uintptr_t>(mod_info.lpBaseOfDll);
        size_t size = mod_info.SizeOfImage;
        size_t pattern_len = strlen(mask);

        for (size_t i = 0; i < size - pattern_len; ++i) {
            bool found = true;
            for (size_t j = 0; j < pattern_len; ++j) {
                if (mask[j] != '?' && *reinterpret_cast<const uint8_t*>(base + i + j) != static_cast<uint8_t>(signature[j])) {
                    found = false;
                    break;
                }
            }
            if (found) {
                return base + i;
            }
        }
        return 0;
    }

    static uintptr_t get_export_address(const char* module_name, const char* proc_name) {
        HMODULE h_mod = GetModuleHandleA(module_name);
        if (!h_mod) {
            h_mod = LoadLibraryA(module_name);
        }
        if (!h_mod) return 0;
        return reinterpret_cast<uintptr_t>(GetProcAddress(h_mod, proc_name));
    }
};

class HookManager {
public:
    static HookManager& instance() {
        static HookManager inst;
        return inst;
    }

    bool initialize() {
        if (initialized_) return true;
        MH_STATUS status = MH_Initialize();
        if (status == MH_OK) {
            initialized_ = true;
            LEPUS_LOG_INFO("MinHook initialized successfully");
            return true;
        }
        LEPUS_LOG_ERROR("MinHook initialization failed: code {}", (int)status);
        return false;
    }

    bool create_hook(void* target, void* detour, void** original) {
        if (!initialized_ && !initialize()) return false;
        MH_STATUS status = MH_CreateHook(target, detour, original);
        if (status == MH_OK) {
            LEPUS_LOG_INFO("Hook created successfully at target {:p}", target);
            return true;
        }
        LEPUS_LOG_ERROR("Failed to create hook at {:p}: code {}", target, (int)status);
        return false;
    }

    bool enable_hook(void* target) {
        MH_STATUS status = MH_EnableHook(target);
        if (status == MH_OK) {
            LEPUS_LOG_INFO("Hook enabled at {:p}", target);
            return true;
        }
        LEPUS_LOG_ERROR("Failed to enable hook at {:p}: code {}", target, (int)status);
        return false;
    }

    bool disable_hook(void* target) {
        return MH_DisableHook(target) == MH_OK;
    }

    void shutdown() {
        if (initialized_) {
            MH_Uninitialize();
            initialized_ = false;
            LEPUS_LOG_INFO("MinHook uninitialized");
        }
    }

private:
    HookManager() : initialized_(false) {}
    ~HookManager() { shutdown(); }
    bool initialized_{false};
};

} // namespace lepus::hook
