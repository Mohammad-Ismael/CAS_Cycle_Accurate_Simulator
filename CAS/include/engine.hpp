#pragma once
#include "ast.hpp"
#include "netlist.hpp"
#include <unordered_map>
#include <queue>
#include <iostream>

struct BoundProcess {
    std::string scope;
    ProcessNode* proc;
    size_t pc = 0;        // Program counter for procedural initial blocks
    bool completed = false;
};

struct NetBinding {
    size_t src_net_id;
    size_t dst_net_id;
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

    // Simulation Time Wheel
    uint64_t sim_time = 0;
    std::priority_queue<SimEvent, std::vector<SimEvent>, std::greater<SimEvent>> time_wheel;
    uint32_t event_order_counter = 0;
    bool simulation_active = true;

    size_t get_or_create_net(const std::string& scoped_name, uint32_t width);
    uint64_t eval_expr(ExpressionNode* expr, const std::string& scope);
    void propagate_comb();
    void step_process(size_t pid);

public:
    void elaborate(std::vector<std::unique_ptr<ModuleNode>>& modules);
    void run();
};