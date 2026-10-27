#include "../include/engine.hpp"
#include <iomanip>
#include <stdexcept>
#include <ctime>

size_t Engine::get_or_create_net(const std::string& scoped_name, uint32_t width) {
    if (net_map.find(scoped_name) != net_map.end()) {
        return net_map[scoped_name];
    }
    size_t id = netlist.size();
    netlist.emplace_back(scoped_name, width);
    netlist.back().vcd_id = VcdDumper::generate_vcd_id(id);
    net_map[scoped_name] = id;
    return id;
}

uint64_t Engine::eval_const_expr(ExpressionNode* expr, const std::string& scope) {
    if (!expr) return 0;
    if (auto num = dynamic_cast<NumberNode*>(expr)) {
        return num->value;
    }
    if (auto id = dynamic_cast<IdentifierNode*>(expr)) {
        std::string scoped_id = scope.empty() ? id->name : (scope + "." + id->name);
        if (param_map.find(scoped_id) != param_map.end()) {
            return param_map.at(scoped_id);
        }
        if (param_map.find(id->name) != param_map.end()) {
            return param_map.at(id->name);
        }
        return 0;
    }
    if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
        uint64_t l = eval_const_expr(bin->lhs.get(), scope);
        uint64_t r = eval_const_expr(bin->rhs.get(), scope);
        if (bin->op == "+") return l + r;
        if (bin->op == "-") return (l >= r) ? (l - r) : 0;
        if (bin->op == "*") return l * r;
        if (bin->op == "<<") return l << r;
        if (bin->op == ">>") return l >> r;
    }
    return 0;
}

NetSliceRef Engine::resolve_slice_ref(ExpressionNode* expr, const std::string& scope) {
    if (auto ident = dynamic_cast<IdentifierNode*>(expr)) {
        std::string full_name = scope.empty() ? ident->name : (scope + "." + ident->name);
        size_t id = net_map.at(full_name);
        return {id, 0, netlist[id].width};
    }
    if (auto idx = dynamic_cast<IndexSelectNode*>(expr)) {
        std::string full_name = scope.empty() ? idx->name : (scope + "." + idx->name);
        size_t id = net_map.at(full_name);
        if (!idx->is_part_select) {
            LogicVal iv = eval_expr(idx->msb_expr.get(), scope);
            uint32_t off = iv.is_unknown() ? 0 : (uint32_t)iv.aval;
            return {id, off, 1};
        } else {
            LogicVal mv = eval_expr(idx->msb_expr.get(), scope);
            LogicVal lv = eval_expr(idx->lsb_expr.get(), scope);
            uint32_t msb = mv.is_unknown() ? 0 : (uint32_t)mv.aval;
            uint32_t lsb = lv.is_unknown() ? 0 : (uint32_t)lv.aval;
            uint32_t w = (msb >= lsb) ? (msb - lsb + 1) : (lsb - msb + 1);
            return {id, lsb, w};
        }
    }
    if (auto num = dynamic_cast<NumberNode*>(expr)) {
        static size_t const_counter = 0;
        std::string const_net_name = scope + ".__const_port_" + std::to_string(const_counter++);
        size_t id = get_or_create_net(const_net_name, num->width);
        LogicVal val = num->is_unknown ? LogicVal::make_x(num->width) : LogicVal::make_known(num->width, num->value);
        netlist[id].set(val);
        return {id, 0, num->width};
    }

    throw std::runtime_error("Expression is not a bindable net target");
}

LogicVal Engine::read_slice(const NetSliceRef& ref) {
    const auto& net_val = netlist[ref.net_id].value;
    uint64_t mask = (ref.width >= 64) ? ~0ULL : ((1ULL << ref.width) - 1);
    uint64_t a = (net_val.aval >> ref.offset) & mask;
    uint64_t b = (net_val.bval >> ref.offset) & mask;
    return LogicVal(ref.width, a, b);
}

