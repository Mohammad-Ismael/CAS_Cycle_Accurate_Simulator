#include "../../include/kernel/expr_evaluator.hpp"
#include "../../include/engine.hpp"
#include "../../include/kernel/logic_ops.hpp"
#include <algorithm>

ExprEvaluator::ExprEvaluator(Engine& eng) : engine(eng) {}

bool ExprEvaluator::eval_function(const std::string& func_name, const std::vector<LogicVal>& arg_vals, LogicVal& result_out) {
    FunctionNode* target_fn = nullptr;
    if (engine.function_map.find(func_name) != engine.function_map.end()) {
        target_fn = engine.function_map.at(func_name);
    }
    if (!target_fn) return false;

    std::string frame_name = func_name + "__call";
    context.push_frame(frame_name);

    // 1. Declare and initialize function return variable
    context.declare_local(target_fn->name, target_fn->return_width, target_fn->return_signed);

    // 2. Declare and initialize input parameters
    for (size_t i = 0; i < target_fn->inputs.size(); ++i) {
        const auto& in_p = target_fn->inputs[i];
        uint32_t w = 1;
        if (in_p.msb_expr && in_p.lsb_expr) {
            auto msb_n = dynamic_cast<NumberNode*>(in_p.msb_expr.get());
            auto lsb_n = dynamic_cast<NumberNode*>(in_p.lsb_expr.get());
            if (msb_n && lsb_n) {
                w = (msb_n->aval >= lsb_n->aval) ? (msb_n->aval - lsb_n->aval + 1) : (lsb_n->aval - msb_n->aval + 1);
            }
        }
        context.declare_local(in_p.name, w, in_p.is_signed);
        if (i < arg_vals.size()) {
            context.set_local_value(in_p.name, arg_vals[i]);
        }
    }

    // 3. Execute body statements directly through Evaluator
    BoundProcess bp{frame_name, nullptr, 0, false};
    for (auto& s : target_fn->body) {
        engine.evaluator.execute_statement(s.get(), bp);
    }

    // 4. Retrieve return variable
    LocalVar* ret_var = context.lookup_local(target_fn->name);
    if (ret_var) {
        result_out = ret_var->value;
    } else {
        result_out = LogicVal::make_x(target_fn->return_width);
    }

    context.pop_frame();
    return true;
}

LogicVal ExprEvaluator::eval(ExpressionNode* expr, const std::string& scope) {
    if (!expr) return LogicVal::make_x(1);

    if (auto num = dynamic_cast<NumberNode*>(expr)) {
        return LogicVal(num->width, num->aval, num->bval);
    }

    if (auto fn_call = dynamic_cast<FunctionCallNode*>(expr)) {
        std::vector<LogicVal> evaluated_args;
        for (const auto& a : fn_call->args) {
            evaluated_args.push_back(eval(a.get(), scope));
        }

        std::string scoped_fn = scope.empty() ? fn_call->func_name : (scope + "." + fn_call->func_name);
        LogicVal ret(32, 0, 0);

        if (eval_function(scoped_fn, evaluated_args, ret)) {
            return ret;
        }
        if (eval_function(fn_call->func_name, evaluated_args, ret)) {
            return ret;
        }
        return LogicVal::make_x(32);
    }

    if (auto ident = dynamic_cast<IdentifierNode*>(expr)) {
        if (ident->name == "$time") return LogicVal::make_known(64, engine.get_sim_time());

        // Check local activation frame first
        LocalVar* local = context.lookup_local(ident->name);
        if (local) return local->value;

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
        if (engine.net_map.find(ident->name) != engine.net_map.end()) {
            return engine.netlist[engine.net_map.at(ident->name)].value;
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
        LogicVal target_val = LogicVal::make_x(1);

        LocalVar* local = context.lookup_local(idx_node->name);
        if (local) {
            target_val = local->value;
        } else {
            std::string scoped_ident = scope.empty() ? idx_node->name : (scope + "." + idx_node->name);
            if (engine.memory_map.find(scoped_ident) != engine.memory_map.end()) {
                const auto& mem = engine.memory_map.at(scoped_ident);
                LogicVal addr_val = eval(idx_node->msb_expr.get(), scope);
                if (addr_val.is_unknown() || addr_val.aval >= mem.depth) {
                    return LogicVal::make_x(mem.word_width);
                }
                return mem.words[addr_val.aval];
            }

            if (engine.net_map.find(scoped_ident) == engine.net_map.end()) {
                if (engine.net_map.find(idx_node->name) != engine.net_map.end()) {
                    scoped_ident = idx_node->name;
                } else {
                    return LogicVal::make_x(1);
                }
            }
            target_val = engine.netlist[engine.net_map.at(scoped_ident)].value;
        }

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
            LocalVar* loc = context.lookup_local(id->name);
            if (loc) {
                l_signed = loc->is_signed;
            } else {
                std::string full = scope.empty() ? id->name : (scope + "." + id->name);
                if (engine.net_map.find(full) != engine.net_map.end()) {
                    l_signed = engine.netlist[engine.net_map.at(full)].is_signed;
                }
            }
        }
        bool r_signed = false;
        if (auto id = dynamic_cast<IdentifierNode*>(bin->rhs.get())) {
            LocalVar* loc = context.lookup_local(id->name);
            if (loc) {
                r_signed = loc->is_signed;
            } else {
                std::string full = scope.empty() ? id->name : (scope + "." + id->name);
                if (engine.net_map.find(full) != engine.net_map.end()) {
                    r_signed = engine.netlist[engine.net_map.at(full)].is_signed;
                }
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