#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

struct ResolvedContinuousAssign;
struct BoundProcess;
class Engine;

typedef void (*LlvmTopologicalCombFn)(uint64_t* __restrict nets);
typedef void (*LlvmSeqClockFn)(const uint64_t* __restrict cur, uint64_t* __restrict next, uint8_t* __restrict mask);

struct LlvmDispatchTable {
    LlvmTopologicalCombFn topological_comb_fn = nullptr;
    LlvmSeqClockFn seq_clock_fn = nullptr;
    bool is_active = false;
    std::shared_ptr<void> jit_engine_handle;

    LlvmDispatchTable() = default;
    ~LlvmDispatchTable() = default;
    LlvmDispatchTable(const LlvmDispatchTable&) = delete;
    LlvmDispatchTable& operator=(const LlvmDispatchTable&) = delete;
    LlvmDispatchTable(LlvmDispatchTable&&) noexcept = default;
    LlvmDispatchTable& operator=(LlvmDispatchTable&&) noexcept = default;
};

class LlvmJIT {
public:
    static bool is_available();

    static bool compile_and_bind(
        const std::vector<ResolvedContinuousAssign>& rcas,
        std::vector<BoundProcess>& procs,
        Engine& engine,
        LlvmDispatchTable& out_table
    );
};
