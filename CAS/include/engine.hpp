#pragma once
#include "ast.hpp"
#include "netlist.hpp"
#include <unordered_map>
#include <iostream>

struct BoundProcess {
    std::string scope;
    ProcessNode* proc;
};

struct NetBinding {
    size_t src_net_id;
    size_t dst_net_id;
};

class Engine {
private:
    std::vector<Net> netlist;
    std::unordered_map<std::string, size_t> net_map; // Hierarchical name -> ID
    std::vector<BoundProcess> processes;
    std::vector<NetBinding> port_bindings; // Continuous interconnects
    bool simulation_active = true;

    size_t get_or_create_net(const std::string& scoped_name, uint32_t width);
    uint64_t eval_expr(ExpressionNode* expr, const std::string& scope);

public:
    void elaborate(std::vector<std::unique_ptr<ModuleNode>>& modules);
    void run();
};