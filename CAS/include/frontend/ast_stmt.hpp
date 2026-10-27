#pragma once
#include "ast_base.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

struct BlockingAssignNode : public StatementNode {
    std::vector<std::string> lhs_targets; // supports single target or concatenation {a, b}
    std::unique_ptr<ExpressionNode> rhs;

    explicit BlockingAssignNode(std::string t, std::unique_ptr<ExpressionNode> r)
        : rhs(std::move(r)) { lhs_targets.push_back(std::move(t)); }

    explicit BlockingAssignNode(std::vector<std::string> targets, std::unique_ptr<ExpressionNode> r)
        : lhs_targets(std::move(targets)), rhs(std::move(r)) {}
};

struct NonBlockingAssignNode : public StatementNode {
    std::string target;
    std::unique_ptr<ExpressionNode> rhs;
    NonBlockingAssignNode(std::string t, std::unique_ptr<ExpressionNode> r)
        : target(std::move(t)), rhs(std::move(r)) {}
};

struct MemAssignNode : public StatementNode {
    std::string mem_name;
    std::unique_ptr<ExpressionNode> addr_expr;
    std::unique_ptr<ExpressionNode> rhs;
    bool is_non_blocking = false;

    MemAssignNode(std::string name, std::unique_ptr<ExpressionNode> addr,
                  std::unique_ptr<ExpressionNode> r, bool nba = false)
        : mem_name(std::move(name)), addr_expr(std::move(addr)),
          rhs(std::move(r)), is_non_blocking(nba) {}
};

struct IfStatementNode : public StatementNode {
    std::unique_ptr<ExpressionNode> condition;
    std::vector<std::unique_ptr<StatementNode>> then_branch;
    std::vector<std::unique_ptr<StatementNode>> else_branch;
    explicit IfStatementNode(std::unique_ptr<ExpressionNode> cond)
        : condition(std::move(cond)) {}
};

struct CaseItem {
    std::unique_ptr<ExpressionNode> match_expr; // nullptr if default
    std::vector<std::unique_ptr<StatementNode>> statements;
    bool is_default = false;
};

struct CaseStatementNode : public StatementNode {
    std::unique_ptr<ExpressionNode> selector;
    std::vector<CaseItem> items;
    explicit CaseStatementNode(std::unique_ptr<ExpressionNode> sel)
        : selector(std::move(sel)) {}
};

struct TaskCallNode : public StatementNode {
    std::string task_name;
    explicit TaskCallNode(std::string name) : task_name(std::move(name)) {}
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
    explicit DisplayStatementNode(std::string fmt, std::vector<std::string> a, bool mon = false, bool lead_time = false)
        : format_string(std::move(fmt)), args(std::move(a)), is_monitor(mon), has_leading_time(lead_time) {}
};

struct FinishStatementNode : public StatementNode {};

struct DumpFileStatementNode : public StatementNode {
    std::string filename;
    explicit DumpFileStatementNode(std::string f) : filename(std::move(f)) {}
};

struct DumpVarsStatementNode : public StatementNode {};