void Engine::write_slice(const NetSliceRef& ref, const LogicVal& val) {
    auto cur = netlist[ref.net_id].value;
    uint64_t mask = (ref.width >= 64) ? ~0ULL : ((1ULL << ref.width) - 1);
    uint64_t clear_mask = ~(mask << ref.offset);

    uint64_t new_a = (cur.aval & clear_mask) | ((val.aval & mask) << ref.offset);
    uint64_t new_b = (cur.bval & clear_mask) | ((val.bval & mask) << ref.offset);
    netlist[ref.net_id].set(LogicVal(cur.width, new_a, new_b));
}

LogicVal Engine::eval_expr(ExpressionNode* expr, const std::string& scope) {
    if (auto num = dynamic_cast<NumberNode*>(expr)) {
        if (num->is_unknown) return LogicVal::make_x(num->width);
        return LogicVal::make_known(num->width, num->value);
    }
    if (auto ident = dynamic_cast<IdentifierNode*>(expr)) {
        if (ident->name == "$time") return LogicVal::make_known(64, sim_time);
        std::string scoped_ident = scope.empty() ? ident->name : (scope + "." + ident->name);
        if (param_map.find(scoped_ident) != param_map.end()) {
            return LogicVal::make_known(32, param_map.at(scoped_ident));
        }
        if (net_map.find(scoped_ident) != net_map.end()) {
            return netlist[net_map.at(scoped_ident)].value;
        }
        if (param_map.find(ident->name) != param_map.end()) {
            return LogicVal::make_known(32, param_map.at(ident->name));
        }
        return LogicVal::make_x(1);
    }
    if (auto idx_node = dynamic_cast<IndexSelectNode*>(expr)) {
        std::string scoped_ident = scope.empty() ? idx_node->name : (scope + "." + idx_node->name);
        if (memory_map.find(scoped_ident) != memory_map.end()) {
            const auto& mem = memory_map.at(scoped_ident);
            LogicVal addr_val = eval_expr(idx_node->msb_expr.get(), scope);
            if (addr_val.is_unknown() || addr_val.aval >= mem.depth) {
                return LogicVal::make_x(mem.word_width);
            }
            return mem.words[addr_val.aval];
        }

        const auto& target_val = netlist[net_map.at(scoped_ident)].value;
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
        if (un->op == '~') return val.bitwise_not();
    }
    if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
        LogicVal l = eval_expr(bin->lhs.get(), scope);
        LogicVal r = eval_expr(bin->rhs.get(), scope);

        if (bin->op == "+") {
            uint32_t w = std::max(l.width, r.width);
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(w);
            return LogicVal::make_known(w, l.aval + r.aval);
        }
        if (bin->op == "-") {
            uint32_t w = std::max(l.width, r.width);
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(w);
            return LogicVal::make_known(w, l.aval - r.aval);
        }
        if (bin->op == "&")  return LogicVal::bitwise_and(l, r);
        if (bin->op == "|")  return LogicVal::bitwise_or(l, r);
        if (bin->op == "^")  return LogicVal::bitwise_xor(l, r);
        if (bin->op == "<<") return LogicVal::shift_left(l, r);
        if (bin->op == ">>") return LogicVal::shift_right(l, r);
        if (bin->op == "==") return LogicVal::logical_eq(l, r);
        if (bin->op == "!=") return LogicVal::logical_neq(l, r);
        if (bin->op == "<")  return LogicVal::rel_lt(l, r);
        if (bin->op == "<=") return LogicVal::rel_le(l, r);
        if (bin->op == ">")  return LogicVal::rel_gt(l, r);
        if (bin->op == ">=") return LogicVal::rel_ge(l, r);
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
        if (cond.is_unknown()) {
            LogicVal t_val = eval_expr(tern->true_expr.get(), scope);
            LogicVal f_val = eval_expr(tern->false_expr.get(), scope);
            return LogicVal::make_x(std::max(t_val.width, f_val.width));
        }
        return cond.to_bool() ? eval_expr(tern->true_expr.get(), scope) : eval_expr(tern->false_expr.get(), scope);
    }
    return LogicVal::make_x(1);
}

