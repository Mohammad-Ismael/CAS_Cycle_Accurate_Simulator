#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <map>
#include <deque>
#include <unordered_map>
#include "kernel/netlist.hpp"
#include "kernel/evaluator.hpp"
#include "kernel/bytecode.hpp"
#include "kernel/native_jit.hpp"
#include "kernel/llvm_jit.hpp"
#include "kernel/time_wheel.hpp"
#include "kernel/memory_fastpath.hpp"
#include "tracer/vcd_dumper.hpp"

class Engine;

struct NBAUpdate {
    size_t net_id;
    LogicVal val;
    uint32_t offset = 0;
    uint32_t slice_width = 0;
    bool is_slice = false;
};

struct NBAMemUpdate {
    std::string mem_scoped_name;
    int64_t addr;
    LogicVal val;
    uint32_t offset = 0;
    uint32_t slice_width = 0;
    bool is_slice = false;
};

struct DelayedBlockingAssign {
    size_t pid;
    std::string target_name;
    std::string scope;
    LogicVal val;
};

struct SvaClockEdgeHistory {
    uint64_t last_recorded_time = (uint64_t)-1;
    std::deque<std::vector<LogicVal>> snapshots;
};

struct IntraEventNbaItem {
    size_t net_id;
    LogicVal val;
    EdgeType edge;
    size_t trigger_net_id;
    uint64_t reg_time;
    uint32_t offset = 0;
    uint32_t slice_width = 0;
    bool is_slice = false;
};

struct IntraEventBlockingItem {
    size_t pid;
    std::string target_name;
    std::string scope;
    LogicVal val;
    EdgeType edge;
    size_t trigger_net_id;
    uint64_t reg_time;
};

struct NBAEventItem {
    size_t event_id;
};

struct PendingSpecifyNetEvent {
    size_t net_id;
    LogicVal new_val;
    uint64_t target_time;
};

class StratifiedScheduler {
private:
    Engine& engine;
    TimeWheel time_wheel;

    std::vector<NBAUpdate> nba_queue;
    std::vector<NBAMemUpdate> nba_mem_queue;

    std::map<uint64_t, std::vector<NBAUpdate>> delayed_nba_queue;
    std::map<uint64_t, std::vector<NBAMemUpdate>> delayed_nba_mem_queue;
    std::map<uint64_t, std::vector<DelayedBlockingAssign>> delayed_blocking_queue;
    std::unordered_map<size_t, PendingSpecifyNetEvent> pending_specify_events;
    std::vector<IntraEventNbaItem> intra_event_nba_queue;
    std::vector<IntraEventBlockingItem> intra_event_blocking_queue;

    bool trigger_intra_event_nbas();
    bool trigger_intra_event_blocking();
    void evaluate_clocking_blocks();
    void evaluate_covergroup_events();
    void capture_preponed_samples();
    bool evaluate_sva_assertions();
    std::vector<LogicVal> preponed_net_values;
    std::map<uint64_t, std::vector<LogicVal>> net_time_history;
    LogicVal get_historical_net_value(size_t net_id, uint64_t sample_time) const;
    std::unordered_map<size_t, SvaClockEdgeHistory> sva_clock_history;

    uint64_t current_sim_time = 0;
    bool simulation_active = true;

    size_t delta_cycles_count = 0;
    size_t nba_commits_count = 0;

