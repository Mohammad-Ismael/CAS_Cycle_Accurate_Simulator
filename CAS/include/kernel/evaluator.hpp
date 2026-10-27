#pragma once
#include "../ast.hpp"
#include "netlist.hpp"
#include <string>

class Engine;
struct BoundProcess;

class Evaluator {
private:
    Engine& engine;

public:
    explicit Evaluator(Engine& eng);

    LogicVal read_slice(const NetSliceRef& ref);
    void write_slice(const NetSliceRef& ref, const LogicVal& val);
    LogicVal eval_expr(ExpressionNode* expr, const std::string& scope);
    void execute_statement(StatementNode* stmt, BoundProcess& bp);
    void check_monitor();
};