void Engine::check_monitor() {
    if (!monitor_session.active) return;

    bool values_changed = false;
    std::vector<LogicVal> current_values;

    for (const auto& arg_name : monitor_session.args) {
        if (arg_name == "$time") {
            current_values.push_back(LogicVal::make_known(64, sim_time));
        } else {
            std::string scoped_arg = monitor_session.scope.empty() ? arg_name : (monitor_session.scope + "." + arg_name);
            if (net_map.find(scoped_arg) != net_map.end()) {
                current_values.push_back(netlist[net_map.at(scoped_arg)].value);
            } else {
                current_values.push_back(LogicVal::make_x(1));
            }
        }
    }

    if (monitor_session.prev_values.empty()) {
        values_changed = true;
    } else {
        for (size_t i = 0; i < current_values.size(); ++i) {
            if (current_values[i] != monitor_session.prev_values[i]) {
                values_changed = true;
                break;
            }
        }
    }

    if (values_changed) {
        monitor_session.prev_values = current_values;
        if (monitor_session.has_leading_time) {
            std::cout << sim_time;
        }

        size_t arg_idx = 0;
        size_t fmt_len = monitor_session.format_string.size();
        for (size_t i = 0; i < fmt_len; ++i) {
            if (monitor_session.format_string[i] == '%' && i + 1 < fmt_len) {
                char spec = monitor_session.format_string[++i];
                if (spec == '0' && i + 1 < fmt_len) {
                    char next_spec = monitor_session.format_string[i + 1];
                    if (next_spec == 't' || next_spec == 'h' || next_spec == 'd' || next_spec == 'b') {
                        spec = next_spec;
                        ++i;
                    }
                }

                if (arg_idx < current_values.size()) {
                    const auto& val = current_values[arg_idx++];
                    if (val.is_unknown()) {
                        std::cout << "x";
                    } else {
                        if (spec == 'd' || spec == 't') std::cout << val.aval;
                        else if (spec == 'b') std::cout << val.to_bin_string();
                        else if (spec == 'h') std::cout << std::hex << val.aval << std::dec;
                        else std::cout << val.aval;
                    }
                }
            } else {
                std::cout << monitor_session.format_string[i];
            }
        }
        std::cout << "\n";
    }
}

void Engine::propagate_comb() {
    delta_cycles_count++;
    bool changed = true;
    size_t iters = 0;
    while (changed && iters < 100) {
        changed = false;

        // 1. Module port interconnect slice bindings
        for (auto& bind : slice_bindings) {
            LogicVal src_val = read_slice(bind.src);
            LogicVal dst_val = read_slice(bind.dst);
            if (src_val != dst_val) {
                write_slice(bind.dst, src_val);
                changed = true;
            }
        }

        // 2. Built-in Gate Primitives
        for (auto& g : gates) {
            LogicVal res = eval_expr(g.in_exprs[0], g.scope);
            for (size_t i = 1; i < g.in_exprs.size(); ++i) {
                LogicVal next_in = eval_expr(g.in_exprs[i], g.scope);
                if (g.type == GateType::AND) res = LogicVal::bitwise_and(res, next_in);
                else if (g.type == GateType::OR) res = LogicVal::bitwise_or(res, next_in);
                else if (g.type == GateType::XOR) res = LogicVal::bitwise_xor(res, next_in);
                else if (g.type == GateType::NAND) res = LogicVal::bitwise_and(res, next_in);
                else if (g.type == GateType::NOR) res = LogicVal::bitwise_or(res, next_in);
                else if (g.type == GateType::XNOR) res = LogicVal::bitwise_xor(res, next_in);
            }
            if (g.type == GateType::NAND || g.type == GateType::NOR || g.type == GateType::XNOR || g.type == GateType::NOT) {
                res = res.bitwise_not();
            }
            LogicVal cur_out = read_slice(g.out_target);
            if (cur_out != res) {
                write_slice(g.out_target, res);
                changed = true;
            }
        }

        // 3. Continuous assign statements
        for (auto& ca : continuous_assigns) {
            LogicVal val = eval_expr(ca.rhs, ca.scope);
            LogicVal cur = read_slice(ca.target);
            if (cur != val) {
                write_slice(ca.target, val);
                changed = true;
            }
        }

        // 4. always_comb blocks
        for (auto& bp : processes) {
            if (bp.proc->type != ProcessType::COMBINATIONAL) continue;
            for (auto& stmt : bp.proc->body) {
                execute_statement(stmt.get(), bp);
            }
        }

        for (const auto& n : netlist) {
            if (n.modified) {
                changed = true;
                break;
            }
        }
        iters++;
    }
}

