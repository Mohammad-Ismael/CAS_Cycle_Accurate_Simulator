#include "../../include/kernel/expr_evaluator.hpp"
#include "../../include/engine.hpp"
#include "../../include/kernel/logic_ops.hpp"
#include <algorithm>

ExprEvaluator::ExprEvaluator(Engine& eng) : engine(eng) {}

LogicVal ExprEvaluator::eval(ExpressionNode* expr, const std::string& scope) {
    if (!expr) return LogicVal::make_x(1);

    if (auto num = dynamic_cast<NumberNode*>(expr)) {
        return LogicVal(num->width, num->aval, num->bval);
    }

    if (auto ident = dynamic_cast<IdentifierNode*>(expr)) {
        if (ident->name == "$time") return LogicVal::make_known(64, engine.get_sim_time());
        std::string scoped_ident = scope.empty() ? ident->name : (scope + "." + ident->name);
        if (engine.param_map.find(scoped_ident) != engine.param_map.end()) {
            return LogicVal::make_known(32, engine.param_map.at(scoped_ident));
        }
        if (engine.net_map.find(scoped_ident) != engine.net_map.end()) {
            return engine.netlist[engine.net_map.at(scoped_ident)].value;
        }
        if (engine.param_map.find(ident->name) != engine.param_map.end()) {
            return LogicVal::make_known(32, engine.param_map.at(ident->name));
        }
        return LogicVal::make_x(1);
    }

    if (auto rep = dynamic_cast<ReplicateNode*>(expr)) {
        LogicVal count_val = eval(rep->count_expr.get(), scope);
        uint64_t count = count_val.is_unknown() ? 0 : count_val.aval;
        LogicVal t_val = eval(rep->target_expr.get(), scope);

        if (count == 0 || t_val.width == 0) return LogicVal(1, 0, 0);
        uint32_t total_w = t_val.width * (uint32_t)count;
        uint64_t new_a = 0;
        uint64_t new_b = 0;
        for (uint64_t i = 0; i < count; ++i) {
            new_a = (new_a << t_val.width) | t_val.aval;
            new_b = (new_b << t_val.width) | t_val.bval;
        }
        return LogicVal(total_w, new_a, new_b);
    }

    if (auto cat = dynamic_cast<ConcatNode*>(expr)) {
        uint32_t total_w = 0;
        std::vector<LogicVal> vals;
        for (auto& elem : cat->elements) {
            LogicVal v = eval(elem.get(), scope);
            vals.push_back(v);
            total_w += v.width;
        }

        uint64_t new_a = 0;
        uint64_t new_b = 0;
        int shift = (int)total_w;

        for (const auto& v : vals) {
            shift -= v.width;
            uint64_t mask = (v.width >= 64) ? ~0ULL : ((1ULL << v.width) - 1);
            if (shift >= 0) {
                new_a |= ((v.aval & mask) << shift);
                new_b |= ((v.bval & mask) << shift);
            }
        }
        return LogicVal(total_w, new_a, new_b);
    }

    if (auto idx_node = dynamic_cast<IndexSelectNode*>(expr)) {
        std::string scoped_ident = scope.empty() ? idx_node->name : (scope + "." + idx_node->name);
        if (engine.memory_map.find(scoped_ident) != engine.memory_map.end()) {
            const auto& mem = engine.memory_map.at(scoped_ident);
            LogicVal addr_val = eval(idx_node->msb_expr.get(), scope);
            if (addr_val.is_unknown() || addr_val.aval >= mem.depth) {
                return LogicVal::make_x(mem.word_width);
            }
            return mem.words[addr_val.aval];
        }

        const auto& target_val = engine.netlist[engine.net_map.at(scoped_ident)].value;
        if (!idx_node->is_part_select) {
            LogicVal idx_val = eval(idx_node->msb_expr.get(), scope);
            if (idx_val.is_unknown() || idx_val.aval >= target_val.width) return LogicVal::make_x(1);
            uint32_t sh = idx_val.aval;
            return LogicVal(1, (target_val.aval >> sh) & 1, (target_val.bval >> sh) & 1);
        } else {
            LogicVal msb_v = eval(idx_node->msb_expr.get(), scope);
            LogicVal lsb_v = eval(idx_node->lsb_expr.get(), scope);
            if (msb_v.is_unknown() || lsb_v.is_unknown() || msb_v.aval < lsb_v.aval) {
                uint32_t w = (msb_v.aval >= lsb_v.aval) ? (msb_v.aval - lsb_v.aval + 1) : 1;
                return LogicVal::make_x(w);
            }
            uint32_t msb = msb_v.aval;
            uint32_t lsb = lsb_v.aval;
            uint32_t slice_w = msb - lsb + 1;
            uint64_t mask = (slice_w >= 64) ? ~0ULL : ((1ULL << slice_w) - 1);
            return LogicVal(slice_w, (target_val.aval >> lsb) & mask, (target_val.bval >> lsb) & mask);
        }
    }

    if (auto un = dynamic_cast<UnaryOpNode*>(expr)) {
        LogicVal val = eval(un->operand.get(), scope);
        if (un->op == '!') {
            if (val.is_unknown()) return LogicVal::make_x(1);
            return LogicVal::make_known(1, (val.aval == 0) ? 1 : 0);
        }
        if (un->op == '~') return LogicOps::bitwise_not(val);
        if (un->op == '+') return val;
        if (un->op == '-') {
            if (val.is_unknown()) return LogicVal::make_x(val.width);
            uint64_t mask = (val.width >= 64) ? ~0ULL : ((1ULL << val.width) - 1);
            uint64_t neg_a = (~val.aval + 1) & mask;
            return LogicVal(val.width, neg_a, 0);
        }
    }

    if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
        LogicVal l = eval(bin->lhs.get(), scope);
        LogicVal r = eval(bin->rhs.get(), scope);

        bool l_signed = false;
        if (auto id = dynamic_cast<IdentifierNode*>(bin->lhs.get())) {
            std::string full = scope.empty() ? id->name : (scope + "." + id->name);
            if (engine.net_map.find(full) != engine.net_map.end()) {
                l_signed = engine.netlist[engine.net_map.at(full)].is_signed;
            }
        }
        bool r_signed = false;
        if (auto id = dynamic_cast<IdentifierNode*>(bin->rhs.get())) {
            std::string full = scope.empty() ? id->name : (scope + "." + id->name);
            if (engine.net_map.find(full) != engine.net_map.end()) {
                r_signed = engine.netlist[engine.net_map.at(full)].is_signed;
            }
        }

        bool expr_signed = l_signed && r_signed;

        if (bin->op == "+") {
            uint32_t w = std::min(64U, std::max(l.width, r.width) + 1);
            LogicVal ext_l = LogicOps::cast_width(l, w, expr_signed);
            LogicVal ext_r = LogicOps::cast_width(r, w, expr_signed);
            if (ext_l.is_unknown() || ext_r.is_unknown()) return LogicVal::make_x(w);
            return LogicVal::make_known(w, ext_l.aval + ext_r.aval);
        }
        if (bin->op == "-") {
            uint32_t w = std::min(64U, std::max(l.width, r.width) + 1);
            LogicVal ext_l = LogicOps::cast_width(l, w, expr_signed);
            LogicVal ext_r = LogicOps::cast_width(r, w, expr_signed);
            if (ext_l.is_unknown() || ext_r.is_unknown()) return LogicVal::make_x(w);
            return LogicVal::make_known(w, ext_l.aval - ext_r.aval);
        }
        if (bin->op == "*") {
            uint32_t w = std::min(64U, l.width + r.width);
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(w);
            return LogicVal::make_known(w, l.aval * r.aval);
        }
        if (bin->op == "&")   return LogicOps::bitwise_and(l, r);
        if (bin->op == "|")   return LogicOps::bitwise_or(l, r);
        if (bin->op == "^")   return LogicOps::bitwise_xor(l, r);
        if (bin->op == "<<")  return LogicOps::shift_left(l, r);
        if (bin->op == ">>")  return LogicOps::shift_right(l, r);
        if (bin->op == ">>>") return LogicOps::arith_shift_right(l, r, l_signed);
        if (bin->op == "==")  return LogicOps::logical_eq(l, r);
        if (bin->op == "!=")  return LogicOps::logical_neq(l, r);
        if (bin->op == "===") return LogicOps::case_eq(l, r);
        if (bin->op == "!==") return LogicOps::case_neq(l, r);
        if (bin->op == "<") {
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
            if (expr_signed) {
                int64_t sl = (int64_t)LogicOps::cast_width(l, 64, true).aval;
                int64_t sr = (int64_t)LogicOps::cast_width(r, 64, true).aval;
                return LogicVal::make_known(1, (sl < sr) ? 1 : 0);
            }
            return LogicVal::make_known(1, (l.aval < r.aval) ? 1 : 0);
        }
        if (bin->op == "<=") {
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
            if (expr_signed) {
                int64_t sl = (int64_t)LogicOps::cast_width(l, 64, true).aval;
                int64_t sr = (int64_t)LogicOps::cast_width(r, 64, true).aval;
                return LogicVal::make_known(1, (sl <= sr) ? 1 : 0);
            }
            return LogicVal::make_known(1, (l.aval <= r.aval) ? 1 : 0);
        }
        if (bin->op == ">") {
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
            if (expr_signed) {
                int64_t sl = (int64_t)LogicOps::cast_width(l, 64, true).aval;
                int64_t sr = (int64_t)LogicOps::cast_width(r, 64, true).aval;
                return LogicVal::make_known(1, (sl > sr) ? 1 : 0);
            }
            return LogicVal::make_known(1, (l.aval > r.aval) ? 1 : 0);
        }
        if (bin->op == ">=") {
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
            if (expr_signed) {
                int64_t sl = (int64_t)LogicOps::cast_width(l, 64, true).aval;
                int64_t sr = (int64_t)LogicOps::cast_width(r, 64, true).aval;
                return LogicVal::make_known(1, (sl >= sr) ? 1 : 0);
            }
            return LogicVal::make_known(1, (l.aval >= r.aval) ? 1 : 0);
        }
        if (bin->op == "&&") {
            if ((!l.is_unknown() && l.aval == 0) || (!r.is_unknown() && r.aval == 0)) return LogicVal::make_known(1, 0);
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
            return LogicVal::make_known(1, (l.aval != 0 && r.aval != 0) ? 1 : 0);
        }
        if (bin->op == "||") {
            if ((!l.is_unknown() && l.aval != 0) || (!r.is_unknown() && r.aval != 0)) return LogicVal::make_known(1, 1);
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
            return LogicVal::make_known(1, (l.aval != 0 || r.aval != 0) ? 1 : 0);
        }
    }

    if (auto tern = dynamic_cast<TernaryOpNode*>(expr)) {
        LogicVal cond = eval(tern->condition.get(), scope);
        LogicVal t_val = eval(tern->true_expr.get(), scope);
        LogicVal f_val = eval(tern->false_expr.get(), scope);
        return LogicOps::ternary(cond, t_val, f_val);
    }

    return LogicVal::make_x(1);
}