#include "../../include/kernel/bytecode.hpp"
#include "../../include/engine.hpp"
#include "../../include/frontend/ast_expr.hpp"
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
