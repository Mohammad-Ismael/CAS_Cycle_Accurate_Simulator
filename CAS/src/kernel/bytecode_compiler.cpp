#include "../../include/kernel/bytecode.hpp"
#include "../../include/engine.hpp"
#include "../../include/frontend/ast_expr.hpp"
#include "../../include/frontend/ast_stmt.hpp"
#include "../../include/kernel/elaborator.hpp"
#include <algorithm>

struct BytecodeCompiler::CompileContext {
    Engine& engine;
    std::string scope;
    std::vector<BytecodeInst> instructions;
    bool is_pure_2state_64 = true;
};

bool BytecodeCompiler::compile_node(ExpressionNode* node, CompileContext& ctx, uint32_t& out_width, bool& out_signed) {
    if (!node) return false;

    if (auto id = dynamic_cast<IdentifierNode*>(node)) {
        if (!id->is_bound || id->cached_net_id == UINT32_MAX || id->cached_net_id >= ctx.engine.netlist.size()) {
            return false;
        }
        const auto& net = ctx.engine.netlist[id->cached_net_id];
        out_width = net.width;
        out_signed = net.is_signed;
        if (out_width > 64) {
            ctx.is_pure_2state_64 = false;
        }
        ctx.instructions.emplace_back(Opcode::PUSH_NET_VAL, id->cached_net_id, out_width);
        return true;
    }

    if (auto num = dynamic_cast<NumberNode*>(node)) {
        if (num->is_real) return false;
        out_width = num->width;
        out_signed = num->is_signed;
        if (num->bval != 0 || out_width > 64) {
            ctx.is_pure_2state_64 = false;
        }
        ctx.instructions.emplace_back(Opcode::PUSH_CONST_VAL, out_width, out_signed ? 1 : 0, num->aval,
                                      LogicVal(out_width, num->aval, num->bval, out_signed));
        return true;
    }

    if (auto un = dynamic_cast<UnaryOpNode*>(node)) {
        uint32_t op_w = 0;
        bool op_s = false;
        if (!compile_node(un->operand.get(), ctx, op_w, op_s)) return false;

        BytecodeInst inst;
        inst.arg0 = op_w;
        inst.arg1 = op_s ? 1 : 0;
        if (un->op == "~") {
            inst.op = Opcode::BIT_NOT;
            out_width = op_w;
            out_signed = op_s;
        } else if (un->op == "!") {
            inst.op = Opcode::NOT;
            out_width = 1;
            out_signed = false;
        } else if (un->op == "-") {
            inst.op = Opcode::NEG;
            out_width = op_w;
            out_signed = true;
        } else if (un->op == "+") {
            out_width = op_w;
            out_signed = op_s;
            return true;
        } else if (un->op == "&") {
            inst.op = Opcode::RED_AND;
            out_width = 1;
            out_signed = false;
        } else if (un->op == "~&") {
            inst.op = Opcode::RED_NAND;
            out_width = 1;
            out_signed = false;
        } else if (un->op == "|") {
            inst.op = Opcode::RED_OR;
            out_width = 1;
            out_signed = false;
        } else if (un->op == "~|") {
            inst.op = Opcode::RED_NOR;
            out_width = 1;
            out_signed = false;
        } else if (un->op == "^") {
            inst.op = Opcode::RED_XOR;
            out_width = 1;
            out_signed = false;
        } else if (un->op == "~^" || un->op == "^~") {
            inst.op = Opcode::RED_XNOR;
            out_width = 1;
            out_signed = false;
        } else {
            return false;
        }
        ctx.instructions.push_back(inst);
        return true;
    }

    if (auto bin = dynamic_cast<BinaryOpNode*>(node)) {
        uint32_t lw = 0, rw = 0;
        bool ls = false, rs = false;
        if (!compile_node(bin->lhs.get(), ctx, lw, ls)) return false;
        if (!compile_node(bin->rhs.get(), ctx, rw, rs)) return false;

        bool expr_signed = ls && rs;
        BytecodeInst inst;
        inst.arg0 = std::max(lw, rw);
        inst.arg1 = expr_signed ? 1 : 0;

        if (bin->op == "+") {
            inst.op = Opcode::ADD;
            out_width = std::max(lw, rw);
            out_signed = expr_signed;
        } else if (bin->op == "-") {
            inst.op = Opcode::SUB;
            out_width = std::max(lw, rw);
            out_signed = expr_signed;
        } else if (bin->op == "*") {
            inst.op = Opcode::MUL;
            out_width = std::min(64U, lw + rw);
            out_signed = expr_signed;
        } else if (bin->op == "/") {
            inst.op = Opcode::DIV;
            out_width = lw;
            out_signed = expr_signed;
        } else if (bin->op == "%") {
            inst.op = Opcode::MOD;
            out_width = lw;
            out_signed = expr_signed;
        } else if (bin->op == "&") {
            inst.op = Opcode::AND;
            out_width = std::max(lw, rw);
            out_signed = expr_signed;
        } else if (bin->op == "|") {
            inst.op = Opcode::OR;
            out_width = std::max(lw, rw);
            out_signed = expr_signed;
        } else if (bin->op == "^") {
            inst.op = Opcode::XOR;
            out_width = std::max(lw, rw);
            out_signed = expr_signed;
        } else if (bin->op == "~^" || bin->op == "^~") {
            inst.op = Opcode::XNOR;
            out_width = std::max(lw, rw);
            out_signed = expr_signed;
        } else if (bin->op == "<<") {
            inst.op = Opcode::SHL;
            out_width = lw;
            out_signed = ls;
        } else if (bin->op == ">>") {
            inst.op = Opcode::SHR;
            out_width = lw;
            out_signed = ls;
        } else if (bin->op == ">>>") {
            inst.op = Opcode::SRA;
            inst.arg0 = lw;
            inst.arg1 = ls ? 1 : 0;
            out_width = lw;
            out_signed = ls;
        } else if (bin->op == "==") {
            inst.op = Opcode::EQ;
            out_width = 1;
            out_signed = false;
        } else if (bin->op == "!=") {
            inst.op = Opcode::NE;
            out_width = 1;
            out_signed = false;
        } else if (bin->op == "===") {
            inst.op = Opcode::CASE_EQ;
            out_width = 1;
            out_signed = false;
        } else if (bin->op == "!==") {
            inst.op = Opcode::CASE_NE;
            out_width = 1;
            out_signed = false;
        } else if (bin->op == "<") {
            inst.op = Opcode::LT;
            out_width = 1;
            out_signed = false;
        } else if (bin->op == "<=") {
            inst.op = Opcode::LE;
            out_width = 1;
            out_signed = false;
        } else if (bin->op == ">") {
            inst.op = Opcode::GT;
            out_width = 1;
            out_signed = false;
        } else if (bin->op == ">=") {
            inst.op = Opcode::GE;
            out_width = 1;
            out_signed = false;
        } else if (bin->op == "&&") {
            inst.op = Opcode::LOGIC_AND;
            out_width = 1;
            out_signed = false;
        } else if (bin->op == "||") {
            inst.op = Opcode::LOGIC_OR;
            out_width = 1;
            out_signed = false;
        } else {
            return false;
        }
        ctx.instructions.push_back(inst);
        return true;
    }

    if (auto tern = dynamic_cast<TernaryOpNode*>(node)) {
        uint32_t cw = 0, tw = 0, fw = 0;
        bool cs = false, ts = false, fs = false;
        if (!compile_node(tern->condition.get(), ctx, cw, cs)) return false;
        if (!compile_node(tern->true_expr.get(), ctx, tw, ts)) return false;
        if (!compile_node(tern->false_expr.get(), ctx, fw, fs)) return false;

        out_width = std::max(tw, fw);
        out_signed = ts && fs;
        BytecodeInst inst;
        inst.op = Opcode::TERNARY_MUX;
        inst.arg0 = out_width;
        inst.arg1 = out_signed ? 1 : 0;
        ctx.instructions.push_back(inst);
        return true;
    }

    if (auto cat = dynamic_cast<ConcatNode*>(node)) {
        if (cat->elements.empty()) return false;
        uint32_t total_w = 0;
        for (auto& elem : cat->elements) {
            uint32_t ew = 0;
            bool es = false;
            if (!compile_node(elem.get(), ctx, ew, es)) return false;
            total_w += ew;
        }
        out_width = total_w;
        out_signed = false;
        if (total_w > 64) {
            ctx.is_pure_2state_64 = false;
        }
        BytecodeInst inst;
        inst.op = Opcode::CONCAT;
        inst.arg0 = static_cast<uint32_t>(cat->elements.size());
        inst.arg1 = total_w;
        ctx.instructions.push_back(inst);
        return true;
    }

    return false;
}

