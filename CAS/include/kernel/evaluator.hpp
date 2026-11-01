#pragma once
#include "../frontend/ast_stmt.hpp"
#include "netlist.hpp"
#include "expr_evaluator.hpp"
#include <string>

class Engine;
struct BoundProcess;
struct GateInstance;

class Evaluator {
private:
    Engine& engine;
    ExprEvaluator expr_eval;
    BoundProcess* current_bp = nullptr;

public:
    explicit Evaluator(Engine& eng);

    void set_current_process(BoundProcess* bp);
    BoundProcess* get_current_process() const { return current_bp; }
    ExprEvaluator& get_expr_eval() { return expr_eval; }

    void set_current_this(ClassObject* obj) { expr_eval.set_current_this(obj); }
    ClassObject* get_current_this() const { return expr_eval.get_current_this(); }

    LogicVal read_slice(const NetSliceRef& ref);
    void write_slice(const NetSliceRef& ref, const LogicVal& val);
    void write_variable(const std::string& var_name, const LogicVal& val, BoundProcess& bp);
    LogicVal eval_expr(ExpressionNode* expr, const std::string& scope);
    LogicVal eval_gate(const GateInstance& g);
    void execute_statement(StatementNode* stmt, BoundProcess& bp);
    void check_monitor();
    std::vector<size_t> get_connected_nets(size_t start_id);
};