void Engine::execute_statement(StatementNode* stmt, BoundProcess& bp) {
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
            if (LogicVal::logical_eq(sel_val, item_val).to_bool()) {
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
        if (task_map.find(scoped_tname) != task_map.end()) {
            TaskNode* target_task = task_map.at(scoped_tname);
            for (auto& s : target_task->body) {
                execute_statement(s.get(), bp);
                if (!simulation_active) return;
                propagate_comb();
                evaluate_sequential_blocks();
                commit_nba_updates();
            }
        }
    }
    else if (auto delay = dynamic_cast<DelayStatementNode*>(stmt)) {
        sim_time += delay->delay;
        vcd_dumper.dump_sample(netlist, sim_time);
        check_monitor();
    }
    else if (auto disp = dynamic_cast<DisplayStatementNode*>(stmt)) {
        if (disp->is_monitor) {
            monitor_session.active = true;
            monitor_session.scope = bp.scope;
            monitor_session.format_string = disp->format_string;
            monitor_session.args = disp->args;
            monitor_session.has_leading_time = disp->has_leading_time;
            monitor_session.prev_values.clear();
            check_monitor();
        } else {
            std::cout << "[CAS $display] ";
            if (disp->has_leading_time) {
                std::cout << sim_time << " ";
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

                    if (arg_idx < disp->args.size()) {
                        std::string arg_name = disp->args[arg_idx++];
                        if (arg_name == "$time") {
                            std::cout << sim_time;
                        } else {
                            std::string scoped_arg = bp.scope.empty() ? arg_name : (bp.scope + "." + arg_name);
                            if (net_map.find(scoped_arg) != net_map.end()) {
                                size_t net_id = net_map.at(scoped_arg);
                                const auto& net = netlist[net_id];
                                if (net.value.is_unknown()) {
                                    std::cout << "x";
                                } else {
                                    if (spec == 'd' || spec == 't') std::cout << net.value.aval;
                                    else if (spec == 'b') std::cout << net.value.to_bin_string();
                                    else if (spec == 'h') std::cout << std::hex << net.value.aval << std::dec;
                                    else std::cout << net.value.aval;
                                }
                            } else if (param_map.find(scoped_arg) != param_map.end()) {
                                std::cout << param_map.at(scoped_arg);
                            } else if (param_map.find(arg_name) != param_map.end()) {
                                std::cout << param_map.at(arg_name);
                            } else {
                                std::cout << "x";
                            }
                        }
                    }
                } else {
                    std::cout << disp->format_string[i];
                }
            }
            std::cout << "\n";
        }
    }
    else if (dynamic_cast<FinishStatementNode*>(stmt)) {
        std::cout << "[CAS $finish] Simulation Terminated at time " << sim_time << ".\n";
        simulation_active = false;
        bp.completed = true;
    }
    else if (auto nassign = dynamic_cast<NonBlockingAssignNode*>(stmt)) {
        LogicVal rhs_val = eval_expr(nassign->rhs.get(), bp.scope);
        std::string scoped_target = bp.scope + "." + nassign->target;
        size_t net_id = net_map.at(scoped_target);
        nba_queue.push_back({net_id, rhs_val});
    }
    else if (auto mem_assign = dynamic_cast<MemAssignNode*>(stmt)) {
        std::string scoped_mem = bp.scope + "." + mem_assign->mem_name;
        LogicVal addr_val = eval_expr(mem_assign->addr_expr.get(), bp.scope);
        LogicVal rhs_val = eval_expr(mem_assign->rhs.get(), bp.scope);

        if (!addr_val.is_unknown()) {
            uint32_t addr = addr_val.aval;
            if (mem_assign->is_non_blocking) {
                nba_mem_queue.push_back({scoped_mem, addr, rhs_val});
            } else {
                if (memory_map.find(scoped_mem) != memory_map.end()) {
                    auto& mem = memory_map.at(scoped_mem);
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
                size_t net_id = net_map.at(scoped_target);
                uint32_t width = netlist[net_id].width;
                uint64_t mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
                netlist[net_id].set(LogicVal(width, (rhs_val.aval >> shift) & mask, (rhs_val.bval >> shift) & mask));
                shift += width;
            }
        } else {
            std::string scoped_target = bp.scope + "." + assign->lhs_targets[0];
            size_t net_id = net_map.at(scoped_target);
            netlist[net_id].set(rhs_val);
        }
    }
}

