#include "../../include/kernel/evaluator.hpp"
#include "../../include/engine.hpp"
#include "../../include/kernel/logic_ops.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm>

Evaluator::Evaluator(Engine& eng) : engine(eng) {}

LogicVal Evaluator::read_slice(const NetSliceRef& ref) {
    const auto& net_val = engine.netlist[ref.net_id].value;
    uint64_t mask = (ref.width >= 64) ? ~0ULL : ((1ULL << ref.width) - 1);
    uint64_t a = (net_val.aval >> ref.offset) & mask;
    uint64_t b = (net_val.bval >> ref.offset) & mask;
    return LogicVal(ref.width, a, b);
}

void Evaluator::write_slice(const NetSliceRef& ref, const LogicVal& val) {
    auto cur = engine.netlist[ref.net_id].value;
    uint64_t mask = (ref.width >= 64) ? ~0ULL : ((1ULL << ref.width) - 1);
    uint64_t clear_mask = ~(mask << ref.offset);

    uint64_t new_a = (cur.aval & clear_mask) | ((val.aval & mask) << ref.offset);
    uint64_t new_b = (cur.bval & clear_mask) | ((val.bval & mask) << ref.offset);
    engine.netlist[ref.net_id].set(LogicVal(cur.width, new_a, new_b));
}

LogicVal Evaluator::eval_expr(ExpressionNode* expr, const std::string& scope) {
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
        LogicVal count_val = eval_expr(rep->count_expr.get(), scope);
        uint64_t count = count_val.is_unknown() ? 0 : count_val.aval;
        LogicVal t_val = eval_expr(rep->target_expr.get(), scope);

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
            LogicVal v = eval_expr(elem.get(), scope);
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
            LogicVal addr_val = eval_expr(idx_node->msb_expr.get(), scope);
            if (addr_val.is_unknown() || addr_val.aval >= mem.depth) {
                return LogicVal::make_x(mem.word_width);
            }
            return mem.words[addr_val.aval];
        }

        const auto& target_val = engine.netlist[engine.net_map.at(scoped_ident)].value;
        if (!idx_node->is_part_select) {
            LogicVal idx_val = eval_expr(idx_node->msb_expr.get(), scope);
            if (idx_val.is_unknown() || idx_val.aval >= target_val.width) return LogicVal::make_x(1);
            uint32_t sh = idx_val.aval;
            return LogicVal(1, (target_val.aval >> sh) & 1, (target_val.bval >> sh) & 1);
        } else {
            LogicVal msb_v = eval_expr(idx_node->msb_expr.get(), scope);
            LogicVal lsb_v = eval_expr(idx_node->lsb_expr.get(), scope);
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
        LogicVal val = eval_expr(un->operand.get(), scope);
        if (un->op == '!') {
            if (val.is_unknown()) return LogicVal::make_x(1);
            return LogicVal::make_known(1, (val.aval == 0) ? 1 : 0);
        }
        if (un->op == '~') return LogicOps::bitwise_not(val);
    }
    if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
        LogicVal l = eval_expr(bin->lhs.get(), scope);
        LogicVal r = eval_expr(bin->rhs.get(), scope);

        if (bin->op == "+") {
            uint32_t w = std::min(64U, std::max(l.width, r.width) + 1);
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(w);
            return LogicVal::make_known(w, l.aval + r.aval);
        }
        if (bin->op == "-") {
            uint32_t w = std::min(64U, std::max(l.width, r.width) + 1);
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(w);
            return LogicVal::make_known(w, l.aval - r.aval);
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
        if (bin->op == "==")  return LogicOps::logical_eq(l, r);
        if (bin->op == "!=")  return LogicOps::logical_neq(l, r);
        if (bin->op == "===") return LogicOps::case_eq(l, r);
        if (bin->op == "!==") return LogicOps::case_neq(l, r);
        if (bin->op == "<") {
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
            return LogicVal::make_known(1, (l.aval < r.aval) ? 1 : 0);
        }
        if (bin->op == "<=") {
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
            return LogicVal::make_known(1, (l.aval <= r.aval) ? 1 : 0);
        }
        if (bin->op == ">") {
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
            return LogicVal::make_known(1, (l.aval > r.aval) ? 1 : 0);
        }
        if (bin->op == ">=") {
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
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
        LogicVal cond = eval_expr(tern->condition.get(), scope);
        LogicVal t_val = eval_expr(tern->true_expr.get(), scope);
        LogicVal f_val = eval_expr(tern->false_expr.get(), scope);
        return LogicOps::ternary(cond, t_val, f_val);
    }
    return LogicVal::make_x(1);
}

