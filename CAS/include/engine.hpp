#pragma once
#include "ast.hpp"
#include "netlist.hpp"
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

struct NetBinding {
    size_t src_net_id;
    size_t dst_net_id;
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
    size_t target_net_id;
    ExpressionNode* rhs;
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
    std::vector<NetBinding> port_bindings;

    // Memory arrays & continuous assigns
    std::unordered_map<std::string, MemoryArray> memory_map;
    std::vector<ContinuousAssignBinding> continuous_assigns;

    // Stratified IEEE 1800 Regions
    std::vector<NBAUpdate> nba_queue;
    std::vector<NBAMemUpdate> nba_mem_queue;

    // Simulation Time Wheel
    uint64_t sim_time = 0;
    std::priority_queue<SimEvent, std::vector<SimEvent>, std::greater<SimEvent>> time_wheel;
    uint32_t event_order_counter = 0;
    bool simulation_active = true;

    // VCD Dumper State
    bool vcd_enabled = false;
    std::string vcd_filename;
    std::ofstream vcd_file;
    uint64_t last_dump_time = ~0ULL;

    size_t get_or_create_net(const std::string& scoped_name, uint32_t width);
    LogicVal eval_expr(ExpressionNode* expr, const std::string& scope);
    void execute_statement(StatementNode* stmt, BoundProcess& bp);
    void propagate_comb();
    void evaluate_sequential_blocks();
    void commit_nba_updates();
    void step_process(size_t pid);

    // Hierarchical Elaboration
    void elaborate_module(ModuleNode* mod, const std::string& scope,
                          const std::unordered_map<std::string, ModuleNode*>& module_defs);

    // VCD Subroutines
    std::string generate_vcd_id(size_t id);
    void init_vcd_header();
    void dump_vcd_sample(bool initial_dump = false);

public:
    ~Engine();
    void elaborate(std::vector<std::unique_ptr<ModuleNode>>& modules);
    void run();
};