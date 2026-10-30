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

typedef void* svScope;

struct SvScopeContext {
    std::string scope_name;
    void* user_data = nullptr;
};

class DpiRegistry {
private:
    void* handle = nullptr;
    std::unordered_map<std::string, void*> symbol_cache;
    bool attempted_load = false;

    static inline SvScopeContext* active_scope_ptr = nullptr;
    static inline void* active_engine_ptr = nullptr;
    static inline std::unordered_map<std::string, std::unique_ptr<SvScopeContext>> scope_contexts;

public:
    DpiRegistry() = default;

    static SvScopeContext* get_or_create_scope(const std::string& name) {
        auto it = scope_contexts.find(name);
        if (it != scope_contexts.end()) return it->second.get();
        auto ctx = std::make_unique<SvScopeContext>();
        ctx->scope_name = name;
        SvScopeContext* ptr = ctx.get();
        scope_contexts[name] = std::move(ctx);
        return ptr;
    }

    static svScope get_active_scope() {
        return static_cast<svScope>(active_scope_ptr);
    }

    static svScope set_active_scope(svScope s) {
        svScope old = static_cast<svScope>(active_scope_ptr);
        active_scope_ptr = static_cast<SvScopeContext*>(s);
        return old;
    }

    static svScope set_active_scope_by_name(const std::string& name) {
        svScope old = static_cast<svScope>(active_scope_ptr);
        active_scope_ptr = get_or_create_scope(name);
        return old;
    }

    static std::string get_active_scope_name() {
        return active_scope_ptr ? active_scope_ptr->scope_name : "";
    }

    static void set_active_engine(void* eng) {
        active_engine_ptr = eng;
    }

    static void* get_active_engine() {
        return active_engine_ptr;
    }
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
