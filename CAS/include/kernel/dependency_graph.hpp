#pragma once
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

class Engine;
struct ResolvedContinuousAssign;
struct BoundProcess;

struct TopologicalCombOrder {
    std::vector<uint32_t> topological_rca_order; // Inlined acyclic RCAs in strict topological order
    std::vector<uint32_t> cyclic_rcas;            // Cycle-inducing RCAs (dynamic fallback)
    std::vector<uint32_t> non_compiled_rcas;      // Non-compiled / 4-state RCAs
    bool is_purely_acyclic = true;
};

class DependencyGraph {
public:
    static TopologicalCombOrder build_and_sort_rcas(
        const std::vector<ResolvedContinuousAssign>& rcas,
        const Engine& engine
    );
};