void Evaluator::check_monitor() {
    if (!engine.monitor_session.active) return;

    bool values_changed = false;
    std::vector<LogicVal> current_values;

    for (const auto& arg_name : engine.monitor_session.args) {
        if (arg_name == "$time") {
            current_values.push_back(LogicVal::make_known(64, engine.get_sim_time()));
        } else {
            std::string scoped_arg = engine.monitor_session.scope.empty() ? arg_name : (engine.monitor_session.scope + "." + arg_name);
            if (engine.net_map.find(scoped_arg) != engine.net_map.end()) {
                current_values.push_back(engine.netlist[engine.net_map.at(scoped_arg)].value);
            } else {
                current_values.push_back(LogicVal::make_x(1));
            }
        }
    }

    if (engine.monitor_session.prev_values.empty()) {
        values_changed = true;
    } else {
        for (size_t i = 0; i < current_values.size(); ++i) {
            if (current_values[i] != engine.monitor_session.prev_values[i]) {
                values_changed = true;
                break;
            }
        }
    }

    if (values_changed) {
        engine.monitor_session.prev_values = current_values;

        if (!engine.is_monitor_allowed()) return;

        if (!engine.monitor_session.src_file.empty() && engine.monitor_session.src_line > 0) {
            std::cout << "[CAS $monitor " << engine.monitor_session.src_file << ":" << engine.monitor_session.src_line << "] ";
        } else {
            std::cout << "[CAS $monitor] ";
        }

        if (engine.monitor_session.has_leading_time) {
            std::cout << engine.get_sim_time() << " ";
        }

        size_t arg_idx = 0;
        size_t fmt_len = engine.monitor_session.format_string.size();
        for (size_t i = 0; i < fmt_len; ++i) {
            if (engine.monitor_session.format_string[i] == '%' && i + 1 < fmt_len) {
                char spec = engine.monitor_session.format_string[++i];
                if (spec == '0' && i + 1 < fmt_len) {
                    char next_spec = engine.monitor_session.format_string[i + 1];
                    if (next_spec == 't' || next_spec == 'h' || next_spec == 'd' || next_spec == 'b') {
                        spec = next_spec;
                        ++i;
                    }
                }

                if (arg_idx < current_values.size()) {
                    const auto& val = current_values[arg_idx++];
                    if (val.is_unknown() && (spec == 'd' || spec == 't' || spec == 'h')) {
                        std::cout << "x";
                    } else {
                        if (spec == 'd' || spec == 't') std::cout << val.aval;
                        else if (spec == 'b') std::cout << val.to_bin_string();
                        else if (spec == 'h') std::cout << std::hex << val.aval << std::dec;
                        else std::cout << val.aval;
                    }
                }
            } else {
                std::cout << engine.monitor_session.format_string[i];
            }
        }
        std::cout << "\n";
    }
}

