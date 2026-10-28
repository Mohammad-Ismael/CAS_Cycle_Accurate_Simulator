#pragma once
#include "../frontend/ast_expr.hpp"
#include "netlist.hpp"
#include "execution_context.hpp"
#include <string>

class Engine;

class ExprEvaluator {
private:
    Engine& engine;
    ExecutionContext context;

public:
    explicit ExprEvaluator(Engine& eng);

    ExecutionContext& get_context() { return context; }
    const ExecutionContext& get_context() const { return context; }

    LogicVal eval(ExpressionNode* expr, const std::string& scope);
    bool eval_function(const std::string& func_name, const std::vector<LogicVal>& arg_vals, LogicVal& result_out);
};