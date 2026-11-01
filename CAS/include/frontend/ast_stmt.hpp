#pragma once
#include "ast_base.hpp"
#include "ast_expr.hpp"
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

    std::unique_ptr<StatementNode> clone() const override {
        return std::make_unique<EventControlStatementNode>(edge, signal_name);
    }
};

struct ForeverStatementNode : public StatementNode {
    std::vector<std::unique_ptr<StatementNode>> body;

    explicit ForeverStatementNode(std::vector<std::unique_ptr<StatementNode>> b)
        : body(std::move(b)) {}

    std::unique_ptr<StatementNode> clone() const override {
        std::vector<std::unique_ptr<StatementNode>> b;
        for (const auto& s : body) b.push_back(s ? s->clone() : nullptr);
        return std::make_unique<ForeverStatementNode>(std::move(b));
    }
};

struct BlockStatementNode : public StatementNode {
    std::vector<std::unique_ptr<StatementNode>> statements;

    BlockStatementNode() = default;
    explicit BlockStatementNode(std::vector<std::unique_ptr<StatementNode>> stmts)
        : statements(std::move(stmts)) {}

    std::unique_ptr<StatementNode> clone() const override {
        std::vector<std::unique_ptr<StatementNode>> stmts;
        for (const auto& s : statements) stmts.push_back(s ? s->clone() : nullptr);
        return std::make_unique<BlockStatementNode>(std::move(stmts));
    }
};

struct VarDeclStatementNode : public StatementNode {
    std::string var_name;
    uint32_t width = 32;
    bool is_signed = false;
    std::string type_name = "";
    bool is_queue = false;
    bool is_dynamic_array = false;
    bool is_associative_array = false;
    std::string assoc_key_type = "";
    uint32_t assoc_key_width = 32;
    bool is_class_handle = false;
    std::string class_type_name = "";
    bool is_covergroup_handle = false;
    std::string covergroup_type_name = "";
    bool is_chandle = false;
    bool is_semaphore = false;
    bool is_mailbox = false;
    bool is_virtual_interface = false;
    std::string interface_name = "";
    std::string modport_name = "";
    std::unique_ptr<ExpressionNode> init_expr = nullptr;

    VarDeclStatementNode(std::string n, uint32_t w = 32, bool s = false, std::string t = "", bool q = false, bool dyn = false, bool assoc = false, std::string key_t = "", uint32_t key_w = 32)
        : var_name(std::move(n)), width(w), is_signed(s), type_name(std::move(t)), is_queue(q), is_dynamic_array(dyn), is_associative_array(assoc), assoc_key_type(std::move(key_t)), assoc_key_width(key_w) {}

    std::unique_ptr<StatementNode> clone() const override {
        auto node = std::make_unique<VarDeclStatementNode>(var_name, width, is_signed, type_name, is_queue, is_dynamic_array, is_associative_array, assoc_key_type, assoc_key_width);
        node->is_class_handle = is_class_handle;
        node->class_type_name = class_type_name;
        node->is_covergroup_handle = is_covergroup_handle;
        node->covergroup_type_name = covergroup_type_name;
        node->is_chandle = is_chandle;
        node->is_semaphore = is_semaphore;
        node->is_mailbox = is_mailbox;
        node->is_virtual_interface = is_virtual_interface;
        node->interface_name = interface_name;
        node->modport_name = modport_name;
        node->init_expr = init_expr ? init_expr->clone() : nullptr;
        return node;
    }
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

    std::unique_ptr<StatementNode> clone() const override {
        auto node = std::make_unique<BlockingAssignNode>(lhs_targets, rhs ? rhs->clone() : nullptr, intra_delay);
        node->bit_idx = bit_idx ? bit_idx->clone() : nullptr;
        node->has_intra_event = has_intra_event;
        node->intra_edge = intra_edge;
        node->intra_signal = intra_signal;
        return node;
    }
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

