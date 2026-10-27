#pragma once
#include "ast.hpp"
#include "kernel/netlist.hpp"
#include "profiler/profiler.hpp"
#include <unordered_map>
#include <queue>
#include <iostream>
#include <fstream>

struct BoundProcess {
    std::string scope;
    ProcessNode* proc;
    size_t pc = 0;
    bool completed = false;
};

struct NBAUpdate {
    size_t net_id;
    LogicVal val;
};

struct NBAMemUpdate {
    std::string mem_scoped_name;
    uint32_t addr;
    LogicVal val;
};

struct MemoryArray {
    std::string name;
    uint32_t word_width;
    uint32_t depth;
    std::vector<LogicVal> words;

    MemoryArray() : word_width(32), depth(0) {}
    MemoryArray(std::string n, uint32_t w, uint32_t d)
        : name(std::move(n)), word_width(w), depth(d),
          words(d, LogicVal::make_x(w)) {}
};

struct ContinuousAssignBinding {
    std::string scope;
    NetSliceRef target;
    ExpressionNode* rhs;
};

struct GateInstance {
    GateType type;
    std::string scope;
    NetSliceRef out_target;
    std::vector<ExpressionNode*> in_exprs;
};

struct MonitorSession {
    bool active = false;
    std::string scope;
    std::string format_string;
    std::vector<std::string> args;
    std::vector<LogicVal> prev_values;
    bool has_leading_time = false;
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

    bool vcd_allowed = true;

    size_t delta_cycles_count = 0;
    size_t events_dispatched = 0;
    size_t nba_commits_count = 0;

    bool vcd_enabled = false;
    std::string vcd_filename;
    std::ofstream vcd_file;
    uint64_t last_dump_time = ~0ULL;

    size_t get_or_create_net(const std::string& scoped_name, uint32_t width);
    NetSliceRef resolve_slice_ref(ExpressionNode* expr, const std::string& scope);
    LogicVal read_slice(const NetSliceRef& ref);
    void write_slice(const NetSliceRef& ref, const LogicVal& val);

    LogicVal eval_expr(ExpressionNode* expr, const std::string& scope);
    uint64_t eval_const_expr(ExpressionNode* expr, const std::string& scope);
    void execute_statement(StatementNode* stmt, BoundProcess& bp);
    void propagate_comb();
    void evaluate_sequential_blocks();
    void commit_nba_updates();
    void step_process(size_t pid);
    void check_monitor();

    void elaborate_module(ModuleNode* mod, const std::string& scope,
                          const std::unordered_map<std::string, ModuleNode*>& module_defs,
                          const std::vector<std::unique_ptr<ExpressionNode>>* overrides = nullptr,
                          const std::string& parent_scope = "",
                          std::ostream* debug_out = nullptr);

    std::string generate_vcd_id(size_t id);
    void init_vcd_header();
    void dump_vcd_sample(bool initial_dump = false);

public:
    ~Engine();
    void elaborate(std::vector<std::unique_ptr<ModuleNode>>& modules, 
                   const std::string& top_module_name = "", 
                   std::ostream* debug_out = nullptr);
    void run(std::ostream* debug_out = nullptr);

    void set_vcd_allowed(bool allowed) { vcd_allowed = allowed; }

    EngineStats get_stats();
};