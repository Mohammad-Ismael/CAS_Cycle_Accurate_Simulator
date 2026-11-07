#include "kernel/native_jit.hpp"
#include <cstdlib>
#include <iostream>

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#define HAS_DLOPEN 1
#else
#define HAS_DLOPEN 0
#endif

NativeDispatchTable::~NativeDispatchTable() {
#if HAS_DLOPEN
    if (dl_handle) {
        dlclose(dl_handle);
        dl_handle = nullptr;
    }
#endif
    rca_table = nullptr;
    rca_count = 0;
    proc_table = nullptr;
    proc_count = 0;
    seq_clock_fn = nullptr;
    is_active = false;
}

NativeDispatchTable::NativeDispatchTable(NativeDispatchTable&& other) noexcept
    : dl_handle(other.dl_handle),
      rca_table(other.rca_table),
      rca_count(other.rca_count),
      proc_table(other.proc_table),
      proc_count(other.proc_count),
      seq_clock_fn(other.seq_clock_fn),
      is_active(other.is_active) {
    other.dl_handle = nullptr;
    other.rca_table = nullptr;
    other.rca_count = 0;
    other.proc_table = nullptr;
    other.proc_count = 0;
    other.seq_clock_fn = nullptr;
    other.is_active = false;
}

NativeDispatchTable& NativeDispatchTable::operator=(NativeDispatchTable&& other) noexcept {
    if (this != &other) {
#if HAS_DLOPEN
        if (dl_handle) dlclose(dl_handle);
#endif
        dl_handle = other.dl_handle;
        rca_table = other.rca_table;
        rca_count = other.rca_count;
        proc_table = other.proc_table;
        proc_count = other.proc_count;
        seq_clock_fn = other.seq_clock_fn;
        is_active = other.is_active;

        other.dl_handle = nullptr;
        other.rca_table = nullptr;
        other.rca_count = 0;
        other.proc_table = nullptr;
        other.proc_count = 0;
        other.seq_clock_fn = nullptr;
        other.is_active = false;
    }
    return *this;
}

bool NativeJIT::compile_and_load(
    const std::string& cpp_path,
    const std::string& so_path,
    NativeDispatchTable& out_table
) {
#if !HAS_DLOPEN
    std::cerr << "[NativeJIT] dlopen not supported on this platform.\n";
    return false;
#else
    // 1. Compile C++ into shared object (.so)
    std::string cmd = "g++ -O3 -shared -fPIC \"" + cpp_path + "\" -o \"" + so_path + "\" 2>&1";
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::cerr << "[NativeJIT] Shared object compilation failed with exit code " << ret << "\n";
        return false;
    }

    // 2. Open shared library
    void* handle = dlopen(so_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* err = dlerror();
        std::cerr << "[NativeJIT] dlopen failed: " << (err ? err : "unknown error") << "\n";
        return false;
    }

    // 3. Look up table discovery function
    typedef void (*GetTableFn)(NativeEvalFn**, size_t*, NativeEvalFn**, size_t*, NativeSeqEvalFn*);
    dlerror(); // Clear error
    GetTableFn get_tbl = (GetTableFn)dlsym(handle, "get_native_eval_table");
    const char* dlsym_err = dlerror();
    if (dlsym_err || !get_tbl) {
        std::cerr << "[NativeJIT] dlsym failed to find get_native_eval_table: "
                  << (dlsym_err ? dlsym_err : "symbol is null") << "\n";
        dlclose(handle);
        return false;
    }

    // 4. Retrieve tables
    NativeEvalFn* rca_tbl = nullptr;
    size_t rca_cnt = 0;
    NativeEvalFn* proc_tbl = nullptr;
    size_t proc_cnt = 0;
    NativeSeqEvalFn seq_fn = nullptr;
    get_tbl(&rca_tbl, &rca_cnt, &proc_tbl, &proc_cnt, &seq_fn);

    out_table.dl_handle = handle;
    out_table.rca_table = rca_tbl;
    out_table.rca_count = rca_cnt;
    out_table.proc_table = proc_tbl;
    out_table.proc_count = proc_cnt;
    out_table.seq_clock_fn = seq_fn;
    out_table.is_active = true;

    return true;
#endif
}
