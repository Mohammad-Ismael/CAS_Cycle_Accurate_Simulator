#pragma once
#include "ast.hpp"
#include "kernel/netlist.hpp"
#include "kernel/elaborator.hpp"
#include "kernel/evaluator.hpp"
#include "profiler/profiler.hpp"
#include "tracer/vcd_dumper.hpp"
#include <unordered_map>
#include <queue>
#include <iostream>
#include <vector>

struct NBAUpdate {
    size_t net_id;
    LogicVal val;
};

struct NBAMemUpdate {
    std::string mem_scoped_name;
    uint32_t addr;
    LogicVal val;
};

struct MonitorSession {
    bool active = false;
    std::string scope;
    std::string format_string;
    std::vector<std::string> args;
    std::vector<LogicVal> prev_values;
    bool has_leading_time = false;
    std::string src_file = "";
    size_t src_line = 0;
};

struct SimEvent {
    uint64_t time;
    uint32_t process_id;
    uint32_t order;

    bool operator>(const SimEvent& other) const {
        if (time != other.time) return time > other.time;
        return order > other.order;
    }
};

class Engine {
    friend class Evaluator;

private:
    std::vector<Net> netlist;
    std::unordered_map<std::string, size_t> net_map;
    std::vector<BoundProcess> processes;

    std::vector<SliceBinding> slice_bindings;
    std::vector<ContinuousAssignBinding> continuous_assigns;
    std::vector<GateInstance> gates;
    std::unordered_map<std::string, MemoryArray> memory_map;
    std::unordered_map<std::string, TaskNode*> task_map;
    std::unordered_map<std::string, uint64_t> param_map;

    MonitorSession monitor_session;

    std::vector<NBAUpdate> nba_queue;
    std::vector<NBAMemUpdate> nba_mem_queue;

    uint64_t sim_time = 0;
    std::priority_queue<SimEvent, std::vector<SimEvent>, std::greater<SimEvent>> time_wheel;
    uint32_t event_order_counter = 0;
    bool simulation_active = true;

    // Verbosity and debug control flags
    bool allow_display = true;
    bool allow_monitor = true;
    bool allow_finish_msg = true;

    // Profiler metrics
    size_t delta_cycles_count = 0;
    size_t events_dispatched = 0;
    size_t nba_commits_count = 0;

    VcdDumper vcd_dumper;
    Evaluator evaluator;

    void propagate_comb();
    void evaluate_sequential_blocks();
    void commit_nba_updates();
    void step_process(size_t pid);

public:
    Engine();
    ~Engine() = default;

    void set_vcd_allowed(bool allowed) { vcd_dumper.set_allowed(allowed); }
    void set_display_allowed(bool allowed) { allow_display = allowed; }
    void set_monitor_allowed(bool allowed) { allow_monitor = allowed; }
    void set_finish_msg_allowed(bool allowed) { allow_finish_msg = allowed; }

    bool is_display_allowed() const { return allow_display; }
    bool is_monitor_allowed() const { return allow_monitor; }
    bool is_finish_msg_allowed() const { return allow_finish_msg; }

    void load_elaboration(ElaborationResult&& elab_res);
    void elaborate(std::vector<std::unique_ptr<ModuleNode>>& modules, 
                   const std::string& top_module_name = "", 
                   std::ostream* debug_out = nullptr);
    void run(std::ostream* debug_out = nullptr);

    EngineStats get_stats();
};