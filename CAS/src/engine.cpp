#include "../include/engine.hpp"
#include <iomanip>
#include <bitset>
#include <stdexcept>

size_t Engine::get_or_create_net(const std::string& scoped_name, uint32_t width) {
    if (net_map.find(scoped_name) != net_map.end()) {
        return net_map[scoped_name];
    }
    size_t id = netlist.size();
    netlist.emplace_back(scoped_name, width);
    net_map[scoped_name] = id;
    return id;
}

uint64_t Engine::eval_expr(ExpressionNode* expr, const std::string& scope) {
    if (auto num = dynamic_cast<NumberNode*>(expr)) {
        return num->value;
    }
    if (auto ident = dynamic_cast<IdentifierNode*>(expr)) {
        std::string scoped_ident = scope.empty() ? ident->name : (scope + "." + ident->name);
        return netlist[net_map.at(scoped_ident)].value;
    }
    if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
        uint64_t l = eval_expr(bin->lhs.get(), scope);
        uint64_t r = eval_expr(bin->rhs.get(), scope);
        return (bin->op == '+') ? (l + r) : (l - r);
    }
    return 0;
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

    // 1. Elaborate Top-Level Nets
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
        processes.push_back({root_scope, proc.get()});
    }

    // 2. Elaborate Submodule Instances
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
            processes.push_back({inst_scope, proc.get()});
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

void Engine::run() {
    std::cout << "CAS (Cycle Accurate Simulator) v0.2.0 Initiated\n";
    std::cout << "Hierarchical Nets Elaborated: " << netlist.size() << "\n";
    std::cout << "Port Interconnects Bound: " << port_bindings.size() << "\n";
    std::cout << "--------------------------------------------------------\n";

    // Helper: propagate ports and evaluate all always_comb blocks until stable
    auto propagate_comb = [&]() {
        bool changed = true;
        size_t iters = 0;
        while (changed && iters < 100) {
            changed = false;
            // 1. Propagate port bindings
            for (auto& bind : port_bindings) {
                if (netlist[bind.dst_net_id].value != netlist[bind.src_net_id].value) {
                    netlist[bind.dst_net_id].set(netlist[bind.src_net_id].value);
                    changed = true;
                }
            }
            // 2. Evaluate all always_comb blocks
            for (auto& bp : processes) {
                if (!bp.proc->is_combinational) continue;
                for (auto& stmt : bp.proc->body) {
                    if (auto assign = dynamic_cast<BlockingAssignNode*>(stmt.get())) {
                        uint64_t rhs_val = eval_expr(assign->rhs.get(), bp.scope);
                        if (assign->lhs_targets.size() > 1) {
                            int shift = 0;
                            for (int i = (int)assign->lhs_targets.size() - 1; i >= 0; --i) {
                                std::string scoped_target = bp.scope + "." + assign->lhs_targets[i];
                                size_t net_id = net_map.at(scoped_target);
                                uint32_t width = netlist[net_id].width;
                                uint64_t mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
                                uint64_t slice = (rhs_val >> shift) & mask;
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
    };

    // Run initial / testbench processes
    for (auto& bp : processes) {
        if (bp.proc->is_combinational) continue; // Initial blocks only

        for (auto& stmt : bp.proc->body) {
            if (auto assign = dynamic_cast<BlockingAssignNode*>(stmt.get())) {
                uint64_t rhs_val = eval_expr(assign->rhs.get(), bp.scope);

                if (assign->lhs_targets.size() > 1) {
                    int shift = 0;
                    for (int i = (int)assign->lhs_targets.size() - 1; i >= 0; --i) {
                        std::string scoped_target = bp.scope + "." + assign->lhs_targets[i];
                        size_t net_id = net_map.at(scoped_target);
                        uint32_t width = netlist[net_id].width;
                        uint64_t mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
                        netlist[net_id].set((rhs_val >> shift) & mask);
                        shift += width;
                    }
                } else {
                    std::string scoped_target = bp.scope + "." + assign->lhs_targets[0];
                    size_t net_id = net_map.at(scoped_target);
                    netlist[net_id].set(rhs_val);
                }

                // Immediate combinational delta convergence
                propagate_comb();
            }
            else if (auto disp = dynamic_cast<DisplayStatementNode*>(stmt.get())) {
                std::cout << "[CAS $display] ";
                size_t arg_idx = 0;
                for (size_t i = 0; i < disp->format_string.size(); ++i) {
                    if (disp->format_string[i] == '%' && i + 1 < disp->format_string.size()) {
                        char spec = disp->format_string[++i];
                        if (arg_idx < disp->args.size()) {
                            std::string scoped_arg = bp.scope + "." + disp->args[arg_idx++];
                            size_t net_id = net_map.at(scoped_arg);
                            if (spec == 'd') std::cout << netlist[net_id].value;
                            else if (spec == 'b') std::cout << std::bitset<8>(netlist[net_id].value);
                            else if (spec == 'h') std::cout << std::hex << netlist[net_id].value << std::dec;
                        }
                    } else {
                        std::cout << disp->format_string[i];
                    }
                }
                std::cout << "\n";
            }
            else if (dynamic_cast<FinishStatementNode*>(stmt.get())) {
                std::cout << "[CAS $finish] Simulation Terminated Gracefully.\n";
                return;
            }
        }
    }
}