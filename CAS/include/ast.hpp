#pragma once
#include "lexer.hpp"
#include "common/types.hpp"
#include <string>
#include <vector>
#include <memory>

struct SensitivityTarget {
    EdgeType edge;
    std::string signal_name;
};

struct ASTNode {
    virtual ~ASTNode() = default;
};

struct ExpressionNode : public ASTNode {
    virtual ~ExpressionNode() = default;
    virtual std::unique_ptr<ExpressionNode> clone() const = 0;
};

struct IdentifierNode : public ExpressionNode {
    std::string name;
    IdentifierNode(std::string n) : name(std::move(n)) {}
    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<IdentifierNode>(name);
    }
};

struct NumberNode : public ExpressionNode {
    uint64_t value;
    uint32_t width;
    bool is_unknown;
    NumberNode(uint64_t val, uint32_t w, bool unk = false)
        : value(val), width(w), is_unknown(unk) {}
    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<NumberNode>(value, width, is_unknown);
    }
};

struct IndexSelectNode : public ExpressionNode {
    std::string name;
    std::unique_ptr<ExpressionNode> msb_expr;
    std::unique_ptr<ExpressionNode> lsb_expr;
    bool is_part_select;

    IndexSelectNode(std::string n, std::unique_ptr<ExpressionNode> idx)
        : name(std::move(n)), msb_expr(std::move(idx)), lsb_expr(nullptr), is_part_select(false) {}

    IndexSelectNode(std::string n, std::unique_ptr<ExpressionNode> msb, std::unique_ptr<ExpressionNode> lsb)
        : name(std::move(n)), msb_expr(std::move(msb)), lsb_expr(std::move(lsb)), is_part_select(true) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        if (!is_part_select) {
            return std::make_unique<IndexSelectNode>(name, msb_expr ? msb_expr->clone() : nullptr);
        }
        return std::make_unique<IndexSelectNode>(name, msb_expr ? msb_expr->clone() : nullptr, lsb_expr ? lsb_expr->clone() : nullptr);
    }
};

struct UnaryOpNode : public ExpressionNode {
    char op;
    std::unique_ptr<ExpressionNode> operand;
    UnaryOpNode(char o, std::unique_ptr<ExpressionNode> opnd)
        : op(o), operand(std::move(opnd)) {}
    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<UnaryOpNode>(op, operand ? operand->clone() : nullptr);
    }
};

struct BinaryOpNode : public ExpressionNode {
    std::string op;
    std::unique_ptr<ExpressionNode> lhs;
    std::unique_ptr<ExpressionNode> rhs;
    BinaryOpNode(std::string o, std::unique_ptr<ExpressionNode> l, std::unique_ptr<ExpressionNode> r)
        : op(std::move(o)), lhs(std::move(l)), rhs(std::move(r)) {}
    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<BinaryOpNode>(op, lhs ? lhs->clone() : nullptr, rhs ? rhs->clone() : nullptr);
    }
};

struct TernaryOpNode : public ExpressionNode {
    std::unique_ptr<ExpressionNode> condition;
    std::unique_ptr<ExpressionNode> true_expr;
    std::unique_ptr<ExpressionNode> false_expr;
    TernaryOpNode(std::unique_ptr<ExpressionNode> cond,
                  std::unique_ptr<ExpressionNode> t_expr,
                  std::unique_ptr<ExpressionNode> f_expr)
        : condition(std::move(cond)), true_expr(std::move(t_expr)), false_expr(std::move(f_expr)) {}
    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<TernaryOpNode>(condition ? condition->clone() : nullptr,
                                               true_expr ? true_expr->clone() : nullptr,
                                               false_expr ? false_expr->clone() : nullptr);
    }
};

struct StatementNode : public ASTNode {
    virtual ~StatementNode() = default;
};

struct BlockingAssignNode : public StatementNode {
    std::vector<std::string> lhs_targets;
    std::unique_ptr<ExpressionNode> rhs;
    BlockingAssignNode(std::vector<std::string> targets, std::unique_ptr<ExpressionNode> r)
        : lhs_targets(std::move(targets)), rhs(std::move(r)) {}
};

struct NonBlockingAssignNode : public StatementNode {
    std::string target;
    std::unique_ptr<ExpressionNode> rhs;
    NonBlockingAssignNode(std::string tgt, std::unique_ptr<ExpressionNode> r)
        : target(std::move(tgt)), rhs(std::move(r)) {}
};

