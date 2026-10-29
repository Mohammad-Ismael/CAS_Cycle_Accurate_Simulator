#pragma once
#include "ast_base.hpp"
#include "ast_expr.hpp"
#include "ast_stmt.hpp"
#include "../common/types.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

struct PortNode : public ASTNode {
    std::string name;
    std::string direction; // "input", "output", "inout", "internal"
    std::unique_ptr<ExpressionNode> msb_expr;
    std::unique_ptr<ExpressionNode> lsb_expr;
    bool is_signed = false;
    bool is_array = false;
    std::unique_ptr<ExpressionNode> bound1_expr;
    std::unique_ptr<ExpressionNode> bound2_expr;
    NetKind net_kind = NetKind::WIRE;

    PortNode() = default;

    PortNode(std::string n, std::string dir,
             std::unique_ptr<ExpressionNode> msb = nullptr,
             std::unique_ptr<ExpressionNode> lsb = nullptr,
             bool signed_flag = false,
             NetKind kind = NetKind::WIRE)
        : name(std::move(n)), direction(std::move(dir)),
          msb_expr(std::move(msb)), lsb_expr(std::move(lsb)),
          is_signed(signed_flag), net_kind(kind) {}
};

struct ParameterDef {
    std::string name;
    std::unique_ptr<ExpressionNode> default_expr;
};

struct ParamOverride {
    std::string param_name;
    std::unique_ptr<ExpressionNode> expr;
};

enum class ProcessType {
    INITIAL,
    ALWAYS,
    COMBINATIONAL,
    SEQUENTIAL
};

struct SensitivityTarget {
    EdgeType edge;
    std::string signal_name;
};

struct ProcessNode : public ASTNode {
    ProcessType type;
    std::vector<SensitivityTarget> sensitivity_list;
    std::vector<std::unique_ptr<StatementNode>> body;
    bool is_always_ff = false;
    bool is_always_comb = false;
    bool is_always_latch = false;

    ProcessNode() = default;
    explicit ProcessNode(ProcessType t) : type(t) {}
};

struct TaskNode : public ASTNode {
    std::string name;
    std::string scope = "";
    bool is_automatic = false;
    std::vector<PortNode> ports;
    std::vector<std::unique_ptr<StatementNode>> body;

    TaskNode() = default;
    explicit TaskNode(std::string n, bool autom = false)
        : name(std::move(n)), is_automatic(autom) {}
};

struct FunctionNode : public ASTNode {
    std::string name;
    std::string scope = "";
    bool is_automatic = false;
    uint32_t return_width = 32;
    bool return_signed = false;
    std::vector<PortNode> inputs;
    std::vector<std::unique_ptr<StatementNode>> body;

    FunctionNode() = default;
};

struct PortConnection {
    std::string port_name;
    std::unique_ptr<ExpressionNode> expr;
};

struct InstantiationNode : public ASTNode {
    std::string module_type;
    std::string instance_name;
    std::vector<ParamOverride> param_overrides;
    std::vector<PortConnection> connections;
    bool is_positional = false;

    InstantiationNode() = default;

    InstantiationNode(std::string type, std::string inst)
        : module_type(std::move(type)), instance_name(std::move(inst)) {}

    InstantiationNode(std::string type, std::string inst, bool positional)
        : module_type(std::move(type)), instance_name(std::move(inst)), is_positional(positional) {}
};

struct ContinuousAssignNode : public ASTNode {
    std::string target_name;
    std::unique_ptr<ExpressionNode> rhs;

    ContinuousAssignNode() = default;
    ContinuousAssignNode(std::string target, std::unique_ptr<ExpressionNode> r)
        : target_name(std::move(target)), rhs(std::move(r)) {}
};

struct GatePrimitiveNode : public ASTNode {
    GateType type = GateType::AND;
    std::string instance_name = "";
    std::unique_ptr<ExpressionNode> out_expr;
    std::vector<std::unique_ptr<ExpressionNode>> in_exprs;

    GatePrimitiveNode() = default;

    GatePrimitiveNode(GateType t, std::unique_ptr<ExpressionNode> out,
                      std::vector<std::unique_ptr<ExpressionNode>> ins,
                      std::string inst = "")
        : type(t), instance_name(std::move(inst)), out_expr(std::move(out)), in_exprs(std::move(ins)) {}
};

struct ModuleNode : public ASTNode {
    std::string name;
    std::string defined_in_file = "";
    size_t defined_on_line = 0;
    std::vector<ParameterDef> parameters;
    std::vector<PortNode> ports;
    std::vector<PortNode> signals;
    std::vector<std::unique_ptr<ContinuousAssignNode>> continuous_assigns;
    std::vector<std::unique_ptr<GatePrimitiveNode>> gates;
    std::vector<std::unique_ptr<ProcessNode>> processes;
    std::vector<std::unique_ptr<TaskNode>> tasks;
    std::vector<std::unique_ptr<FunctionNode>> functions;
    std::vector<std::unique_ptr<InstantiationNode>> instantiations;

    ModuleNode() = default;
};