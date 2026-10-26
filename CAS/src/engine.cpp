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
    if (auto un = dynamic_cast<UnaryOpNode*>(expr)) {
        LogicVal val = eval_expr(un->operand.get(), scope);
        if (un->op == '!') {
            if (val.is_unknown()) {
                return LogicVal::make_x(1);
            }
            return LogicVal::make_known(1, (val.aval == 0) ? 1 : 0);
        }
    }
    if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
        LogicVal l = eval_expr(bin->lhs.get(), scope);
        LogicVal r = eval_expr(bin->rhs.get(), scope);
        uint32_t res_width = std::max(l.width, r.width);

        // 4-state arithmetic poisoning: if either operand has X/Z, result is X
        if (l.is_unknown() || r.is_unknown()) {
            return LogicVal::make_x(res_width);
        }

        uint64_t res_val = (bin->op == '+') ? (l.aval + r.aval) : (l.aval - r.aval);
        return LogicVal::make_known(res_width, res_val);
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
    vcd_file << "$version\n   CAS (Cycle Accurate Simulator) v0.6.0 [4-State Engine]\n$end\n";
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
        for (auto& bind : port_bindings) {
            if (netlist[bind.dst_net_id].value != netlist[bind.src_net_id].value) {
                netlist[bind.dst_net_id].set(netlist[bind.src_net_id].value);
                changed = true;
            }
        }

        for (auto& bp : processes) {
            if (bp.proc->type != ProcessType::COMBINATIONAL) continue;
            for (auto& stmt : bp.proc->body) {
                if (auto assign = dynamic_cast<BlockingAssignNode*>(stmt.get())) {
                    LogicVal rhs_val = eval_expr(assign->rhs.get(), bp.scope);
                    if (assign->lhs_targets.size() > 1) {
                        int shift = 0;
                        for (int i = (int)assign->lhs_targets.size() - 1; i >= 0; --i) {
                            std::string scoped_target = bp.scope + "." + assign->lhs_targets[i];
                            size_t net_id = net_map.at(scoped_target);
                            uint32_t width = netlist[net_id].width;
                            uint64_t mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);

                            LogicVal slice(width, (rhs_val.aval >> shift) & mask, (rhs_val.bval >> shift) & mask);
                            if (netlist[net_id].value != slice) {
                                netlist[net_id].set(slice);
                                changed = true;
                            }
                            shift += width;
                        }
                    } else {
                        std::string scoped_target = bp.scope + "." + assign->lhs_targets[0];
                        size_t net_id = net_map.at(scoped_target);
                        if (netlist[net_id].value != rhs_val) {
                            netlist[net_id].set(rhs_val);
                            changed = true;
                        }
                    }
                }
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
    else if (auto nassign = dynamic_cast<NonBlockingAssignNode*>(stmt)) {
        LogicVal rhs_val = eval_expr(nassign->rhs.get(), bp.scope);
        std::string scoped_target = bp.scope + "." + nassign->target;
        size_t net_id = net_map.at(scoped_target);
        nba_queue.push_back({net_id, rhs_val});
    }
    else if (auto bassign = dynamic_cast<BlockingAssignNode*>(stmt)) {
        LogicVal rhs_val = eval_expr(bassign->rhs.get(), bp.scope);
        std::string scoped_target = bp.scope + "." + bassign->lhs_targets[0];
        size_t net_id = net_map.at(scoped_target);
        netlist[net_id].set(rhs_val);
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
    if (nba_queue.empty()) return;

    for (const auto& update : nba_queue) {
        netlist[update.net_id].set(update.val);
    }
    nba_queue.clear();

    propagate_comb();
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

    // 1. Elaborate Root Nets & Processes
    std::string root_scope = top->name;
    for (auto& s : top->signals) {
        uint32_t w = (s.msb >= s.lsb) ? (s.msb - s.lsb + 1) : 1;
        get_or_create_net(root_scope + "." + s.name, w);
    }
    for (auto& p : top->ports) {
        uint32_t w = (p.msb >= p.lsb) ? (p.msb - p.lsb + 1) : 1;
        get_or_create_net(root_scope + "." + p.name, w);
    }
    for (auto& proc : top->processes) {
        processes.push_back({root_scope, proc.get(), 0, false});
    }

    // 2. Elaborate Submodules
    for (auto& inst : top->instantiations) {
        std::string inst_scope = root_scope + "." + inst->instance_name;
        if (module_defs.find(inst->module_type) == module_defs.end()) {
            throw std::runtime_error("Undefined module type instantiated: " + inst->module_type);
        }
        ModuleNode* submod = module_defs[inst->module_type];

        for (auto& p : submod->ports) {
            uint32_t w = (p.msb >= p.lsb) ? (p.msb - p.lsb + 1) : 1;
            get_or_create_net(inst_scope + "." + p.name, w);
        }
        for (auto& s : submod->signals) {
            uint32_t w = (s.msb >= s.lsb) ? (s.msb - s.lsb + 1) : 1;
            get_or_create_net(inst_scope + "." + s.name, w);
        }
        for (auto& proc : submod->processes) {
            processes.push_back({inst_scope, proc.get(), 0, false});
        }

        // Bind Port Interconnects
        for (auto& conn : inst->connections) {
            std::string parent_net_name = root_scope + "." + conn.connected_net;
            std::string child_net_name = inst_scope + "." + conn.port_name;

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

void Engine::step_process(size_t pid) {
    auto& bp = processes[pid];
    if (bp.completed) return;

    while (bp.pc < bp.proc->body.size()) {
        auto stmt = bp.proc->body[bp.pc].get();

        // Delay
        if (auto delay = dynamic_cast<DelayStatementNode*>(stmt)) {
            bp.pc++;
            uint64_t wake_time = sim_time + delay->delay;
            time_wheel.push({wake_time, (uint32_t)pid, event_order_counter++});
            return;
        }

        // $dumpfile
        if (auto df = dynamic_cast<DumpFileStatementNode*>(stmt)) {
            vcd_filename = df->filename;
            vcd_enabled = true;
            bp.pc++;
            continue;
        }

        // $dumpvars
        if (dynamic_cast<DumpVarsStatementNode*>(stmt)) {
            init_vcd_header();
            bp.pc++;
            continue;
        }

        // Blocking Assignment
        if (auto assign = dynamic_cast<BlockingAssignNode*>(stmt)) {
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

            propagate_comb();
            evaluate_sequential_blocks();
            commit_nba_updates();
            dump_vcd_sample();

            for (auto& n : netlist) n.modified = false;
            bp.pc++;
        }
        // $display
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
            bp.pc++;
        }
        // $finish
        else if (dynamic_cast<FinishStatementNode*>(stmt)) {
            std::cout << "[CAS $finish] Simulation Terminated at time " << sim_time << ".\n";
            simulation_active = false;
            bp.completed = true;
            return;
        }
        else {
            bp.pc++;
        }
    }

    bp.completed = true;
}

void Engine::run() {
    std::cout << "CAS (Cycle Accurate Simulator) v0.6.0 [4-State Kernel] Initiated\n";
    std::cout << "Hierarchical Nets Elaborated: " << netlist.size() << "\n";
    std::cout << "Port Interconnects Bound: " << port_bindings.size() << "\n";
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
        std::cout << "[CAS VCD] 4-State trace successfully dumped to: " << vcd_filename << "\n";
    }

    std::cout << "--------------------------------------------------------\n";
    std::cout << "Simulation finished at sim_time = " << sim_time << ".\n";
}