void Engine::evaluate_sequential_blocks() {
    for (auto& bp : processes) {
        if (bp.proc->type != ProcessType::SEQUENTIAL) continue;

        bool triggered = false;
        for (auto& sens : bp.proc->sensitivity_list) {
            std::string scoped_sig = bp.scope + "." + sens.signal_name;
            size_t net_id = net_map.at(scoped_sig);
            const auto& net = netlist[net_id];

            if (sens.edge == EdgeType::POSEDGE && net.is_posedge()) { triggered = true; break; }
            else if (sens.edge == EdgeType::NEGEDGE && net.is_negedge()) { triggered = true; break; }
        }

        if (triggered) {
            for (auto& stmt : bp.proc->body) execute_statement(stmt.get(), bp);
        }
    }
}

void Engine::commit_nba_updates() {
    if (nba_queue.empty() && nba_mem_queue.empty()) return;

    nba_commits_count++;
    for (const auto& update : nba_queue) netlist[update.net_id].set(update.val);
    nba_queue.clear();

    for (const auto& mem_up : nba_mem_queue) {
        if (memory_map.find(mem_up.mem_scoped_name) != memory_map.end()) {
            auto& mem = memory_map.at(mem_up.mem_scoped_name);
            if (mem_up.addr < mem.depth) mem.words[mem_up.addr] = mem_up.val;
        }
    }
    nba_mem_queue.clear();
    propagate_comb();
}

