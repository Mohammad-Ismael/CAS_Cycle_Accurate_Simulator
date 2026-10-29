#pragma once
#include "ast_base.hpp"
#include "../common/types.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

struct EventControlStatementNode : public StatementNode {
    EdgeType edge = EdgeType::LEVEL;
    std::string signal_name;

    EventControlStatementNode(EdgeType e, std::string sig)
        : edge(e), signal_name(std::move(sig)) {}
};

struct ForeverStatementNode : public StatementNode {
    std::vector<std::unique_ptr<StatementNode>> body;

    explicit ForeverStatementNode(std::vector<std::unique_ptr<StatementNode>> b)
        : body(std::move(b)) {}
};

struct BlockStatementNode : public StatementNode {
    std::vector<std::unique_ptr<StatementNode>> statements;

    BlockStatementNode() = default;
    explicit BlockStatementNode(std::vector<std::unique_ptr<StatementNode>> stmts)
        : statements(std::move(stmts)) {}
};

struct VarDeclStatementNode : public StatementNode {
    std::string var_name;
    uint32_t width = 32;
    bool is_signed = false;
    std::string type_name = "";

    VarDeclStatementNode(std::string n, uint32_t w = 32, bool s = false, std::string t = "")
        : var_name(std::move(n)), width(w), is_signed(s), type_name(std::move(t)) {}
};

struct BlockingAssignNode : public StatementNode {
    std::vector<std::string> lhs_targets;
    std::unique_ptr<ExpressionNode> rhs;
    std::unique_ptr<ExpressionNode> bit_idx = nullptr;
    uint64_t intra_delay = 0;
    bool has_intra_event = false;
    EdgeType intra_edge = EdgeType::LEVEL;
    std::string intra_signal = std::string();

    void set_intra_event(EdgeType edge, std::string sig) {
        has_intra_event = true;
        intra_edge = edge;
        intra_signal = std::move(sig);
    }

    explicit BlockingAssignNode(std::string t, std::unique_ptr<ExpressionNode> r, std::unique_ptr<ExpressionNode> idx = nullptr, uint64_t delay = 0)
        : rhs(std::move(r)), bit_idx(std::move(idx)), intra_delay(delay) { lhs_targets.push_back(std::move(t)); }

    explicit BlockingAssignNode(std::vector<std::string> targets, std::unique_ptr<ExpressionNode> r, uint64_t delay = 0)
        : lhs_targets(std::move(targets)), rhs(std::move(r)), intra_delay(delay) {}
};

struct NonBlockingAssignNode : public StatementNode {
    std::string target;
    std::unique_ptr<ExpressionNode> rhs;
    uint64_t intra_delay = 0;
    bool has_intra_event = false;
    EdgeType intra_edge = EdgeType::LEVEL;
    std::string intra_signal = std::string();

    void set_intra_event(EdgeType edge, std::string sig) {
        has_intra_event = true;
        intra_edge = edge;
        intra_signal = std::move(sig);
    }

    NonBlockingAssignNode(std::string t, std::unique_ptr<ExpressionNode> r, uint64_t delay = 0)
        : target(std::move(t)), rhs(std::move(r)), intra_delay(delay) {}
};

struct MemAssignNode : public StatementNode {
    std::string mem_name;
    std::unique_ptr<ExpressionNode> addr_expr;
    std::unique_ptr<ExpressionNode> rhs;
    bool is_non_blocking = false;
    uint64_t intra_delay = 0;
    bool has_intra_event = false;
    EdgeType intra_edge = EdgeType::LEVEL;
    std::string intra_signal = std::string();

    void set_intra_event(EdgeType edge, std::string sig) {
        has_intra_event = true;
        intra_edge = edge;
        intra_signal = std::move(sig);
    }

    MemAssignNode(std::string name, std::unique_ptr<ExpressionNode> addr,
                  std::unique_ptr<ExpressionNode> r, bool nba = false, uint64_t delay = 0)
        : mem_name(std::move(name)), addr_expr(std::move(addr)),
          rhs(std::move(r)), is_non_blocking(nba), intra_delay(delay) {}
};

struct ProceduralAssignNode : public StatementNode {
    std::string target;
    std::unique_ptr<ExpressionNode> rhs;

    ProceduralAssignNode(std::string t, std::unique_ptr<ExpressionNode> r)
        : target(std::move(t)), rhs(std::move(r)) {}
};

struct ProceduralDeassignNode : public StatementNode {
    std::string target;

    explicit ProceduralDeassignNode(std::string t)
        : target(std::move(t)) {}
};

struct IfStatementNode : public StatementNode {
    std::unique_ptr<ExpressionNode> condition;
    std::vector<std::unique_ptr<StatementNode>> then_branch;
    std::vector<std::unique_ptr<StatementNode>> else_branch;
    explicit IfStatementNode(std::unique_ptr<ExpressionNode> cond)
        : condition(std::move(cond)) {}
};

