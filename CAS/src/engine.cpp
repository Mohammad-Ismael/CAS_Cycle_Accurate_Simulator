#include "../include/engine.hpp"
#include <iomanip>
#include <stdexcept>
#include <ctime>

Engine::~Engine() {
    if (vcd_file.is_open()) {
        vcd_file.close();
    }
}

std::string Engine::generate_vcd_id(size_t id) {
    std::string res;
    size_t base = 94; // Printable ASCII range 33 ('!') through 126 ('~')
    do {
        res += (char)(33 + (id % base));
        id /= base;
    } while (id > 0);
    return res;
}

size_t Engine::get_or_create_net(const std::string& scoped_name, uint32_t width) {
    if (net_map.find(scoped_name) != net_map.end()) {
        return net_map[scoped_name];
    }
    size_t id = netlist.size();
    netlist.emplace_back(scoped_name, width);
    netlist.back().vcd_id = generate_vcd_id(id);
    net_map[scoped_name] = id;
    return id;
}

LogicVal Engine::eval_expr(ExpressionNode* expr, const std::string& scope) {
    if (auto num = dynamic_cast<NumberNode*>(expr)) {
        return LogicVal::make_known(num->width, num->value);
    }
    if (auto ident = dynamic_cast<IdentifierNode*>(expr)) {
        if (ident->name == "$time") {
            return LogicVal::make_known(64, sim_time);
        }
        std::string scoped_ident = scope.empty() ? ident->name : (scope + "." + ident->name);
        return netlist[net_map.at(scoped_ident)].value;
    }
    if (auto idx_node = dynamic_cast<IndexSelectNode*>(expr)) {
        std::string scoped_ident = scope.empty() ? idx_node->name : (scope + "." + idx_node->name);

        // Check if target is a 1D Memory Array
        if (memory_map.find(scoped_ident) != memory_map.end()) {
            const auto& mem = memory_map.at(scoped_ident);
            LogicVal addr_val = eval_expr(idx_node->msb_expr.get(), scope);
            if (addr_val.is_unknown() || addr_val.aval >= mem.depth) {
                return LogicVal::make_x(mem.word_width);
            }
            return mem.words[addr_val.aval];
        }

        // Standard Scalar Net bit-select or part-select
        const auto& target_val = netlist[net_map.at(scoped_ident)].value;
        if (!idx_node->is_part_select) {
            LogicVal idx_val = eval_expr(idx_node->msb_expr.get(), scope);
            if (idx_val.is_unknown() || idx_val.aval >= target_val.width) {
                return LogicVal::make_x(1);
            }
            uint32_t sh = idx_val.aval;
            uint64_t a = (target_val.aval >> sh) & 1;
            uint64_t b = (target_val.bval >> sh) & 1;
            return LogicVal(1, a, b);
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
            uint64_t a = (target_val.aval >> lsb) & mask;
            uint64_t b = (target_val.bval >> lsb) & mask;
            return LogicVal(slice_w, a, b);
        }
    }
    if (auto un = dynamic_cast<UnaryOpNode*>(expr)) {
        LogicVal val = eval_expr(un->operand.get(), scope);
        if (un->op == '!') {
            if (val.is_unknown()) return LogicVal::make_x(1);
            return LogicVal::make_known(1, (val.aval == 0) ? 1 : 0);
        }
        if (un->op == '~') {
            return val.bitwise_not();
        }
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
            if ((!l.is_unknown() && l.aval == 0) || (!r.is_unknown() && r.aval == 0)) {
                return LogicVal::make_known(1, 0);
            }
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
            return LogicVal::make_known(1, (l.aval != 0 && r.aval != 0) ? 1 : 0);
        }

        if (bin->op == "||") {
            if ((!l.is_unknown() && l.aval != 0) || (!r.is_unknown() && r.aval != 0)) {
                return LogicVal::make_known(1, 1);
            }
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
            return LogicVal::make_known(1, (l.aval != 0 || r.aval != 0) ? 1 : 0);
        }
    }
    if (auto tern = dynamic_cast<TernaryOpNode*>(expr)) {
        LogicVal cond = eval_expr(tern->condition.get(), scope);
        if (cond.is_unknown()) {
            LogicVal t_val = eval_expr(tern->true_expr.get(), scope);
            LogicVal f_val = eval_expr(tern->false_expr.get(), scope);
            uint32_t w = std::max(t_val.width, f_val.width);
            return LogicVal::make_x(w);
        }
        if (cond.to_bool()) {
            return eval_expr(tern->true_expr.get(), scope);
        } else {
            return eval_expr(tern->false_expr.get(), scope);
        }
    }
    return LogicVal::make_x(1);
}