struct MemAssignNode : public StatementNode {
    std::string mem_name;
    std::unique_ptr<ExpressionNode> addr_expr;
    std::unique_ptr<ExpressionNode> rhs;
    bool is_non_blocking;

    MemAssignNode(std::string name, std::unique_ptr<ExpressionNode> addr,
                  std::unique_ptr<ExpressionNode> r, bool nb)
        : mem_name(std::move(name)), addr_expr(std::move(addr)),
          rhs(std::move(r)), is_non_blocking(nb) {}
};

struct TaskCallNode : public StatementNode {
    std::string task_name;
    explicit TaskCallNode(std::string name) : task_name(std::move(name)) {}
};

struct IfStatementNode : public StatementNode {
    std::unique_ptr<ExpressionNode> condition;
    std::vector<std::unique_ptr<StatementNode>> then_branch;
    std::vector<std::unique_ptr<StatementNode>> else_branch;
    IfStatementNode(std::unique_ptr<ExpressionNode> cond) : condition(std::move(cond)) {}
};

struct CaseItem {
    bool is_default;
    std::unique_ptr<ExpressionNode> match_expr;
    std::vector<std::unique_ptr<StatementNode>> statements;
};

struct CaseStatementNode : public StatementNode {
    std::unique_ptr<ExpressionNode> selector;
    std::vector<CaseItem> items;
    explicit CaseStatementNode(std::unique_ptr<ExpressionNode> sel) : selector(std::move(sel)) {}
};

struct DelayStatementNode : public StatementNode {
    uint64_t delay;
    explicit DelayStatementNode(uint64_t d) : delay(d) {}
};

struct DisplayStatementNode : public StatementNode {
    std::string format_string;
    std::vector<std::string> args;
    bool is_monitor = false;
    bool has_leading_time = false;

    DisplayStatementNode(std::string fmt, std::vector<std::string> a, bool mon = false, bool lead_time = false)
        : format_string(std::move(fmt)), args(std::move(a)), is_monitor(mon), has_leading_time(lead_time) {}
};

struct FinishStatementNode : public StatementNode {};

struct DumpFileStatementNode : public StatementNode {
    std::string filename;
    explicit DumpFileStatementNode(std::string file) : filename(std::move(file)) {}
};

struct DumpVarsStatementNode : public StatementNode {};

struct ContinuousAssignNode : public ASTNode {
    std::string target_name;
    std::unique_ptr<ExpressionNode> rhs;
    ContinuousAssignNode(std::string tgt, std::unique_ptr<ExpressionNode> r)
        : target_name(std::move(tgt)), rhs(std::move(r)) {}
};

struct GatePrimitiveNode : public ASTNode {
    GateType type;
    std::string instance_name;
    std::unique_ptr<ExpressionNode> out_expr;
    std::vector<std::unique_ptr<ExpressionNode>> in_exprs;
};

enum class ProcessType {
    COMBINATIONAL,
    SEQUENTIAL,
    INITIAL
};

struct ProcessNode : public ASTNode {
    ProcessType type;
    std::vector<SensitivityTarget> sensitivity_list;
    std::vector<std::unique_ptr<StatementNode>> body;
    ProcessNode(ProcessType t) : type(t) {}
};

struct TaskNode : public ASTNode {
    std::string name;
    std::vector<std::unique_ptr<StatementNode>> body;
};

struct ParameterDef {
    std::string name;
    std::unique_ptr<ExpressionNode> default_expr;
};

struct PortNode {
    std::string name;
    std::string direction;
    std::unique_ptr<ExpressionNode> msb_expr;
    std::unique_ptr<ExpressionNode> lsb_expr;
    bool is_array = false;
    std::unique_ptr<ExpressionNode> bound1_expr;
    std::unique_ptr<ExpressionNode> bound2_expr;
};

struct PortConnection {
    std::string port_name;
    std::unique_ptr<ExpressionNode> expr;
};

struct InstantiationNode : public ASTNode {
    std::string module_type;
    std::string instance_name;
    bool is_positional = false;
    std::vector<std::unique_ptr<ExpressionNode>> param_overrides;
    std::vector<PortConnection> connections;

    InstantiationNode(std::string type, std::string inst, bool pos = false)
        : module_type(std::move(type)), instance_name(std::move(inst)), is_positional(pos) {}
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
    std::vector<std::unique_ptr<InstantiationNode>> instantiations;
};