void Engine::elaborate_module(ModuleNode* mod, const std::string& scope,
                              const std::unordered_map<std::string, ModuleNode*>& module_defs,
                              const std::vector<std::unique_ptr<ExpressionNode>>* overrides,
                              const std::string& parent_scope,
                              std::ostream* debug_out) {
    if (debug_out) {
        *debug_out << "  -> Scoping module '" << mod->name << "' as [" << scope << "]\n";
    }

    for (size_t i = 0; i < mod->parameters.size(); ++i) {
        const auto& p = mod->parameters[i];
        uint64_t val = 0;
        if (overrides && i < overrides->size()) {
            val = eval_const_expr((*overrides)[i].get(), parent_scope);
            if (debug_out) *debug_out << "     * Parameter override: " << p.name << " = " << val << "\n";
        } else {
            val = eval_const_expr(p.default_expr.get(), scope);
            if (debug_out) *debug_out << "     * Parameter default: " << p.name << " = " << val << "\n";
        }
        param_map[scope + "." + p.name] = val;
    }

    for (auto& p : mod->ports) {
        uint32_t w = 1;
        if (p.msb_expr && p.lsb_expr) {
            uint64_t msb = eval_const_expr(p.msb_expr.get(), scope);
            uint64_t lsb = eval_const_expr(p.lsb_expr.get(), scope);
            w = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
        }
        get_or_create_net(scope + "." + p.name, w);
    }

    for (auto& s : mod->signals) {
        uint32_t w = 1;
        if (s.msb_expr && s.lsb_expr) {
            uint64_t msb = eval_const_expr(s.msb_expr.get(), scope);
            uint64_t lsb = eval_const_expr(s.lsb_expr.get(), scope);
            w = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
        }
        if (s.is_array) {
            uint32_t depth = 1;
            if (s.bound1_expr && s.bound2_expr) {
                uint64_t b1 = eval_const_expr(s.bound1_expr.get(), scope);
                uint64_t b2 = eval_const_expr(s.bound2_expr.get(), scope);
                depth = (b1 >= b2) ? (uint32_t)(b1 - b2 + 1) : (uint32_t)(b2 - b1 + 1);
            } else if (s.bound1_expr) {
                depth = (uint32_t)eval_const_expr(s.bound1_expr.get(), scope);
            }
            std::string mem_scope = scope + "." + s.name;
            memory_map[mem_scope] = MemoryArray(mem_scope, w, depth);
            if (debug_out) {
                *debug_out << "     * Memory allocated: " << mem_scope 
                           << " [" << w << "-bit x " << depth << " words]\n";
            }
        } else {
            get_or_create_net(scope + "." + s.name, w);
        }
    }

    for (auto& ca : mod->continuous_assigns) {
        std::string scoped_target = scope + "." + ca->target_name;
        size_t target_id = net_map.at(scoped_target);
        continuous_assigns.push_back({scope, {target_id, 0, netlist[target_id].width}, ca->rhs.get()});
    }

    for (auto& g : mod->gates) {
        GateInstance gi;
        gi.type = g->type;
        gi.scope = scope;
        gi.out_target = resolve_slice_ref(g->out_expr.get(), scope);
        for (auto& ie : g->in_exprs) gi.in_exprs.push_back(ie.get());
        gates.push_back(gi);
    }

    for (auto& t : mod->tasks) {
        task_map[scope + "." + t->name] = t.get();
    }

    for (auto& proc : mod->processes) {
        processes.push_back({scope, proc.get(), 0, false});
    }

    for (auto& inst : mod->instantiations) {
        std::string child_scope = scope + "." + inst->instance_name;
        if (module_defs.find(inst->module_type) == module_defs.end()) {
            throw std::runtime_error("Undefined module type instantiated: " + inst->module_type);
        }
        ModuleNode* submod = module_defs.at(inst->module_type);

        elaborate_module(submod, child_scope, module_defs, &inst->param_overrides, scope, debug_out);

        for (size_t conn_idx = 0; conn_idx < inst->connections.size(); ++conn_idx) {
            auto& conn = inst->connections[conn_idx];
            std::string target_port_name;
            if (inst->is_positional) {
                if (conn_idx >= submod->ports.size()) {
                    throw std::runtime_error("Positional connection out of bounds in " + inst->instance_name);
                }
                target_port_name = submod->ports[conn_idx].name;
            } else {
                target_port_name = conn.port_name;
            }

            std::string dir = "input";
            for (auto& p : submod->ports) {
                if (p.name == target_port_name) {
                    dir = p.direction;
                    break;
                }
            }

            NetSliceRef parent_ref = resolve_slice_ref(conn.expr.get(), scope);
            size_t child_net_id = net_map.at(child_scope + "." + target_port_name);
            NetSliceRef child_ref = {child_net_id, 0, netlist[child_net_id].width};

            if (dir == "input") {
                slice_bindings.push_back({parent_ref, child_ref});
            } else {
                slice_bindings.push_back({child_ref, parent_ref});
            }
        }
    }
}

void Engine::elaborate(std::vector<std::unique_ptr<ModuleNode>>& modules,
                       const std::string& top_module_name,
                       std::ostream* debug_out) {
    if (debug_out) *debug_out << "[PHASE 3: Elaboration] Synthesizing Netlist Hierarchy...\n";
    std::unordered_map<std::string, ModuleNode*> module_defs;
    for (auto& mod : modules) {
        module_defs[mod->name] = mod.get();
    }

    ModuleNode* top = nullptr;

    if (!top_module_name.empty()) {
        if (module_defs.find(top_module_name) != module_defs.end()) {
            top = module_defs.at(top_module_name);
            if (debug_out) *debug_out << "  -> Using explicit top module: '" << top->name << "'\n";
        } else {
            throw std::runtime_error("Elaboration error: Top module '" + top_module_name + "' was not found in parsed sources.");
        }
    } else {
        std::unordered_map<std::string, bool> is_instantiated;
        for (auto& mod : modules) {
            for (auto& inst : mod->instantiations) {
                is_instantiated[inst->module_type] = true;
            }
        }
        for (auto& mod : modules) {
            if (!is_instantiated[mod->name]) {
                top = mod.get();
                break;
            }
        }
        if (!top) top = modules.front().get();
        if (debug_out) *debug_out << "  -> Auto-detected top-level root module: '" << top->name << "'\n";
    }

    elaborate_module(top, top->name, module_defs, nullptr, "", debug_out);
}

