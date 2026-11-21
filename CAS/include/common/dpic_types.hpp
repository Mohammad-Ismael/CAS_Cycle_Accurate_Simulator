#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <cstdint>
#include <memory>
#include <cstring>
#include <cctype>

#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#else
#include <windows.h>
#endif

#include "../dpi/svdpi.h"

struct MemoryArray;
struct DynamicArrayObject;
struct QueueObject;
struct LocalVar;
struct LogicVal;

struct SvOpenArrayDim {
    int left = 0;
    int right = 0;
    int low = 0;
    int high = 0;
    int increment = 1;
    int size = 0;
};

struct SvOpenArray {
    int size = 0;
    int elem_width = 32;
    size_t elem_stride = 4;
    bool is_2state = true;
    bool is_logic = false;
    bool is_real = false;
    std::vector<SvOpenArrayDim> dims;
    std::vector<uint8_t> buffer;

    // Backing references for writeback
    MemoryArray* source_mem = nullptr;
    DynamicArrayObject* source_dyn = nullptr;
    QueueObject* source_queue = nullptr;
    LocalVar* source_local = nullptr;
    std::vector<int64_t> source_indices;

    static inline int compute_dim_offset(const SvOpenArrayDim& dim, int idx) {
        if (dim.increment == -1 && dim.left == 0) return idx;
        return (dim.left >= dim.right) ? (dim.left - idx) : (idx - dim.left);
    }

    void* get_elem_ptr(int indx1) {
        if (dims.empty()) return nullptr;
        int off = compute_dim_offset(dims[0], indx1);
        if (off < 0 || off >= dims[0].size) return nullptr;
        size_t byte_off = static_cast<size_t>(off) * elem_stride;
        if (byte_off + elem_stride > buffer.size()) return nullptr;
        return buffer.data() + byte_off;
    }

    void* get_elem_ptr2(int indx1, int indx2) {
        if (dims.size() < 2) return nullptr;
        int off1 = compute_dim_offset(dims[0], indx1);
        int off2 = compute_dim_offset(dims[1], indx2);
        if (off1 < 0 || off1 >= dims[0].size || off2 < 0 || off2 >= dims[1].size) return nullptr;
        size_t linear = static_cast<size_t>(off1) * dims[1].size + off2;
        size_t byte_off = linear * elem_stride;
        if (byte_off + elem_stride > buffer.size()) return nullptr;
        return buffer.data() + byte_off;
    }

    void* get_elem_ptr3(int indx1, int indx2, int indx3) {
        if (dims.size() < 3) return nullptr;
        int off1 = compute_dim_offset(dims[0], indx1);
        int off2 = compute_dim_offset(dims[1], indx2);
        int off3 = compute_dim_offset(dims[2], indx3);
        if (off1 < 0 || off1 >= dims[0].size || off2 < 0 || off2 >= dims[1].size || off3 < 0 || off3 >= dims[2].size) return nullptr;
        size_t linear = (static_cast<size_t>(off1) * dims[1].size + off2) * dims[2].size + off3;
        size_t byte_off = linear * elem_stride;
        if (byte_off + elem_stride > buffer.size()) return nullptr;
        return buffer.data() + byte_off;
    }
};

struct SvScopeContext {
    std::string scope_name;
    void* user_data = nullptr;
    std::unordered_map<void*, void*> user_data_map;
};

class DpiRegistry {
private:
    std::vector<void*> handles;
    std::vector<std::string> user_libraries;
    std::unordered_map<std::string, void*> symbol_cache;
    bool attempted_load = false;

    static inline SvScopeContext* active_scope_ptr = nullptr;
    static inline void* active_engine_ptr = nullptr;
    static inline std::unordered_map<std::string, std::unique_ptr<SvScopeContext>> scope_contexts;

public:
    DpiRegistry() = default;

    void add_library(const std::string& path) {
        user_libraries.push_back(path);
    }

    const std::vector<std::string>& get_libraries() const {
        return user_libraries;
    }

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
        for (void* h : handles) {
            if (h) {
#ifndef _WIN32
                dlclose(h);
#else
                FreeLibrary(static_cast<HMODULE>(h));
#endif
            }
        }
        handles.clear();
    }

    bool load_library_file(const std::string& path) {
#ifndef _WIN32
        void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!h) {
            h = dlopen(path.c_str(), RTLD_LAZY | RTLD_GLOBAL);
        }
        if (h) {
            handles.push_back(h);
#ifndef _WIN32
            // Scan for Itanium C++ mangled symbols to support companion libraries compiled without extern "C"
            std::string cmd = "nm -D \"" + path + "\" 2>/dev/null";
            FILE* pipe = popen(cmd.c_str(), "r");
            if (pipe) {
                char buf[512];
                while (fgets(buf, sizeof(buf), pipe)) {
                    char* sym_start = strrchr(buf, ' ');
                    if (sym_start) {
                        sym_start++;
                        char* nl = strchr(sym_start, '\n');
                        if (nl) *nl = '\0';
                        char* cr = strchr(sym_start, '\r');
                        if (cr) *cr = '\0';
                        if (strncmp(sym_start, "_Z", 2) == 0) {
                            const char* p = sym_start + 2;
                            if (isdigit(static_cast<unsigned char>(*p))) {
                                int len = 0;
                                while (isdigit(static_cast<unsigned char>(*p))) {
                                    len = len * 10 + (*p - '0');
                                    p++;
                                }
                                if (len > 0 && strlen(p) >= static_cast<size_t>(len)) {
                                    std::string base_name(p, len);
                                    void* sym = dlsym(h, sym_start);
                                    if (sym && symbol_cache.find(base_name) == symbol_cache.end()) {
                                        symbol_cache[base_name] = sym;
                                    }
                                }
                            }
                        }
                    }
                }
                pclose(pipe);
            }
#endif
            return true;
        }
#else
        HMODULE h = LoadLibraryA(path.c_str());
        if (h) {
            handles.push_back(static_cast<void*>(h));
            return true;
        }
#endif
        return false;
    }

    bool ensure_loaded(const std::vector<std::string>& search_paths = {}) {
        if (!handles.empty()) return true;

        // 1. Load any explicit user-specified libraries
        for (const auto& ulib : user_libraries) {
            load_library_file(ulib);
        }

        // 2. Search default candidate libraries
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
            load_library_file(path);
        }
        return !handles.empty();
    }

    void* get_symbol(const std::string& symbol_name, const std::vector<std::string>& search_paths = {}) {
        ensure_loaded(search_paths);

        auto it = symbol_cache.find(symbol_name);
        if (it != symbol_cache.end()) return it->second;

        // Search open handles
        for (void* h : handles) {
            if (!h) continue;
            void* sym = nullptr;
#ifndef _WIN32
            sym = dlsym(h, symbol_name.c_str());
#else
            sym = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(h), symbol_name.c_str()));
#endif
            if (sym) {
                symbol_cache[symbol_name] = sym;
                return sym;
            }
        }

        // Also check executable's global symbol table (RTLD_DEFAULT)
#ifndef _WIN32
#ifdef RTLD_DEFAULT
        void* def_sym = dlsym(RTLD_DEFAULT, symbol_name.c_str());
        if (def_sym) {
            symbol_cache[symbol_name] = def_sym;
            return def_sym;
        }
#endif
#endif
        return nullptr;
    }
};
