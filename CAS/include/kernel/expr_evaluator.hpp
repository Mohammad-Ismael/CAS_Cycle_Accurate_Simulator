#pragma once
#include "../frontend/ast_expr.hpp"
#include "netlist.hpp"
#include "execution_context.hpp"
#include <string>

class Engine;
struct BoundProcess;

class ExprEvaluator {
private:
    Engine& engine;
    ExecutionContext context;
    BoundProcess* current_bp = nullptr;

public:
    explicit ExprEvaluator(Engine& eng);

    void set_current_process(BoundProcess* bp) { current_bp = bp; }
    BoundProcess* get_current_process() const { return current_bp; }
    ExecutionContext& get_context() { return context; }
    const ExecutionContext& get_context() const { return context; }

    LogicVal eval(ExpressionNode* expr, const std::string& scope);
    bool eval_function(const std::string& func_name, const std::vector<LogicVal>& arg_vals, LogicVal& result_out);
};