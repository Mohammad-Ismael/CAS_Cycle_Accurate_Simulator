#pragma once
#include "../ast.hpp"
#include "netlist.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <iostream>

struct GateInstance {
    GateType type;
    std::string scope;
    NetSliceRef out_target;
    std::vector<ExpressionNode*> in_exprs;
};

struct ContinuousAssignBinding {
    std::string scope;
    NetSliceRef target;
    ExpressionNode* rhs;
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

struct BoundProcess {
    std::string scope;
    ProcessNode* proc;
    size_t pc = 0;
    bool completed = false;
};

// Represents the fully synthesized, flat netlist and bindings ready for simulation
struct ElaborationResult {
    std::vector<Net> netlist;
    std::unordered_map<std::string, size_t> net_map;
    std::vector<SliceBinding> slice_bindings;
    std::vector<ContinuousAssignBinding> continuous_assigns;
    std::vector<GateInstance> gates;
    std::unordered_map<std::string, MemoryArray> memory_map;
    std::unordered_map<std::string, TaskNode*> task_map;
    std::unordered_map<std::string, uint64_t> param_map;
    std::vector<BoundProcess> processes;
};

class Elaborator {
private:
    ElaborationResult result;

    size_t get_or_create_net(const std::string& scoped_name, uint32_t width);
    uint64_t eval_const_expr(ExpressionNode* expr, const std::string& scope);
    NetSliceRef resolve_slice_ref(ExpressionNode* expr, const std::string& scope);

    void elaborate_module(ModuleNode* mod, const std::string& scope,
                          const std::unordered_map<std::string, ModuleNode*>& module_defs,
                          const std::vector<std::unique_ptr<ExpressionNode>>* overrides = nullptr,
                          const std::string& parent_scope = "",
                          std::ostream* debug_out = nullptr);

public:
    Elaborator() = default;

    ElaborationResult elaborate(std::vector<std::unique_ptr<ModuleNode>>& modules,
                                const std::string& top_module_name = "",
                                std::ostream* debug_out = nullptr);
};