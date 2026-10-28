#pragma once
#include "../frontend/ast_expr.hpp"
#include "netlist.hpp"
#include <string>

class Engine;

class ExprEvaluator {
private:
    Engine& engine;

public:
    explicit ExprEvaluator(Engine& eng);

    LogicVal eval(ExpressionNode* expr, const std::string& scope);
};