    std::unique_ptr<StatementNode> clone() const override {
        auto node = std::make_unique<NonBlockingAssignNode>(target, rhs ? rhs->clone() : nullptr, intra_delay);
        node->has_intra_event = has_intra_event;
        node->intra_edge = intra_edge;
        node->intra_signal = intra_signal;
        return node;
    }
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

    std::unique_ptr<StatementNode> clone() const override {
        auto node = std::make_unique<MemAssignNode>(mem_name, addr_expr ? addr_expr->clone() : nullptr,
                                                    rhs ? rhs->clone() : nullptr, is_non_blocking, intra_delay);
        node->has_intra_event = has_intra_event;
        node->intra_edge = intra_edge;
        node->intra_signal = intra_signal;
        return node;
    }
};

struct ProceduralAssignNode : public StatementNode {
    std::string target;
    std::unique_ptr<ExpressionNode> rhs;

    ProceduralAssignNode(std::string t, std::unique_ptr<ExpressionNode> r)
        : target(std::move(t)), rhs(std::move(r)) {}

    std::unique_ptr<StatementNode> clone() const override {
        return std::make_unique<ProceduralAssignNode>(target, rhs ? rhs->clone() : nullptr);
    }
};

struct ProceduralDeassignNode : public StatementNode {
    std::string target;

    explicit ProceduralDeassignNode(std::string t)
        : target(std::move(t)) {}

    std::unique_ptr<StatementNode> clone() const override {
        return std::make_unique<ProceduralDeassignNode>(target);
    }
};

struct IfStatementNode : public StatementNode {
    std::unique_ptr<ExpressionNode> condition;
    std::vector<std::unique_ptr<StatementNode>> then_branch;
    std::vector<std::unique_ptr<StatementNode>> else_branch;
    explicit IfStatementNode(std::unique_ptr<ExpressionNode> cond)
        : condition(std::move(cond)) {}

    std::unique_ptr<StatementNode> clone() const override {
        auto node = std::make_unique<IfStatementNode>(condition ? condition->clone() : nullptr);
        for (const auto& s : then_branch) node->then_branch.push_back(s ? s->clone() : nullptr);
        for (const auto& s : else_branch) node->else_branch.push_back(s ? s->clone() : nullptr);
        return node;
    }
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

    std::unique_ptr<StatementNode> clone() const override {
        std::vector<std::unique_ptr<StatementNode>> b;
        for (const auto& s : body) b.push_back(s ? s->clone() : nullptr);
        return std::make_unique<ForStatementNode>(init_stmt ? init_stmt->clone() : nullptr,
                                                 condition ? condition->clone() : nullptr,
                                                 step_stmt ? step_stmt->clone() : nullptr,
                                                 std::move(b));
    }
};

struct ForeachStatementNode : public StatementNode {
    std::string array_name;
    std::string loop_var;
    std::vector<std::unique_ptr<StatementNode>> body;

    ForeachStatementNode(std::string arr, std::string lvar, std::vector<std::unique_ptr<StatementNode>> b)
        : array_name(std::move(arr)), loop_var(std::move(lvar)), body(std::move(b)) {}

    std::unique_ptr<StatementNode> clone() const override {
        std::vector<std::unique_ptr<StatementNode>> b;
        for (const auto& s : body) b.push_back(s ? s->clone() : nullptr);
        return std::make_unique<ForeachStatementNode>(array_name, loop_var, std::move(b));
    }
};

struct WhileStatementNode : public StatementNode {
    std::unique_ptr<ExpressionNode> condition;
    std::vector<std::unique_ptr<StatementNode>> body;

    WhileStatementNode(std::unique_ptr<ExpressionNode> cond,
                       std::vector<std::unique_ptr<StatementNode>> b)
        : condition(std::move(cond)), body(std::move(b)) {}

    std::unique_ptr<StatementNode> clone() const override {
        std::vector<std::unique_ptr<StatementNode>> b;
        for (const auto& s : body) b.push_back(s ? s->clone() : nullptr);
        return std::make_unique<WhileStatementNode>(condition ? condition->clone() : nullptr, std::move(b));
    }
};