struct ForStatementNode : public StatementNode {
    std::unique_ptr<StatementNode> init_stmt;
    std::unique_ptr<ExpressionNode> condition;
    std::unique_ptr<StatementNode> step_stmt;
    std::vector<std::unique_ptr<StatementNode>> body;

    ForStatementNode(std::unique_ptr<StatementNode> init,
                     std::unique_ptr<ExpressionNode> cond,
                     std::unique_ptr<StatementNode> step,
                     std::vector<std::unique_ptr<StatementNode>> b)
        : init_stmt(std::move(init)), condition(std::move(cond)),
          step_stmt(std::move(step)), body(std::move(b)) {}
};

struct WhileStatementNode : public StatementNode {
    std::unique_ptr<ExpressionNode> condition;
    std::vector<std::unique_ptr<StatementNode>> body;

    WhileStatementNode(std::unique_ptr<ExpressionNode> cond,
                       std::vector<std::unique_ptr<StatementNode>> b)
        : condition(std::move(cond)), body(std::move(b)) {}
};

struct RepeatStatementNode : public StatementNode {
    std::unique_ptr<ExpressionNode> count_expr;
    std::vector<std::unique_ptr<StatementNode>> body;

    RepeatStatementNode(std::unique_ptr<ExpressionNode> count,
                        std::vector<std::unique_ptr<StatementNode>> b)
        : count_expr(std::move(count)), body(std::move(b)) {}
};

enum class CaseType {
    CASE,
    CASEZ,
    CASEX
};

struct CaseItem {
    std::unique_ptr<ExpressionNode> match_expr;
    std::vector<std::unique_ptr<ExpressionNode>> match_exprs;
    std::vector<std::unique_ptr<StatementNode>> statements;
    bool is_default = false;
};

struct CaseStatementNode : public StatementNode {
    CaseType case_type = CaseType::CASE;
    std::unique_ptr<ExpressionNode> selector;
    std::vector<CaseItem> items;
    explicit CaseStatementNode(std::unique_ptr<ExpressionNode> sel, CaseType ct = CaseType::CASE)
        : case_type(ct), selector(std::move(sel)) {}
};

enum class JoinType {
    JOIN,
    JOIN_ANY,
    JOIN_NONE
};

struct ForkStatementNode : public StatementNode {
    JoinType join_type = JoinType::JOIN;
    std::vector<std::unique_ptr<StatementNode>> branches;

    explicit ForkStatementNode(JoinType jt, std::vector<std::unique_ptr<StatementNode>> b)
        : join_type(jt), branches(std::move(b)) {}
};

struct DisableForkStatementNode : public StatementNode {
    DisableForkStatementNode() = default;
};

struct WaitForkStatementNode : public StatementNode {
    WaitForkStatementNode() = default;
};

struct EventTriggerStatementNode : public StatementNode {
    std::string event_name;
    bool is_nonblocking = false;

    EventTriggerStatementNode(std::string name, bool nb = false)
        : event_name(std::move(name)), is_nonblocking(nb) {}
};

struct WaitEventTriggeredStatementNode : public StatementNode {
    std::string event_name;
    explicit WaitEventTriggeredStatementNode(std::string name)
        : event_name(std::move(name)) {}
};

struct TaskCallNode : public StatementNode {
    std::string task_name;
    std::vector<std::unique_ptr<ExpressionNode>> args;

    explicit TaskCallNode(std::string name, std::vector<std::unique_ptr<ExpressionNode>> a = {})
        : task_name(std::move(name)), args(std::move(a)) {}
};

struct DelayStatementNode : public StatementNode {
    uint64_t delay;
    std::unique_ptr<ExpressionNode> delay_expr = nullptr;

    explicit DelayStatementNode(uint64_t d, std::unique_ptr<ExpressionNode> expr = nullptr)
        : delay(d), delay_expr(std::move(expr)) {}
};

struct DisplayStatementNode : public StatementNode {
    std::string format_string;
    std::vector<std::unique_ptr<ExpressionNode>> arg_exprs;
    bool is_monitor = false;
    bool has_leading_time = false;
    std::string src_file = "";
    size_t src_line = 0;

    explicit DisplayStatementNode(std::string fmt, std::vector<std::unique_ptr<ExpressionNode>> a,
                                  bool mon = false, bool lead_time = false,
                                  std::string file = "", size_t line = 0)
        : format_string(std::move(fmt)), arg_exprs(std::move(a)), is_monitor(mon),
          has_leading_time(lead_time), src_file(std::move(file)), src_line(line) {}
};

struct FinishStatementNode : public StatementNode {
    std::string src_file = "";
    size_t src_line = 0;

    explicit FinishStatementNode(std::string file = "", size_t line = 0)
        : src_file(std::move(file)), src_line(line) {}
};

struct DumpFileStatementNode : public StatementNode {
    std::string filename;
    explicit DumpFileStatementNode(std::string f) : filename(std::move(f)) {}
};

struct DumpVarsStatementNode : public StatementNode {
    uint32_t levels = 0;
    explicit DumpVarsStatementNode(uint32_t lvl = 0) : levels(lvl) {}
};