void Engine::step_process(size_t pid) {
    auto& bp = processes[pid];
    if (bp.completed) return;

    while (bp.pc < bp.proc->body.size()) {
        auto stmt = bp.proc->body[bp.pc].get();

        if (auto delay = dynamic_cast<DelayStatementNode*>(stmt)) {
            bp.pc++;
            uint64_t wake_time = sim_time + delay->delay;
            time_wheel.push({wake_time, (uint32_t)pid, event_order_counter++});
            return;
        }

        if (auto df = dynamic_cast<DumpFileStatementNode*>(stmt)) {
            vcd_dumper.enable(df->filename);
            bp.pc++;
            continue;
        }

        if (dynamic_cast<DumpVarsStatementNode*>(stmt)) {
            vcd_dumper.init_header(netlist, sim_time);
            bp.pc++;
            continue;
        }

        if (auto if_stmt = dynamic_cast<IfStatementNode*>(stmt)) {
            execute_statement(if_stmt, bp);
            if (!simulation_active) return;
            bp.pc++;
            continue;
        }

        if (auto case_stmt = dynamic_cast<CaseStatementNode*>(stmt)) {
            execute_statement(case_stmt, bp);
            if (!simulation_active) return;
            bp.pc++;
            continue;
        }

        execute_statement(stmt, bp);
        if (!simulation_active) return;

        propagate_comb();
        evaluate_sequential_blocks();
        commit_nba_updates();
        vcd_dumper.dump_sample(netlist, sim_time);
        check_monitor();

        for (auto& n : netlist) n.modified = false;
        bp.pc++;
    }

    bp.completed = true;
}

void Engine::run(std::ostream* debug_out) {
    if (debug_out) {
        *debug_out << "[PHASE 4: Scheduler Initialization]\n";
        *debug_out << "  -> Hierarchical Nets Elaborated : " << netlist.size() << "\n";
        *debug_out << "  -> Slice Interconnects Bound    : " << slice_bindings.size() << "\n";
        *debug_out << "  -> Built-in Gates Instantiated  : " << gates.size() << "\n";
        *debug_out << "  -> Continuous Assigns Bound     : " << continuous_assigns.size() << "\n";
        *debug_out << "  -> Memory Arrays Allocated      : " << memory_map.size() << "\n";
        *debug_out << "  -> Concurrent Processes Bound   : " << processes.size() << "\n";
        *debug_out << "[PHASE 5: Simulation Runtime (Discrete-Event Scheduler)]\n";
        *debug_out << "--------------------------------------------------------\n";
    }

    propagate_comb();

    for (size_t i = 0; i < processes.size(); ++i) {
        if (processes[i].proc->type == ProcessType::INITIAL) {
            step_process(i);
        }
    }

    while (simulation_active && !time_wheel.empty()) {
        events_dispatched++;
        SimEvent ev = time_wheel.top();
        time_wheel.pop();

        sim_time = ev.time;
        step_process(ev.process_id);
    }

    if (vcd_dumper.is_enabled()) {
        std::string dumped_file = vcd_dumper.get_filename();
        vcd_dumper.close();
        std::cout << "[CAS VCD] Trace successfully dumped to: " << dumped_file << "\n";
    }

    std::cout << "--------------------------------------------------------\n";
    std::cout << "[CAS SUMMARY] Simulation completed cleanly at T = " << sim_time << " ns.\n" << std::flush;
}

EngineStats Engine::get_stats() {
    EngineStats s;
    s.net_count = netlist.size();
    s.net_cap = netlist.capacity();
    s.proc_count = processes.size();
    s.proc_cap = processes.capacity();
    s.slice_count = slice_bindings.size();
    s.slice_cap = slice_bindings.capacity();
    s.gate_count = gates.size();
    s.gate_cap = gates.capacity();
    s.ca_count = continuous_assigns.size();
    s.ca_cap = continuous_assigns.capacity();
    s.ram_count = memory_map.size();

    size_t words = 0;
    for (const auto& kv : memory_map) {
        words += kv.second.words.capacity();
    }
    s.ram_total_words = words;
    s.net_map_keys = net_map.size();
    s.net_map_buckets = net_map.bucket_count();
    s.net_map_load = net_map.load_factor();
    s.events_dispatched = events_dispatched;
    s.delta_cycles = delta_cycles_count;
    s.nba_commits = nba_commits_count;
    return s;
}