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
    assoc_array_map = std::move(elab_res.assoc_array_map);
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
    modport_binding_map = std::move(elab_res.modport_binding_map);
    clocking_blocks = std::move(elab_res.clocking_blocks);
    clocking_block_map = std::move(elab_res.clocking_block_map);
    default_clocking_map = std::move(elab_res.default_clocking_map);
    class_defs = std::move(elab_res.class_defs);
    class_handle_names = std::move(elab_res.class_handle_names);
    var_to_class_type = std::move(elab_res.var_to_class_type);
    covergroup_defs = std::move(elab_res.covergroup_defs);
    covergroup_handle_names = std::move(elab_res.covergroup_handle_names);
    var_to_covergroup_type = std::move(elab_res.var_to_covergroup_type);
    sva_assertions = std::move(elab_res.sva_assertions);
    property_defs = std::move(elab_res.property_defs);
    sequence_defs = std::move(elab_res.sequence_defs);
    assertion_names = std::move(elab_res.assertion_names);
    property_names = std::move(elab_res.property_names);
    sequence_names = std::move(elab_res.sequence_names);
    dpi_import_map = std::move(elab_res.dpi_import_map);
    dpi_export_map = std::move(elab_res.dpi_export_map);
    chandle_names = std::move(elab_res.chandle_names);
    open_array_names = std::move(elab_res.open_array_names);
    active_scope_name = elab_res.top_scope;
    DpiRegistry::set_active_scope_by_name(active_scope_name);
    DpiRegistry::set_active_engine(this);
}

LogicVal Engine::dispatch_dpi_export(const std::string& c_name, const std::vector<LogicVal>& args) {
    std::string scope = DpiRegistry::get_active_scope_name();
    if (scope.empty()) {
        scope = active_scope_name;
    }

    std::string sv_name = c_name;
    bool is_task = false;

    std::string scoped_export = scope.empty() ? c_name : (scope + "." + c_name);
    if (dpi_export_map.find(scoped_export) != dpi_export_map.end()) {
        sv_name = dpi_export_map[scoped_export]->sv_name;
        is_task = dpi_export_map[scoped_export]->is_task;
    } else if (dpi_export_map.find(c_name) != dpi_export_map.end()) {
        sv_name = dpi_export_map[c_name]->sv_name;
        is_task = dpi_export_map[c_name]->is_task;
    }

    std::string scoped_target = scope.empty() ? sv_name : (scope + "." + sv_name);
    FunctionNode* fn = nullptr;
    if (function_map.find(scoped_target) != function_map.end()) {
        fn = function_map[scoped_target];
    } else if (function_map.find(sv_name) != function_map.end()) {
        fn = function_map[sv_name];
    }

    if (fn) {
        LogicVal ret = LogicVal::make_known(fn->return_width, 0);
        bool ok = evaluator.get_expr_eval().eval_function(scoped_target, args, ret);
        if (!ok) {
            evaluator.get_expr_eval().eval_function(fn->name, args, ret);
        }
        return ret;
    }

    TaskNode* tn = nullptr;
    if (task_map.find(scoped_target) != task_map.end()) {
        tn = task_map[scoped_target];
    } else if (task_map.find(sv_name) != task_map.end()) {
        tn = task_map[sv_name];
    }

    if (tn) {
        BoundProcess bp{scope.empty() ? tn->scope : scope, nullptr, 0, false};
        for (auto& s : tn->body) {
            evaluator.execute_statement(s.get(), bp);
            if (!scheduler.is_active()) break;
        }
        return LogicVal(0, 0, 0);
    }

    throw std::runtime_error("[DPI-FNF] Exported routine '" + c_name + "' (SV target: '" + sv_name + "') not found in scope '" + scope + "'");
}