void Engine::init_vcd_header() {
    if (!vcd_enabled || vcd_file.is_open()) return;

    vcd_file.open(vcd_filename);
    if (!vcd_file.is_open()) {
        std::cerr << "[CAS ERROR] Unable to create VCD file: " << vcd_filename << "\n";
        vcd_enabled = false;
        return;
    }

    std::time_t now = std::time(nullptr);
    vcd_file << "$date\n   " << std::asctime(std::localtime(&now)) << "$end\n";
    vcd_file << "$version\n   CAS (Cycle Accurate Simulator) v0.9.0 [Memory & Dataflow Kernel]\n$end\n";
    vcd_file << "$timescale 1ns $end\n";

    std::unordered_map<std::string, std::vector<size_t>> scopes;
    for (size_t i = 0; i < netlist.size(); ++i) {
        std::string full_name = netlist[i].name;
        size_t dot_pos = full_name.rfind('.');
        std::string scope_name = (dot_pos == std::string::npos) ? "top" : full_name.substr(0, dot_pos);
        scopes[scope_name].push_back(i);
    }

    for (const auto& kv : scopes) {
        vcd_file << "$scope module " << kv.first << " $end\n";
        for (size_t net_id : kv.second) {
            const auto& net = netlist[net_id];
            size_t dot_pos = net.name.rfind('.');
            std::string leaf_name = (dot_pos == std::string::npos) ? net.name : net.name.substr(dot_pos + 1);
            vcd_file << "$var wire " << net.width << " " << net.vcd_id << " " << leaf_name << " $end\n";
        }
        vcd_file << "$upscope $end\n";
    }

    vcd_file << "$enddefinitions $end\n";
    vcd_file << "$dumpvars\n";
    dump_vcd_sample(true);
    vcd_file << "$end\n";
}

void Engine::dump_vcd_sample(bool initial_dump) {
    if (!vcd_enabled || !vcd_file.is_open()) return;

    if (!initial_dump && sim_time != last_dump_time) {
        vcd_file << "#" << sim_time << "\n";
        last_dump_time = sim_time;
    }

    for (auto& net : netlist) {
        if (initial_dump || net.modified) {
            if (net.width == 1) {
                if (net.value.is_unknown()) {
                    vcd_file << "x" << net.vcd_id << "\n";
                } else {
                    vcd_file << (net.value.aval & 1) << net.vcd_id << "\n";
                }
            } else {
                vcd_file << "b" << net.value.to_bin_string() << " " << net.vcd_id << "\n";
            }
        }
    }
}

void Engine::propagate_comb() {
    bool changed = true;
    size_t iters = 0;
    while (changed && iters < 100) {
        changed = false;

        // 1. Module port interconnect bindings
        for (auto& bind : port_bindings) {
            if (netlist[bind.dst_net_id].value != netlist[bind.src_net_id].value) {
                netlist[bind.dst_net_id].set(netlist[bind.src_net_id].value);
                changed = true;
            }
        }

        // 2. Continuous assign statements
        for (auto& ca : continuous_assigns) {
            LogicVal val = eval_expr(ca.rhs, ca.scope);
            if (netlist[ca.target_net_id].value != val) {
                netlist[ca.target_net_id].set(val);
                changed = true;
            }
        }

        // 3. always_comb blocks
        for (auto& bp : processes) {
            if (bp.proc->type != ProcessType::COMBINATIONAL) continue;
            for (auto& stmt : bp.proc->body) {
                execute_statement(stmt.get(), bp);
            }
        }

        // Check if any net was modified in this pass
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
            LogicVal eq_check = LogicVal::logical_eq(sel_val, item_val);
            if (eq_check.to_bool()) {
                for (auto& s : item.statements) execute_statement(s.get(), bp);
                branch_taken = true;
                break;
            }
        }

        if (!branch_taken && default_item) {
            for (auto& s : default_item->statements) execute_statement(s.get(), bp);
        }
    }
    else if (auto disp = dynamic_cast<DisplayStatementNode*>(stmt)) {
        std::cout << "[CAS $display] ";
        size_t arg_idx = 0;
        for (size_t i = 0; i < disp->format_string.size(); ++i) {
            if (disp->format_string[i] == '%' && i + 1 < disp->format_string.size()) {
                char spec = disp->format_string[++i];
                if (spec == '0' && i + 1 < disp->format_string.size() && disp->format_string[i + 1] == 't') {
                    spec = 't';
                    i++;
                }

                if (arg_idx < disp->args.size()) {
                    std::string arg_name = disp->args[arg_idx++];
                    if (arg_name == "$time") {
                        std::cout << sim_time;
                    } else {
                        std::string scoped_arg = bp.scope + "." + arg_name;
                        size_t net_id = net_map.at(scoped_arg);
                        const auto& net = netlist[net_id];

                        if (net.value.is_unknown()) {
                            std::cout << "x";
                        } else {
                            if (spec == 'd' || spec == 't') std::cout << net.value.aval;
                            else if (spec == 'b') std::cout << net.value.to_bin_string();
                            else if (spec == 'h') std::cout << std::hex << net.value.aval << std::dec;
                        }
                    }
                }
            } else {
                std::cout << disp->format_string[i];
            }
        }
        std::cout << "\n";
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
                    if (addr < mem.depth) {
                        mem.words[addr] = rhs_val;
                    }
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
                LogicVal slice(width, (rhs_val.aval >> shift) & mask, (rhs_val.bval >> shift) & mask);
                netlist[net_id].set(slice);
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

            if (sens.edge == EdgeType::POSEDGE && net.is_posedge()) {
                triggered = true;
                break;
            } else if (sens.edge == EdgeType::NEGEDGE && net.is_negedge()) {
                triggered = true;
                break;
            }
        }

        if (triggered) {
            for (auto& stmt : bp.proc->body) {
                execute_statement(stmt.get(), bp);
            }
        }
    }
}