struct RepeatStatementNode : public StatementNode {
    std::unique_ptr<ExpressionNode> count_expr;
    std::vector<std::unique_ptr<StatementNode>> body;

    RepeatStatementNode(std::unique_ptr<ExpressionNode> count,
                        std::vector<std::unique_ptr<StatementNode>> b)
        : count_expr(std::move(count)), body(std::move(b)) {}

    std::unique_ptr<StatementNode> clone() const override {
        std::vector<std::unique_ptr<StatementNode>> b;
        for (const auto& s : body) b.push_back(s ? s->clone() : nullptr);
        return std::make_unique<RepeatStatementNode>(count_expr ? count_expr->clone() : nullptr, std::move(b));
    }
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

    std::unique_ptr<StatementNode> clone() const override {
        auto node = std::make_unique<CaseStatementNode>(selector ? selector->clone() : nullptr, case_type);
        for (const auto& item : items) {
            CaseItem ci;
            ci.is_default = item.is_default;
            ci.match_expr = item.match_expr ? item.match_expr->clone() : nullptr;
            for (const auto& me : item.match_exprs) ci.match_exprs.push_back(me ? me->clone() : nullptr);
            for (const auto& st : item.statements) ci.statements.push_back(st ? st->clone() : nullptr);
            node->items.push_back(std::move(ci));
        }
        return node;
    }
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

    std::unique_ptr<StatementNode> clone() const override {
        std::vector<std::unique_ptr<StatementNode>> br;
        for (const auto& s : branches) br.push_back(s ? s->clone() : nullptr);
        return std::make_unique<ForkStatementNode>(join_type, std::move(br));
    }
};

struct DisableForkStatementNode : public StatementNode {
    DisableForkStatementNode() = default;

    std::unique_ptr<StatementNode> clone() const override {
        return std::make_unique<DisableForkStatementNode>();
    }
};

struct WaitForkStatementNode : public StatementNode {
    WaitForkStatementNode() = default;

    std::unique_ptr<StatementNode> clone() const override {
        return std::make_unique<WaitForkStatementNode>();
    }
};

struct EventTriggerStatementNode : public StatementNode {
    std::string event_name;
    bool is_nonblocking = false;

    EventTriggerStatementNode(std::string name, bool nb = false)
        : event_name(std::move(name)), is_nonblocking(nb) {}

    std::unique_ptr<StatementNode> clone() const override {
        return std::make_unique<EventTriggerStatementNode>(event_name, is_nonblocking);
    }
};

struct WaitEventTriggeredStatementNode : public StatementNode {
    std::string event_name;
    explicit WaitEventTriggeredStatementNode(std::string name)
        : event_name(std::move(name)) {}

    std::unique_ptr<StatementNode> clone() const override {
        return std::make_unique<WaitEventTriggeredStatementNode>(event_name);
    }
};

struct TaskCallNode : public StatementNode {
    std::string task_name;
    std::vector<std::unique_ptr<ExpressionNode>> args;
    std::string class_type_name = "";
    std::vector<std::string> arg_names = {};

    explicit TaskCallNode(std::string name, std::vector<std::unique_ptr<ExpressionNode>> a = {}, std::string cls_type = "", std::vector<std::string> anames = {})
        : task_name(std::move(name)), args(std::move(a)), class_type_name(std::move(cls_type)), arg_names(std::move(anames)) {}

    std::unique_ptr<StatementNode> clone() const override {
        std::vector<std::unique_ptr<ExpressionNode>> a;
        for (const auto& arg : args) a.push_back(arg ? arg->clone() : nullptr);
        return std::make_unique<TaskCallNode>(task_name, std::move(a), class_type_name, arg_names);
    }
};

struct DelayStatementNode : public StatementNode {
    uint64_t delay;
    std::unique_ptr<ExpressionNode> delay_expr = nullptr;