void Evaluator::execute_statement(StatementNode* stmt, BoundProcess& bp) {
    if (auto if_stmt = dynamic_cast<IfStatementNode*>(stmt)) {
        LogicVal cond = eval_expr(if_stmt->condition.get(), bp.scope);
        if (cond.to_bool()) {
            for (auto& s : if_stmt->then_branch) execute_statement(s.get(), bp);
        } else {
            for (auto& s : if_stmt->else_branch) execute_statement(s.get(), bp);
        }
    }
    else if (auto case_stmt = dynamic_cast<CaseStatementNode*>(stmt)) {
        LogicVal sel_val = eval_expr(case_stmt->selector.get(), bp.scope);
        bool branch_taken = false;
        const CaseItem* default_item = nullptr;

        for (const auto& item : case_stmt->items) {
            if (item.is_default) {
                default_item = &item;
                continue;
            }
            LogicVal item_val = eval_expr(item.match_expr.get(), bp.scope);
            if (LogicOps::case_eq(sel_val, item_val).to_bool()) {
                for (auto& s : item.statements) execute_statement(s.get(), bp);
                branch_taken = true;
                break;
            }
        }
        if (!branch_taken && default_item) {
            for (auto& s : default_item->statements) execute_statement(s.get(), bp);
        }
    }
    else if (auto tcall = dynamic_cast<TaskCallNode*>(stmt)) {
        std::string scoped_tname = bp.scope + "." + tcall->task_name;
        if (engine.task_map.find(scoped_tname) != engine.task_map.end()) {
            TaskNode* target_task = engine.task_map.at(scoped_tname);
            for (auto& s : target_task->body) {
                execute_statement(s.get(), bp);
                if (!engine.scheduler.is_active()) return;
            }
        }
    }
    else if (auto delay = dynamic_cast<DelayStatementNode*>(stmt)) {
        // Procedural delays are scheduled strictly via StratifiedScheduler::step_process
        // onto the IEEE 1800 TimeWheel. Direct local time advancement is bypassed.
    }
    else if (auto disp = dynamic_cast<DisplayStatementNode*>(stmt)) {
        if (disp->is_monitor) {
            engine.monitor_session.active = true;
            engine.monitor_session.scope = bp.scope;
            engine.monitor_session.format_string = disp->format_string;
            engine.monitor_session.args.clear();
            for (auto& arg : disp->arg_exprs) {
                if (auto id = dynamic_cast<IdentifierNode*>(arg.get())) {
                    engine.monitor_session.args.push_back(id->name);
                }
            }
            engine.monitor_session.has_leading_time = disp->has_leading_time;
            engine.monitor_session.src_file = disp->src_file;
            engine.monitor_session.src_line = disp->src_line;
            engine.monitor_session.prev_values.clear();
            check_monitor();
        } else {
            if (!engine.is_display_allowed()) return;

            if (!disp->src_file.empty() && disp->src_line > 0) {
                std::cout << "[CAS $display " << disp->src_file << ":" << disp->src_line << "] ";
            } else {
                std::cout << "[CAS $display] ";
            }

            if (disp->has_leading_time) {
                std::cout << engine.get_sim_time() << " ";
            }
            size_t arg_idx = 0;
            size_t fmt_len = disp->format_string.size();
            for (size_t i = 0; i < fmt_len; ++i) {
                if (disp->format_string[i] == '%' && i + 1 < fmt_len) {
                    char spec = disp->format_string[++i];
                    if (spec == '0' && i + 1 < fmt_len) {
                        char next_spec = disp->format_string[i + 1];
                        if (next_spec == 't' || next_spec == 'h' || next_spec == 'd' || next_spec == 'b') {
                            spec = next_spec;
                            ++i;
                        }
                    }

                    if (arg_idx < disp->arg_exprs.size()) {
                        LogicVal val = eval_expr(disp->arg_exprs[arg_idx++].get(), bp.scope);
                        if (val.is_unknown() && (spec == 'd' || spec == 't' || spec == 'h')) {
                            std::cout << "x";
                        } else {
                            if (spec == 'd' || spec == 't') std::cout << val.aval;
                            else if (spec == 'b') std::cout << val.to_bin_string();
                            else if (spec == 'h') std::cout << std::hex << val.aval << std::dec;
                            else std::cout << val.aval;
                        }
                    }
                } else {
                    std::cout << disp->format_string[i];
                }
            }
            std::cout << "\n";
        }
    }
    else if (auto fin = dynamic_cast<FinishStatementNode*>(stmt)) {
        if (engine.is_finish_msg_allowed()) {
            if (!fin->src_file.empty() && fin->src_line > 0) {
                std::cout << "[CAS $finish " << fin->src_file << ":" << fin->src_line << "] Simulation Terminated at time " << engine.get_sim_time() << ".\n";
            } else {
                std::cout << "[CAS $finish] Simulation Terminated at time " << engine.get_sim_time() << ".\n";
            }
        }
        engine.scheduler.terminate();
        bp.completed = true;
    }
    else if (auto nassign = dynamic_cast<NonBlockingAssignNode*>(stmt)) {
        LogicVal rhs_val = eval_expr(nassign->rhs.get(), bp.scope);
        std::string scoped_target = bp.scope + "." + nassign->target;
        size_t net_id = engine.net_map.at(scoped_target);
        engine.scheduler.queue_nba(net_id, rhs_val);
    }
    else if (auto mem_assign = dynamic_cast<MemAssignNode*>(stmt)) {
        std::string scoped_mem = bp.scope + "." + mem_assign->mem_name;
        LogicVal addr_val = eval_expr(mem_assign->addr_expr.get(), bp.scope);
        LogicVal rhs_val = eval_expr(mem_assign->rhs.get(), bp.scope);

        if (!addr_val.is_unknown()) {
            uint32_t addr = addr_val.aval;
            if (mem_assign->is_non_blocking) {
                engine.scheduler.queue_nba_mem(scoped_mem, addr, rhs_val);
            } else {
                if (engine.memory_map.find(scoped_mem) != engine.memory_map.end()) {
                    auto& mem = engine.memory_map.at(scoped_mem);
                    if (addr < mem.depth) mem.words[addr] = rhs_val;
                }
            }
        }
    }
    else if (auto assign = dynamic_cast<BlockingAssignNode*>(stmt)) {
        LogicVal rhs_val = eval_expr(assign->rhs.get(), bp.scope);
        if (assign->lhs_targets.size() > 1) {
            int shift = 0;
            for (int i = (int)assign->lhs_targets.size() - 1; i >= 0; --i) {
                std::string scoped_target = bp.scope + "." + assign->lhs_targets[i];
                size_t net_id = engine.net_map.at(scoped_target);
                uint32_t width = engine.netlist[net_id].width;
                uint64_t mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
                engine.netlist[net_id].set(LogicVal(width, (rhs_val.aval >> shift) & mask, (rhs_val.bval >> shift) & mask));
                shift += width;
            }
        } else {
            std::string scoped_target = bp.scope + "." + assign->lhs_targets[0];
            size_t net_id = engine.net_map.at(scoped_target);
            engine.netlist[net_id].set(rhs_val);
        }
    }
}