extern "C" {
svScope svGetScope(void) {
    return DpiRegistry::get_active_scope();
}

svScope svSetScope(const svScope scope) {
    return DpiRegistry::set_active_scope(scope);
}

void sv_write_reg(int addr, int data) {
    Engine* eng = static_cast<Engine*>(DpiRegistry::get_active_engine());
    if (!eng) {
        throw std::runtime_error("[CAS-DPI-ERR] sv_write_reg called without active simulation engine context");
    }
    eng->dispatch_dpi_export("sv_write_reg", {LogicVal(32, static_cast<uint32_t>(addr)), LogicVal(32, static_cast<uint32_t>(data))});
}

int sv_read_reg(int addr) {
    Engine* eng = static_cast<Engine*>(DpiRegistry::get_active_engine());
    if (!eng) {
        throw std::runtime_error("[CAS-DPI-ERR] sv_read_reg called without active simulation engine context");
    }
    LogicVal res = eng->dispatch_dpi_export("sv_read_reg", {LogicVal(32, static_cast<uint32_t>(addr))});
    return static_cast<int>(static_cast<int32_t>(res.aval));
}

// IEEE Std 1800-2023 Annex I.9 DPI-C Open Array Inquiry & Access APIs
int svDimensions(const void* h) {
    if (!h) return 0;
    return 1;
}

int svLeft(const void* h, int d) {
    if (!h || d != 1) return 0;
    return 0;
}

int svRight(const void* h, int d) {
    if (!h || d != 1) return 0;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(h);
    return arr->size > 0 ? arr->size - 1 : 0;
}

int svLow(const void* h, int d) {
    if (!h || d != 1) return 0;
    return 0;
}

int svHigh(const void* h, int d) {
    if (!h || d != 1) return 0;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(h);
    return arr->size > 0 ? arr->size - 1 : 0;
}

int svIncrement(const void* h, int d) {
    return 1;
}

int svLength(const void* h, int d) {
    if (!h || d != 1) return 0;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(h);
    return arr->size;
}

int svSize(const void* h, int d) {
    if (!h || d != 1) return 0;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(h);
    return arr->size;
}

int svSizeOfArray(const void* h) {
    if (!h) return 0;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(h);
    return arr->size;
}

void* svGetArrElemPtr1(const void* h, int indx) {
    if (!h) return nullptr;
    SvOpenArray* arr = const_cast<SvOpenArray*>(reinterpret_cast<const SvOpenArray*>(h));
    if (indx < 0 || indx >= arr->size) return nullptr;
    if (arr->elem_width <= 32) {
        return static_cast<void*>(&(arr->int32_elements[indx]));
    } else {
        return static_cast<void*>(&(arr->int64_elements[indx]));
    }
}

void* svGetArrElemPtr2(const void* h, int indx1, int /*indx2*/) {
    return svGetArrElemPtr1(h, indx1);
}

void* svGetArrElemPtr3(const void* h, int indx1, int /*indx2*/, int /*indx3*/) {
    return svGetArrElemPtr1(h, indx1);
}

void* svGetArrElemPtr(const void* h, int indx1, ...) {
    return svGetArrElemPtr1(h, indx1);
}

void* svGetArrayPtr(const void* h) {
    return svGetArrElemPtr1(h, 0);
}
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

size_t Engine::get_clocking_block_id(const std::string& cb_name, const std::string& scope) const {
    if (!cb_name.empty()) {
        std::string scoped = scope.empty() ? cb_name : (scope + "." + cb_name);
        auto it = clocking_block_map.find(scoped);
        if (it != clocking_block_map.end()) return it->second;
        auto it2 = clocking_block_map.find(cb_name);
        if (it2 != clocking_block_map.end()) return it2->second;
    } else {
        auto it = default_clocking_map.find(scope);
        if (it != default_clocking_map.end()) return it->second;
        if (!default_clocking_map.empty()) return default_clocking_map.begin()->second;
    }
    return (size_t)-1;
}

bool Engine::is_clocking_block(const std::string& name) const {
    return clocking_block_map.find(name) != clocking_block_map.end();
}