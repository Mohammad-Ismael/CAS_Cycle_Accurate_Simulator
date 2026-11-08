#include "../../include/kernel/dependency_graph.hpp"
#include "../../include/engine.hpp"
#include "../../include/kernel/elaborator.hpp"
#include "../../include/kernel/bytecode.hpp"
#include <queue>
#include <algorithm>
#include <iostream>

TopologicalCombOrder DependencyGraph::build_and_sort_rcas(
    const std::vector<ResolvedContinuousAssign>& rcas,
    const Engine& engine
) {
    TopologicalCombOrder order;
    std::vector<uint32_t> candidate_indices;
    std::unordered_map<uint32_t, std::vector<uint32_t>> net_to_rca_drivers;

    // 1. Partition candidate 2-state 64-bit RCAs vs non-compiled
    for (size_t i = 0; i < rcas.size(); ++i) {
        const auto& rca = rcas[i];
        if (rca.compiled_expr.is_compiled &&
            rca.compiled_expr.is_pure_2state_64 &&
            rca.target.width <= 64 &&
            rca.target.offset < 64 &&
            (rca.target.offset + rca.target.width) <= 64) {
            candidate_indices.push_back(static_cast<uint32_t>(i));

            uint32_t canon_dst = (rca.target.net_id < engine.get_canonical_net_map().size()) ?
                                  static_cast<uint32_t>(engine.get_canonical_net_map()[rca.target.net_id]) :
                                  rca.target.net_id;
            net_to_rca_drivers[canon_dst].push_back(static_cast<uint32_t>(i));
            if (canon_dst < engine.get_alias_members().size()) {
                for (size_t other_id : engine.get_alias_members()[canon_dst]) {
                    if (other_id != canon_dst) {
                        net_to_rca_drivers[static_cast<uint32_t>(other_id)].push_back(static_cast<uint32_t>(i));
                    }
                }
            }
        } else {
            order.non_compiled_rcas.push_back(static_cast<uint32_t>(i));
        }
    }

    // 2. Build dependency graph between candidates
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> adj;
    std::unordered_map<uint32_t, uint32_t> in_degree;
    std::unordered_set<uint32_t> self_cyclic;

    for (uint32_t v : candidate_indices) {
        in_degree[v] = 0;
    }

    for (uint32_t v : candidate_indices) {
        const auto& rca = rcas[v];
        std::unordered_set<uint32_t> read_nets;
        for (const auto& inst : rca.compiled_expr.instructions) {
            if (inst.op == Opcode::DONE) break;
            if (inst.op == Opcode::PUSH_NET_U64 || inst.op == Opcode::PUSH_NET_VAL) {
                uint32_t raw_id = inst.arg0;
                uint32_t canon_id = (raw_id < engine.get_canonical_net_map().size()) ?
                                    static_cast<uint32_t>(engine.get_canonical_net_map()[raw_id]) : raw_id;
                read_nets.insert(canon_id);
            }
        }

        for (uint32_t src_net : read_nets) {
            auto it = net_to_rca_drivers.find(src_net);
            if (it != net_to_rca_drivers.end()) {
                for (uint32_t u : it->second) {
                    if (u != v) {
                        // Directed edge: u -> v (u produces, v consumes)
                        if (adj[u].insert(v).second) {
                            in_degree[v]++;
                        }
                    } else {
                        self_cyclic.insert(v);
                    }
                }
            }
        }
    }

    // 3. Kahn's Algorithm using a min-priority queue for deterministic stable ordering
    std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> pq;
    for (uint32_t idx : candidate_indices) {
        if (!self_cyclic.count(idx) && in_degree[idx] == 0) {
            pq.push(idx);
        }
    }

    order.topological_rca_order.reserve(candidate_indices.size());
    while (!pq.empty()) {
        uint32_t u = pq.top();
        pq.pop();
        order.topological_rca_order.push_back(u);

        auto it = adj.find(u);
        if (it != adj.end()) {
            for (uint32_t v : it->second) {
                if (--in_degree[v] == 0 && !self_cyclic.count(v)) {
                    pq.push(v);
                }
            }
        }
    }

    // 4. Identify cyclic or unvisited nodes
    std::unordered_set<uint32_t> visited(order.topological_rca_order.begin(), order.topological_rca_order.end());
    for (uint32_t idx : candidate_indices) {
        if (!visited.count(idx)) {
            order.cyclic_rcas.push_back(idx);
        }
    }

    order.is_purely_acyclic = order.cyclic_rcas.empty();

    std::cout << "[DependencyGraph] Extracted " << candidate_indices.size()
              << " JIT RCA candidates: " << order.topological_rca_order.size()
              << " topologically sorted (acyclic), " << order.cyclic_rcas.size()
              << " cyclic fallback, " << order.non_compiled_rcas.size() << " non-compiled.\n";

    return order;
}
