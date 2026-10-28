#include "../../include/kernel/evaluator.hpp"
#include "../../include/engine.hpp"
#include "../../include/kernel/logic_ops.hpp"
#include <iostream>
#include <iomanip>

Evaluator::Evaluator(Engine& eng) 
    : engine(eng), expr_eval(eng) {}

LogicVal Evaluator::read_slice(const NetSliceRef& ref) {
    const auto& net = engine.netlist[ref.net_id];
    const auto& net_val = net.value;
    uint64_t mask = (ref.width >= 64) ? ~0ULL : ((1ULL << ref.width) - 1);
    uint64_t a = (net_val.aval >> ref.offset) & mask;
    uint64_t b = (net_val.bval >> ref.offset) & mask;
    return LogicVal(ref.width, a, b);
}

void Evaluator::write_slice(const NetSliceRef& ref, const LogicVal& val) {
    auto& target_net = engine.netlist[ref.net_id];
    LogicVal casted = LogicOps::cast_width(val, ref.width, target_net.is_signed);

    auto cur = target_net.value;
    uint64_t mask = (ref.width >= 64) ? ~0ULL : ((1ULL << ref.width) - 1);
    uint64_t clear_mask = ~(mask << ref.offset);

    uint64_t new_a = (cur.aval & clear_mask) | ((casted.aval & mask) << ref.offset);
    uint64_t new_b = (cur.bval & clear_mask) | ((casted.bval & mask) << ref.offset);
    target_net.set(LogicVal(cur.width, new_a, new_b));
}

LogicVal Evaluator::eval_expr(ExpressionNode* expr, const std::string& scope) {
    return expr_eval.eval(expr, scope);
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
        // Scheduled via StratifiedScheduler::step_process
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
        LogicVal casted = LogicOps::cast_width(rhs_val, engine.netlist[net_id].width, engine.netlist[net_id].is_signed);
        engine.scheduler.queue_nba(net_id, casted);
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
            LogicVal casted = LogicOps::cast_width(rhs_val, engine.netlist[net_id].width, engine.netlist[net_id].is_signed);
            engine.netlist[net_id].set(casted);
        }
    }
}