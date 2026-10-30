#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <cstdint>

#ifndef _WIN32
#include <dlfcn.h>
#else
#include <windows.h>
#endif

class DpiRegistry {
private:
    void* handle = nullptr;
    std::unordered_map<std::string, void*> symbol_cache;
    bool attempted_load = false;

public:
    DpiRegistry() = default;
    ~DpiRegistry() {
        if (handle) {
#ifndef _WIN32
            dlclose(handle);
#else
            FreeLibrary(static_cast<HMODULE>(handle));
#endif
            handle = nullptr;
        }
    }

    bool ensure_loaded(const std::vector<std::string>& search_paths = {}) {
        if (handle) return true;

        std::vector<std::string> candidates = {
            "./c_model.so",
            "c_model.so",
            "./c_model.dll",
            "c_model.dll"
        };
        for (const auto& sp : search_paths) {
            if (!sp.empty()) {
                candidates.push_back(sp + "/c_model.so");
                candidates.push_back(sp + "/c_model.dll");
                candidates.push_back(sp);
            }
        }

        for (const auto& path : candidates) {
#ifndef _WIN32
            void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
            if (h) {
                handle = h;
                return true;
            }
#else
            HMODULE h = LoadLibraryA(path.c_str());
            if (h) {
                handle = static_cast<void*>(h);
                return true;
            }
#endif
        }
        return false;
    }

    void* get_symbol(const std::string& symbol_name, const std::vector<std::string>& search_paths = {}) {
        auto it = symbol_cache.find(symbol_name);
        if (it != symbol_cache.end()) return it->second;

        if (!handle) {
            ensure_loaded(search_paths);
        }
        if (!handle) {
            return nullptr;
        }

        void* sym = nullptr;
#ifndef _WIN32
        sym = dlsym(handle, symbol_name.c_str());
#else
        sym = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), symbol_name.c_str()));
#endif
        if (sym) {
            symbol_cache[symbol_name] = sym;
        }
        return sym;
    }
};
