#include "../include/engine.hpp"
#include <iomanip>
#include <bitset>

size_t Engine::get_or_create_net(const std::string& name, uint32_t width) {
    if (net_map.find(name) != net_map.end()) {
        return net_map[name];
    }
    size_t id = netlist.size();
    netlist.emplace_back(name, width);
    net_map[name] = id;
    return id;
}

uint64_t Engine::eval_expr(ExpressionNode* expr) {
    if (auto num = dynamic_cast<NumberNode*>(expr)) {
        return num->value;
    }
    if (auto ident = dynamic_cast<IdentifierNode*>(expr)) {
        return netlist[net_map.at(ident->name)].value;
    }
    if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
        uint64_t l = eval_expr(bin->lhs.get());
        uint64_t r = eval_expr(bin->rhs.get());
        return (bin->op == '+') ? (l + r) : (l - r);
    }
    return 0;
}

void Engine::elaborate(std::vector<std::unique_ptr<ModuleNode>>& modules) {
    for (auto& mod : modules) {
        // Register Ports
        for (auto& p : mod->ports) {
            uint32_t w = (p.msb >= p.lsb) ? (p.msb - p.lsb + 1) : 1;
            get_or_create_net(p.name, w);
        }
        // Register Internal Nets
        for (auto& s : mod->signals) {
            uint32_t w = (s.msb >= s.lsb) ? (s.msb - s.lsb + 1) : 1;
            get_or_create_net(s.name, w);
        }
        // Extract processes
        for (auto& proc : mod->processes) {
            active_processes.push_back(std::move(proc));
        }
    }
}

void Engine::run() {
    std::cout << "CAS (Cycle Accurate Simulator) v1.0.0 Initiated\n";
    std::cout << "Elaborated Netlist Size: " << netlist.size() << " nets.\n";
    std::cout << "--------------------------------------------------------\n";

    // Delta-Cycle Stabilization Loop
    size_t delta_cycles = 0;
    const size_t MAX_DELTAS = 10000;

    while (simulation_active && delta_cycles < MAX_DELTAS) {
        bool any_net_changed = false;

        for (auto& proc : active_processes) {
            for (auto& stmt : proc->body) {
                if (auto assign = dynamic_cast<BlockingAssignNode*>(stmt.get())) {
                    uint64_t rhs_val = eval_expr(assign->rhs.get());

                    // Support unpacking dynamic bit slices: {cout, sum}
                    if (assign->lhs_targets.size() > 1) {
                        int shift = 0;
                        for (int i = (int)assign->lhs_targets.size() - 1; i >= 0; --i) {
                            size_t net_id = net_map.at(assign->lhs_targets[i]);
                            uint32_t width = netlist[net_id].width;
                            uint64_t mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
                            netlist[net_id].set((rhs_val >> shift) & mask);
                            shift += width;
                        }
                    } else {
                        size_t net_id = net_map.at(assign->lhs_targets[0]);
                        netlist[net_id].set(rhs_val);
                    }
                }
                else if (auto disp = dynamic_cast<DisplayStatementNode*>(stmt.get())) {
                    std::cout << "[CAS $display] ";
                    size_t arg_idx = 0;
                    for (size_t i = 0; i < disp->format_string.size(); ++i) {
                        if (disp->format_string[i] == '%' && i + 1 < disp->format_string.size()) {
                            char spec = disp->format_string[++i];
                            if (arg_idx < disp->args.size()) {
                                size_t net_id = net_map.at(disp->args[arg_idx++]);
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
                    simulation_active = false;
                    std::cout << "[CAS $finish] Simulation Terminated Gracefully.\n";
                    return;
                }
            }
        }

        for (auto& net : netlist) {
            if (net.modified) {
                any_net_changed = true;
                net.modified = false;
            }
        }

        delta_cycles++;
        if (!any_net_changed) break; // Reached combinational equilibrium
    }

    std::cout << "--------------------------------------------------------\n";
    std::cout << "Simulation converged in " << delta_cycles << " delta-cycle(s).\n";
}