CompiledExpr BytecodeCompiler::compile(ExpressionNode* expr, const std::string& scope, Engine& engine, uint32_t target_width, bool target_signed) {
    CompiledExpr res;
    if (!expr) return res;

    CompileContext ctx{engine, scope, {}, true};
    uint32_t out_w = 0;
    bool out_s = false;
    if (compile_node(expr, ctx, out_w, out_s)) {
        ctx.instructions.emplace_back(Opcode::DONE);
        res.instructions = std::move(ctx.instructions);
        res.is_compiled = true;
        if (target_width > 64) {
            ctx.is_pure_2state_64 = false;
        }
        res.is_pure_2state_64 = ctx.is_pure_2state_64;
        res.result_width = out_w;
        res.result_signed = out_s;
    }
    return res;
}

bool BytecodeCompiler::compile_statements(const std::vector<std::unique_ptr<StatementNode>>& stmts, CompileContext& ctx) {
    for (const auto& s : stmts) {
        if (!compile_statement(s.get(), ctx)) return false;
    }
    return true;
}

bool BytecodeCompiler::compile_statement(StatementNode* stmt, CompileContext& ctx) {
    if (!stmt) return true;

    if (auto blk = dynamic_cast<BlockStatementNode*>(stmt)) {
        return compile_statements(blk->statements, ctx);
    }

    if (auto ba = dynamic_cast<BlockingAssignNode*>(stmt)) {
        if (!ba->is_bound || ba->cached_net_id == UINT32_MAX || ba->cached_net_id >= ctx.engine.netlist.size()) {
            return false;
        }
        if (ba->lhs_targets.size() != 1) return false;
        if (ba->bit_idx || ba->bit_lsb || ba->bit_idx2 || ba->bit_lsb2) return false;
        if (ba->stream_lhs || ba->intra_delay > 0 || ba->has_intra_event) return false;

        const auto& target_net = ctx.engine.netlist[ba->cached_net_id];
        if (target_net.has_procedural_assign) return false;

        uint32_t rhs_w = 0;
        bool rhs_s = false;
        if (!compile_node(ba->rhs.get(), ctx, rhs_w, rhs_s)) return false;

        if (target_net.width > 64) {
            ctx.is_pure_2state_64 = false;
        }
        Opcode op = (ctx.is_pure_2state_64 && target_net.width <= 64) ? Opcode::STORE_NET_U64 : Opcode::STORE_NET_VAL;
        ctx.instructions.emplace_back(op, ba->cached_net_id, target_net.width, target_net.is_signed ? 1 : 0);
        return true;
    }

    if (auto if_s = dynamic_cast<IfStatementNode*>(stmt)) {
        uint32_t cond_w = 0;
        bool cond_s = false;
        if (!compile_node(if_s->condition.get(), ctx, cond_w, cond_s)) return false;

        size_t j_false = ctx.instructions.size();
        ctx.instructions.emplace_back(Opcode::JUMP_IF_ZERO, 0);

        if (!compile_statements(if_s->then_branch, ctx)) return false;

        if (!if_s->else_branch.empty()) {
            size_t j_end = ctx.instructions.size();
            ctx.instructions.emplace_back(Opcode::JUMP, 0);

            ctx.instructions[j_false].arg0 = static_cast<uint32_t>(ctx.instructions.size() - j_false);

            if (!compile_statements(if_s->else_branch, ctx)) return false;

            ctx.instructions[j_end].arg0 = static_cast<uint32_t>(ctx.instructions.size() - j_end);
        } else {
            ctx.instructions[j_false].arg0 = static_cast<uint32_t>(ctx.instructions.size() - j_false);
        }
        return true;
    }

    if (auto cs = dynamic_cast<CaseStatementNode*>(stmt)) {
        if (cs->case_type != CaseType::CASE || !cs->selector) return false;

        std::vector<size_t> exit_jumps;
        const CaseItem* default_item = nullptr;

        for (const auto& item : cs->items) {
            if (item.is_default) {
                default_item = &item;
                continue;
            }

            std::vector<ExpressionNode*> match_list;
            if (!item.match_exprs.empty()) {
                for (const auto& me : item.match_exprs) {
                    if (me) match_list.push_back(me.get());
                }
            } else if (item.match_expr) {
                match_list.push_back(item.match_expr.get());
            }

            if (match_list.empty()) return false;

            for (size_t i = 0; i < match_list.size(); ++i) {
                uint32_t sw = 0; bool ss = false;
                if (!compile_node(cs->selector.get(), ctx, sw, ss)) return false;
                uint32_t mw = 0; bool ms = false;
                if (!compile_node(match_list[i], ctx, mw, ms)) return false;
                ctx.instructions.emplace_back(Opcode::CASE_EQ);
                if (i > 0) {
                    ctx.instructions.emplace_back(Opcode::LOGIC_OR);
                }
            }

            size_t j_next = ctx.instructions.size();
            ctx.instructions.emplace_back(Opcode::JUMP_IF_ZERO, 0);

            if (!compile_statements(item.statements, ctx)) return false;

            size_t j_exit = ctx.instructions.size();
            ctx.instructions.emplace_back(Opcode::JUMP, 0);
            exit_jumps.push_back(j_exit);

            ctx.instructions[j_next].arg0 = static_cast<uint32_t>(ctx.instructions.size() - j_next);
        }

        if (default_item) {
            if (!compile_statements(default_item->statements, ctx)) return false;
        }

        for (size_t j : exit_jumps) {
            ctx.instructions[j].arg0 = static_cast<uint32_t>(ctx.instructions.size() - j);
        }
        return true;
    }

    return false;
}

bool BytecodeCompiler::compile_process(BoundProcess& bp, Engine& engine) {
    if (!bp.proc || bp.proc->body.empty()) return false;

    CompileContext ctx{engine, bp.scope, {}, true};
    if (!compile_statements(bp.proc->body, ctx)) {
        return false;
    }

    if (ctx.instructions.empty()) return false;

    ctx.instructions.emplace_back(Opcode::DONE);

    bp.compiled_body.instructions = std::move(ctx.instructions);
    bp.compiled_body.is_compiled = true;
    bp.compiled_body.is_pure_2state_64 = ctx.is_pure_2state_64;
    return true;
}
