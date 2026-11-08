#pragma once
#include <string>
#include <cstdint>
#include <cstddef>
#include "kernel/codegen.hpp"

typedef void (*NativeEvalFn)(uint64_t* __restrict);
typedef void (*NativeSeqEvalFn)(const uint64_t* __restrict cur, uint64_t* __restrict next, uint8_t* __restrict modified_mask);
typedef void (*NativeTopologicalEvalFn)(uint64_t* __restrict);

struct NativeDispatchTable {
    void* dl_handle = nullptr;
    NativeEvalFn* rca_table = nullptr;
    size_t rca_count = 0;
    NativeEvalFn* proc_table = nullptr;
    size_t proc_count = 0;
    NativeSeqEvalFn seq_clock_fn = nullptr;
    NativeTopologicalEvalFn topological_comb_fn = nullptr;
    bool is_active = false;

    NativeDispatchTable() = default;
    ~NativeDispatchTable();

    NativeDispatchTable(const NativeDispatchTable&) = delete;
    NativeDispatchTable& operator=(const NativeDispatchTable&) = delete;
    NativeDispatchTable(NativeDispatchTable&& other) noexcept;
    NativeDispatchTable& operator=(NativeDispatchTable&& other) noexcept;
};

class NativeJIT {
public:
    static bool compile_and_load(
        const std::string& cpp_path,
        const std::string& so_path,
        NativeDispatchTable& out_table
    );
};
