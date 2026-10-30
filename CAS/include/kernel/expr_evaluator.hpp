#pragma once
#include "../frontend/ast_expr.hpp"
#include "netlist.hpp"
#include "execution_context.hpp"
#include <string>

class Engine;
struct BoundProcess;
struct FunctionNode;

class ExprEvaluator {
private:
    Engine& engine;
    ExecutionContext context;
    BoundProcess* current_bp = nullptr;
    ClassObject* current_this = nullptr;
    const std::vector<LogicVal>* preponed_override = nullptr;

public:
    explicit ExprEvaluator(Engine& eng);

    void set_preponed_override(const std::vector<LogicVal>* po) { preponed_override = po; }
    const std::vector<LogicVal>* get_preponed_override() const { return preponed_override; }

    void set_current_process(BoundProcess* bp) { current_bp = bp; }
    BoundProcess* get_current_process() const { return current_bp; }
    ExecutionContext& get_context() { return context; }
    const ExecutionContext& get_context() const { return context; }

    void set_current_this(ClassObject* obj) { current_this = obj; }
    ClassObject* get_current_this() const { return current_this; }

    LogicVal eval(ExpressionNode* expr, const std::string& scope);
    bool eval_function(const std::string& func_name, const std::vector<LogicVal>& arg_vals, LogicVal& result_out);
    bool eval_class_method(ClassObject* obj, FunctionNode* fn, const std::vector<LogicVal>& arg_vals, LogicVal& result_out);
};
