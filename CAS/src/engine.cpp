#include "../include/engine.hpp"
#include <iomanip>
#include <stdexcept>

Engine::Engine() : evaluator(*this) {}

void Engine::load_elaboration(ElaborationResult&& elab_res) {
    netlist = std::move(elab_res.netlist);
    net_map = std::move(elab_res.net_map);
    slice_bindings = std::move(elab_res.slice_bindings);
    continuous_assigns = std::move(elab_res.continuous_assigns);
    gates = std::move(elab_res.gates);
    memory_map = std::move(elab_res.memory_map);
    task_map = std::move(elab_res.task_map);
    param_map = std::move(elab_res.param_map);
    processes = std::move(elab_res.processes);
}

void Engine::elaborate(std::vector<std::unique_ptr<ModuleNode>>& modules,
                       const std::string& top_module_name,
                       std::ostream* debug_out) {
    Elaborator elaborator;
    load_elaboration(elaborator.elaborate(modules, top_module_name, debug_out));
}

void Engine::propagate_comb() {
    delta_cycles_count++;
    bool changed = true;
    size_t iters = 0;
    while (changed && iters < 200) {
        changed = false;

        for (auto& bind : slice_bindings) {
            LogicVal src_val = evaluator.read_slice(bind.src);
            LogicVal dst_val = evaluator.read_slice(bind.dst);
            if (src_val != dst_val) {
                evaluator.write_slice(bind.dst, src_val);
                changed = true;
            }
        }

        for (auto& g : gates) {
            LogicVal res = evaluator.eval_expr(g.in_exprs[0], g.scope);
            for (size_t i = 1; i < g.in_exprs.size(); ++i) {
                LogicVal next_in = evaluator.eval_expr(g.in_exprs[i], g.scope);
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
            LogicVal cur_out = evaluator.read_slice(g.out_target);
            if (cur_out != res) {
                evaluator.write_slice(g.out_target, res);
                changed = true;
            }
        }

        for (auto& ca : continuous_assigns) {
            LogicVal val = evaluator.eval_expr(ca.rhs, ca.scope);
            LogicVal cur = evaluator.read_slice(ca.target);
            if (cur != val) {
                evaluator.write_slice(ca.target, val);
                changed = true;
            }
        }

        for (auto& bp : processes) {
            if (bp.proc->type != ProcessType::COMBINATIONAL) continue;
            for (auto& stmt : bp.proc->body) {
                evaluator.execute_statement(stmt.get(), bp);
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

void Engine::evaluate_sequential_blocks() {
    for (auto& bp : processes) {
        if (bp.proc->type != ProcessType::SEQUENTIAL) continue;

        bool triggered = false;
        for (auto& sens : bp.proc->sensitivity_list) {
            std::string scoped_sig = bp.scope + "." + sens.signal_name;
            auto it = net_map.find(scoped_sig);
            if (it == net_map.end()) continue;
            const auto& net = netlist[it->second];

            if (sens.edge == EdgeType::POSEDGE && net.is_posedge()) { triggered = true; break; }
            else if (sens.edge == EdgeType::NEGEDGE && net.is_negedge()) { triggered = true; break; }
        }

        if (triggered) {
            for (auto& stmt : bp.proc->body) {
                evaluator.execute_statement(stmt.get(), bp);
            }
        }
    }
}

bool Engine::commit_nba_updates() {
    if (nba_queue.empty() && nba_mem_queue.empty()) return false;

    nba_commits_count++;
    for (const auto& update : nba_queue) {
        netlist[update.net_id].set(update.val);
    }
    nba_queue.clear();

    for (const auto& mem_up : nba_mem_queue) {
        if (memory_map.find(mem_up.mem_scoped_name) != memory_map.end()) {
            auto& mem = memory_map.at(mem_up.mem_scoped_name);
            if (mem_up.addr < mem.depth) mem.words[mem_up.addr] = mem_up.val;
        }
    }
    nba_mem_queue.clear();
    return true;
}

void Engine::step_process(size_t pid) {
    if (!simulation_active) return;
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

        if (auto dv = dynamic_cast<DumpVarsStatementNode*>(stmt)) {
            vcd_dumper.set_levels(dv->levels);
            vcd_dumper.init_header(netlist, sim_time);
            bp.pc++;
            continue;
        }

        evaluator.execute_statement(stmt, bp);
        if (!simulation_active) return;
        bp.pc++;
    }

    if (bp.proc->type == ProcessType::INITIAL) {
        bp.completed = true;
    } else {
        // ALWAYS processes loop continuously: reset PC to 0 and re-execute from start
        bp.pc = 0;
        step_process(pid);
    }
}

void Engine::execute_time_slot_delta_loop() {
    // Phase 1: Propagate combinational logic to settle inputs for this timestamp
    propagate_comb();

    // Phase 2: Sample clock edges and evaluate sequential (always_ff) blocks ONCE
    // Their non-blocking assignments (<=) are staged into nba_queue without committing
    evaluate_sequential_blocks();

    // Phase 3: Non-blocking Assignment (NBA) Update Region
    // Commit staged updates simultaneously across all registers
    if (commit_nba_updates()) {
        // Phase 4: Propagate combinational effects of new register states
        propagate_comb();
    }
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
        *debug_out << "[PHASE 5: Simulation Runtime (IEEE 1800 Stratified Engine)]\n";
        *debug_out << "--------------------------------------------------------\n";
    }

    sim_time = 0;
    simulation_active = true;

    // Time 0 Initialization: Start all initial and always processes
    for (size_t i = 0; i < processes.size(); ++i) {
        if (processes[i].proc->type == ProcessType::INITIAL || 
            processes[i].proc->type == ProcessType::ALWAYS) {
            step_process(i);
        }
    }

    // Stabilize initial state at Time 0
    execute_time_slot_delta_loop();

    // IEEE Postponed Region at Time 0: Dump waveform and sample $monitor
    vcd_dumper.dump_sample(netlist, sim_time);
    evaluator.check_monitor();
    for (auto& n : netlist) n.modified = false;

    // Time Wheel Loop
    while (simulation_active && !time_wheel.empty()) {
        uint64_t current_slot_time = time_wheel.top().time;
        sim_time = current_slot_time;

        // Active Region: Wake up all processes scheduled for this timestamp
        while (!time_wheel.empty() && time_wheel.top().time == current_slot_time && simulation_active) {
            SimEvent ev = time_wheel.top();
            time_wheel.pop();
            events_dispatched++;
            step_process(ev.process_id);
        }

        if (!simulation_active) break;

        // Run stratified delta cycles (Active -> NBA -> Comb propagation)
        execute_time_slot_delta_loop();

        // IEEE Postponed Region: Read-only sample after all deltas have stabilized
        vcd_dumper.dump_sample(netlist, sim_time);
        evaluator.check_monitor();

        // Clear edge flags for the next time tick
        for (auto& n : netlist) n.modified = false;
    }

    if (vcd_dumper.is_enabled()) {
        std::string dumped_file = vcd_dumper.get_filename();
        vcd_dumper.close();
        if (allow_finish_msg) {
            std::cout << "[CAS VCD] Trace successfully dumped to: " << dumped_file << "\n";
        }
    }

    if (allow_finish_msg) {
        std::cout << "--------------------------------------------------------\n";
        std::cout << "[CAS SUMMARY] Simulation completed cleanly at T = " << sim_time << " ns.\n" << std::flush;
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
    s.events_dispatched = events_dispatched;
    s.delta_cycles = delta_cycles_count;
    s.nba_commits = nba_commits_count;
    return s;
}