void Engine::commit_nba_updates() {
    if (nba_queue.empty() && nba_mem_queue.empty()) return;

    for (const auto& update : nba_queue) {
        netlist[update.net_id].set(update.val);
    }
    nba_queue.clear();

    for (const auto& mem_up : nba_mem_queue) {
        if (memory_map.find(mem_up.mem_scoped_name) != memory_map.end()) {
            auto& mem = memory_map.at(mem_up.mem_scoped_name);
            if (mem_up.addr < mem.depth) {
                mem.words[mem_up.addr] = mem_up.val;
            }
        }
    }
    nba_mem_queue.clear();

    propagate_comb();
}

void Engine::elaborate_module(ModuleNode* mod, const std::string& scope,
                              const std::unordered_map<std::string, ModuleNode*>& module_defs) {
    for (auto& p : mod->ports) {
        uint32_t w = (p.msb >= p.lsb) ? (p.msb - p.lsb + 1) : 1;
        get_or_create_net(scope + "." + p.name, w);
    }

    for (auto& s : mod->signals) {
        uint32_t w = (s.msb >= s.lsb) ? (s.msb - s.lsb + 1) : 1;
        if (s.is_array) {
            std::string mem_scope = scope + "." + s.name;
            memory_map[mem_scope] = MemoryArray(mem_scope, w, s.array_depth);
        } else {
            get_or_create_net(scope + "." + s.name, w);
        }
    }

    for (auto& ca : mod->continuous_assigns) {
        std::string scoped_target = scope + "." + ca->target_name;
        size_t target_id = net_map.at(scoped_target);
        continuous_assigns.push_back({scope, target_id, ca->rhs.get()});
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

        elaborate_module(submod, child_scope, module_defs);

        for (auto& conn : inst->connections) {
            std::string parent_net_name = scope + "." + conn.connected_net;
            std::string child_net_name = child_scope + "." + conn.port_name;

            size_t parent_id = net_map.at(parent_net_name);
            size_t child_id = net_map.at(child_net_name);

            std::string dir = "input";
            for (auto& p : submod->ports) {
                if (p.name == conn.port_name) {
                    dir = p.direction;
                    break;
                }
            }

            if (dir == "input") {
                port_bindings.push_back({parent_id, child_id});
            } else {
                port_bindings.push_back({child_id, parent_id});
            }
        }
    }
}

void Engine::elaborate(std::vector<std::unique_ptr<ModuleNode>>& modules) {
    std::unordered_map<std::string, ModuleNode*> module_defs;
    for (auto& mod : modules) {
        module_defs[mod->name] = mod.get();
    }

    std::unordered_map<std::string, bool> is_instantiated;
    for (auto& mod : modules) {
        for (auto& inst : mod->instantiations) {
            is_instantiated[inst->module_type] = true;
        }
    }

    ModuleNode* top = nullptr;
    for (auto& mod : modules) {
        if (!is_instantiated[mod->name]) {
            top = mod.get();
            break;
        }
    }
    if (!top) top = modules.front().get();

    elaborate_module(top, top->name, module_defs);
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
            vcd_filename = df->filename;
            vcd_enabled = true;
            bp.pc++;
            continue;
        }

        if (dynamic_cast<DumpVarsStatementNode*>(stmt)) {
            init_vcd_header();
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
        dump_vcd_sample();

        for (auto& n : netlist) n.modified = false;

        bp.pc++;
    }

    bp.completed = true;
}

void Engine::run() {
    std::cout << "CAS (Cycle Accurate Simulator) v0.9.0 [Memory & Dataflow Kernel] Initiated\n";
    std::cout << "Hierarchical Nets Elaborated: " << netlist.size() << "\n";
    std::cout << "Port Interconnects Bound: " << port_bindings.size() << "\n";
    std::cout << "Continuous Assigns Bound: " << continuous_assigns.size() << "\n";
    std::cout << "Memory Arrays Allocated: " << memory_map.size() << "\n";
    std::cout << "--------------------------------------------------------\n";

    propagate_comb();

    for (size_t i = 0; i < processes.size(); ++i) {
        if (processes[i].proc->type == ProcessType::INITIAL) {
            step_process(i);
        }
    }

    while (simulation_active && !time_wheel.empty()) {
        SimEvent ev = time_wheel.top();
        time_wheel.pop();

        sim_time = ev.time;
        step_process(ev.process_id);
    }

    if (vcd_enabled && vcd_file.is_open()) {
        vcd_file.close();
        std::cout << "[CAS VCD] Trace successfully dumped to: " << vcd_filename << "\n";
    }

    std::cout << "--------------------------------------------------------\n";
    std::cout << "Simulation finished at sim_time = " << sim_time << ".\n";
}