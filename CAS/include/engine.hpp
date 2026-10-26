#pragma once
#include "ast.hpp"
#include "netlist.hpp"
#include <unordered_map>
#include <iostream>

class Engine {
private:
    std::vector<Net> netlist;
    std::unordered_map<std::string, size_t> net_map;
    std::vector<std::unique_ptr<ProcessNode>> active_processes;
    bool simulation_active = true;

    size_t get_or_create_net(const std::string& name, uint32_t width);
    uint64_t eval_expr(ExpressionNode* expr);

public:
    void elaborate(std::vector<std::unique_ptr<ModuleNode>>& modules);
    void run();
};