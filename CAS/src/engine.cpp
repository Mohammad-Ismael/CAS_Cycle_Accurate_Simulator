#include "../include/engine.hpp"

Engine::Engine() : evaluator(*this), scheduler(*this) {}

void Engine::load_elaboration(ElaborationResult&& elab_res) {
    netlist = std::move(elab_res.netlist);
    net_map = std::move(elab_res.net_map);
    slice_bindings = std::move(elab_res.slice_bindings);
    continuous_assigns = std::move(elab_res.continuous_assigns);
    resolved_continuous_assigns.clear();
    std::unordered_map<uint64_t, size_t> slice_to_rca;
    for (const auto& ca : continuous_assigns) {
        uint64_t key = ((uint64_t)ca.target.net_id << 32) | ((uint64_t)ca.target.offset << 16) | (uint64_t)ca.target.width;
        auto it = slice_to_rca.find(key);
        if (it == slice_to_rca.end()) {
            size_t idx = resolved_continuous_assigns.size();
            slice_to_rca[key] = idx;
            ResolvedContinuousAssign rca;
            rca.target = ca.target;
            rca.drivers.push_back({ca.scope, ca.rhs});
            resolved_continuous_assigns.push_back(std::move(rca));
        } else {
            resolved_continuous_assigns[it->second].drivers.push_back({ca.scope, ca.rhs});
        }
    }
    gates = std::move(elab_res.gates);
    memory_map = std::move(elab_res.memory_map);
    queue_map = std::move(elab_res.queue_map);
    dynamic_array_map = std::move(elab_res.dynamic_array_map);
    task_map = std::move(elab_res.task_map);
    param_map = std::move(elab_res.param_map);
    processes = std::move(elab_res.processes);
    function_map = std::move(elab_res.function_map);
    named_events = std::move(elab_res.named_events);
    named_event_map = std::move(elab_res.named_event_map);
    enum_types = std::move(elab_res.enum_types);
    var_to_enum_type = std::move(elab_res.var_to_enum_type);
    enum_constant_map = std::move(elab_res.enum_constant_map);
    struct_types = std::move(elab_res.struct_types);
    var_to_struct_type = std::move(elab_res.var_to_struct_type);
    interface_port_map = std::move(elab_res.interface_port_map);
}

void Engine::elaborate(std::vector<std::unique_ptr<ModuleNode>>& modules,
                       const std::string& top_module_name,
                       std::ostream* debug_out) {
    Elaborator elaborator;
    load_elaboration(elaborator.elaborate(modules, top_module_name, debug_out));
}

std::string Engine::resolve_scoped_name(const std::string& scope, const std::string& name) const {
    auto dot_pos = name.find('.');
    if (dot_pos != std::string::npos) {
        std::string prefix = name.substr(0, dot_pos);
        std::string suffix = name.substr(dot_pos + 1);
        std::string scoped_prefix = scope.empty() ? prefix : (scope + "." + prefix);

        auto it = interface_port_map.find(scoped_prefix);
        if (it != interface_port_map.end()) {
            return it->second + "." + suffix;
        }
        auto it2 = interface_port_map.find(prefix);
        if (it2 != interface_port_map.end()) {
            return it2->second + "." + suffix;
        }
    }
    return scope.empty() ? name : (scope + "." + name);
}

size_t Engine::get_net_id(const std::string& scope, const std::string& name) const {
    std::string resolved = resolve_scoped_name(scope, name);
    auto it = net_map.find(resolved);
    if (it != net_map.end()) return it->second;

    auto it2 = net_map.find(name);
    if (it2 != net_map.end()) return it2->second;

    std::string scoped = scope.empty() ? name : (scope + "." + name);
    auto it3 = net_map.find(scoped);
    if (it3 != net_map.end()) return it3->second;

    return (size_t)-1;
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
        *debug_out << "[PHASE 5: Simulation Runtime (Modular IEEE 1800 Kernel)]\n";
        *debug_out << "--------------------------------------------------------\n";
    }

    scheduler.run_simulation();

    if (vcd_dumper.is_enabled()) {
        std::string dumped_file = vcd_dumper.get_filename();
        vcd_dumper.close();
        if (allow_finish_msg) {
            std::cout << "[CAS VCD] Trace successfully dumped to: " << dumped_file << "\n";
        }
    }

    if (allow_finish_msg) {
        std::cout << "--------------------------------------------------------\n";
        std::cout << "[CAS SUMMARY] Simulation completed cleanly at T = " << scheduler.get_sim_time() << " ns.\n" << std::flush;
    }
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

    s.events_dispatched = scheduler.get_events_dispatched();
    s.delta_cycles = scheduler.get_delta_cycles();
    s.nba_commits = scheduler.get_nba_commits();
    return s;
}