    void propagate_comb();
    bool execute_bytecode_2state_64(const CompiledExpr& expr, uint64_t& out_val);
    LogicVal execute_bytecode_logicval(const CompiledExpr& expr);
    bool execute_process_bytecode_2state_64(BoundProcess& bp);
    void execute_process_bytecode_logicval(BoundProcess& bp);
    void execute_comb_proc_bytecode(BoundProcess& bp);
    bool evaluate_timing_checks();
    void evaluate_sequential_blocks(uint32_t triggered_net_id = UINT32_MAX, EdgeType edge = EdgeType::LEVEL);
    bool commit_nba_updates();
    void execute_time_slot_delta_loop();
    bool resume_waiting_processes(bool for_reactive = false);
    void check_program_completion();
    std::vector<uint32_t> due_reactive_pids;
    void transfer_due_delayed_updates(uint64_t target_time);
    void invoke_task(BoundProcess& bp, TaskCallNode* tcall);
    void write_task_output(BoundProcess& bp, const std::string& actual_name, const LogicVal& val);
    void invoke_fork(size_t pid, ForkStatementNode* fork_stmt);
    void notify_parent_child_completed(size_t child_pid);
    bool has_active_descendants(size_t pid);
    void disable_fork(size_t caller_pid);
    void terminate_process_tree(size_t target_pid);
    std::vector<NBAEventItem> nba_event_queue;
    void trigger_named_event(const std::string& ev_name, const std::string& scope, bool is_nonblocking);
    void trigger_named_event_id(size_t ev_id);

    std::vector<uint32_t> dirty_rcas;
    std::vector<uint8_t> rca_enqueued;

    std::vector<uint32_t> dirty_comb_procs;
    std::vector<uint8_t> comb_proc_enqueued;

public:
    explicit StratifiedScheduler(Engine& eng);

    NativeDispatchTable native_dispatch;
    LlvmDispatchTable llvm_dispatch;
    cas::MemoryFastPath memory_fastpath;
    std::vector<uint64_t> native_nets_u64;
    std::vector<uint64_t> native_nets_next;
    std::vector<uint8_t> native_modified_mask;
    std::vector<uint32_t> dirty_native_nets; // fast list of modified net IDs
    std::vector<uint32_t> native_clk_net_ids;
    std::vector<uint32_t> native_seq_dst_nets;
    bool native_seq_fired_in_time_slot = false;

    struct TopoTargetRecord {
        uint32_t net_id;
        uint32_t canon_id;
        uint32_t width;
        bool is_signed;
    };
    std::vector<TopoTargetRecord> topological_target_records;
    std::vector<uint8_t> is_topological_rca;

    void init_scheduler();
    void sync_all_native_nets();
    bool commit_native_clock_updates();

    void notify_net_changed(size_t net_id);
    void notify_mem_changed();
    void enqueue_rca(uint32_t rca_idx);
    void enqueue_comb_proc(uint32_t proc_id);

    void step_process(size_t pid);
    void schedule_event(uint64_t wake_time, uint32_t pid);
    void queue_nba(size_t net_id, const LogicVal& val, uint32_t offset = 0, uint32_t slice_width = 0, bool is_slice = false);
    void queue_nba_mem(const std::string& scoped_mem, int64_t addr, const LogicVal& val, uint32_t offset = 0, uint32_t slice_width = 0, bool is_slice = false);
    void queue_delayed_nba(uint64_t wake_time, size_t net_id, const LogicVal& val, uint32_t offset = 0, uint32_t slice_width = 0, bool is_slice = false);
    void queue_delayed_nba_mem(uint64_t wake_time, const std::string& scoped_mem, int64_t addr, const LogicVal& val, uint32_t offset = 0, uint32_t slice_width = 0, bool is_slice = false);
    void queue_delayed_blocking(uint64_t wake_time, size_t pid, const std::string& target_name, const std::string& scope, const LogicVal& val);
    void queue_intra_event_nba(size_t trigger_net_id, EdgeType edge, size_t net_id, const LogicVal& val, uint32_t offset = 0, uint32_t slice_width = 0, bool is_slice = false);
    void queue_intra_event_blocking(size_t trigger_net_id, EdgeType edge, size_t pid, const std::string& target_name, const std::string& scope, const LogicVal& val);

    void terminate();
    bool is_active() const { return simulation_active; }

    uint64_t get_sim_time() const { return current_sim_time; }
    void run_simulation();

    size_t get_delta_cycles() const { return delta_cycles_count; }
    size_t get_nba_commits() const { return nba_commits_count; }
    size_t get_events_dispatched() const { return time_wheel.events_dispatched(); }
};