    explicit DelayStatementNode(uint64_t d, std::unique_ptr<ExpressionNode> expr = nullptr)
        : delay(d), delay_expr(std::move(expr)) {}

    std::unique_ptr<StatementNode> clone() const override {
        return std::make_unique<DelayStatementNode>(delay, delay_expr ? delay_expr->clone() : nullptr);
    }
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

    std::unique_ptr<StatementNode> clone() const override {
        std::vector<std::unique_ptr<ExpressionNode>> a;
        for (const auto& arg : arg_exprs) a.push_back(arg ? arg->clone() : nullptr);
        return std::make_unique<DisplayStatementNode>(format_string, std::move(a), is_monitor, has_leading_time, src_file, src_line);
    }
};

struct FinishStatementNode : public StatementNode {
    std::string src_file = "";
    size_t src_line = 0;

    explicit FinishStatementNode(std::string file = "", size_t line = 0)
        : src_file(std::move(file)), src_line(line) {}

    std::unique_ptr<StatementNode> clone() const override {
        return std::make_unique<FinishStatementNode>(src_file, src_line);
    }
};

struct ExitStatementNode : public StatementNode {
    std::string src_file = "";
    size_t src_line = 0;

    explicit ExitStatementNode(std::string file = "", size_t line = 0)
        : src_file(std::move(file)), src_line(line) {}

    std::unique_ptr<StatementNode> clone() const override {
        return std::make_unique<ExitStatementNode>(src_file, src_line);
    }
};

struct DumpFileStatementNode : public StatementNode {
    std::string filename;
    explicit DumpFileStatementNode(std::string f) : filename(std::move(f)) {}

    std::unique_ptr<StatementNode> clone() const override {
        return std::make_unique<DumpFileStatementNode>(filename);
    }
};

struct DumpVarsStatementNode : public StatementNode {
    uint32_t levels = 0;
    explicit DumpVarsStatementNode(uint32_t lvl = 0) : levels(lvl) {}

    std::unique_ptr<StatementNode> clone() const override {
        return std::make_unique<DumpVarsStatementNode>(levels);
    }
};

struct CycleDelayStatementNode : public StatementNode {
    std::unique_ptr<ExpressionNode> cycle_expr;
    std::string clocking_name;

    explicit CycleDelayStatementNode(std::unique_ptr<ExpressionNode> expr = nullptr, std::string cb = "")
        : cycle_expr(std::move(expr)), clocking_name(std::move(cb)) {}

    std::unique_ptr<StatementNode> clone() const override {
        return std::make_unique<CycleDelayStatementNode>(cycle_expr ? cycle_expr->clone() : nullptr, clocking_name);
    }
};

struct ReturnStatementNode : public StatementNode {
    std::unique_ptr<ExpressionNode> expr;
    explicit ReturnStatementNode(std::unique_ptr<ExpressionNode> e = nullptr) : expr(std::move(e)) {}

    std::unique_ptr<StatementNode> clone() const override {
        return std::make_unique<ReturnStatementNode>(expr ? expr->clone() : nullptr);
    }
};

struct SysCastStatementNode : public StatementNode {
    std::unique_ptr<ExpressionNode> dest_expr;
    std::unique_ptr<ExpressionNode> src_expr;

    SysCastStatementNode(std::unique_ptr<ExpressionNode> d, std::unique_ptr<ExpressionNode> s)
        : dest_expr(std::move(d)), src_expr(std::move(s)) {}

    std::unique_ptr<StatementNode> clone() const override {
        return std::make_unique<SysCastStatementNode>(dest_expr ? dest_expr->clone() : nullptr, src_expr ? src_expr->clone() : nullptr);
    }
};

struct RandomizeStatementNode : public StatementNode {
    std::unique_ptr<ExpressionNode> call;

    explicit RandomizeStatementNode(std::unique_ptr<ExpressionNode> c)
        : call(std::move(c)) {}

    std::unique_ptr<StatementNode> clone() const override {
        return std::make_unique<RandomizeStatementNode>(call ? call->clone() : nullptr);
    }
};
