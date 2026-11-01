#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <map>
#include <deque>
#include <unordered_map>
#include "kernel/netlist.hpp"
#include "kernel/evaluator.hpp"
#include "kernel/time_wheel.hpp"
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
    std::unordered_map<size_t, SvaClockEdgeHistory> sva_clock_history;

    uint64_t current_sim_time = 0;
    bool simulation_active = true;

    size_t delta_cycles_count = 0;
    size_t nba_commits_count = 0;

    void propagate_comb();
    bool evaluate_timing_checks();
    void evaluate_sequential_blocks();
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

public:
    explicit StratifiedScheduler(Engine& eng);

    void step_process(size_t pid);
    void schedule_event(uint64_t wake_time, uint32_t pid);
    void queue_nba(size_t net_id, const LogicVal& val, uint32_t offset = 0, uint32_t slice_width = 0, bool is_slice = false);
    void queue_nba_mem(const std::string& scoped_mem, int64_t addr, const LogicVal& val);
    void queue_delayed_nba(uint64_t wake_time, size_t net_id, const LogicVal& val, uint32_t offset = 0, uint32_t slice_width = 0, bool is_slice = false);
    void queue_delayed_nba_mem(uint64_t wake_time, const std::string& scoped_mem, int64_t addr, const LogicVal& val);
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