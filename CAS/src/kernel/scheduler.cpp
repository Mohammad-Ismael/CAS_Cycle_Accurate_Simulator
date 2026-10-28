#include "../../include/kernel/scheduler.hpp"
#include "../../include/engine.hpp"

StratifiedScheduler::StratifiedScheduler(Engine& eng) : engine(eng) {}

void StratifiedScheduler::schedule_event(uint64_t wake_time, uint32_t pid) {
    time_wheel.schedule(wake_time, pid);
}

void StratifiedScheduler::queue_nba(size_t net_id, const LogicVal& val) {
    nba_queue.push_back({net_id, val});
}

void StratifiedScheduler::queue_nba_mem(const std::string& scoped_mem, uint32_t addr, const LogicVal& val) {
    nba_mem_queue.push_back({scoped_mem, addr, val});
}

void StratifiedScheduler::terminate() {
    simulation_active = false;
    time_wheel.clear();
}

void StratifiedScheduler::propagate_comb() {
    delta_cycles_count++;
    bool changed = true;
    size_t iters = 0;

    while (changed && iters < 200) {
        changed = false;

        for (auto& bind : engine.slice_bindings) {
            LogicVal src_val = engine.evaluator.read_slice(bind.src);
            LogicVal dst_val = engine.evaluator.read_slice(bind.dst);
            if (src_val != dst_val) {
                engine.evaluator.write_slice(bind.dst, src_val);
                changed = true;
            }
        }

        for (auto& g : engine.gates) {
            LogicVal res = engine.evaluator.eval_expr(g.in_exprs[0], g.scope);
            for (size_t i = 1; i < g.in_exprs.size(); ++i) {
                LogicVal next_in = engine.evaluator.eval_expr(g.in_exprs[i], g.scope);
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
            LogicVal cur_out = engine.evaluator.read_slice(g.out_target);
            if (cur_out != res) {
                engine.evaluator.write_slice(g.out_target, res);
                changed = true;
            }
        }

        for (auto& ca : engine.continuous_assigns) {
            LogicVal val = engine.evaluator.eval_expr(ca.rhs, ca.scope);
            LogicVal cur = engine.evaluator.read_slice(ca.target);
            if (cur != val) {
                engine.evaluator.write_slice(ca.target, val);
                changed = true;
            }
        }

        for (auto& bp : engine.processes) {
            if (bp.proc->type != ProcessType::COMBINATIONAL) continue;
            for (auto& stmt : bp.proc->body) {
                engine.evaluator.execute_statement(stmt.get(), bp);
            }
        }

        for (const auto& n : engine.netlist) {
            if (n.modified) {
                changed = true;
                break;
            }
        }
        iters++;
    }
}

void StratifiedScheduler::evaluate_sequential_blocks() {
    for (auto& bp : engine.processes) {
        if (bp.proc->type != ProcessType::SEQUENTIAL) continue;

        bool triggered = false;
        for (auto& sens : bp.proc->sensitivity_list) {
            std::string scoped_sig = bp.scope + "." + sens.signal_name;
            auto it = engine.net_map.find(scoped_sig);
            if (it == engine.net_map.end()) continue;
            const auto& net = engine.netlist[it->second];

            if (sens.edge == EdgeType::POSEDGE && net.is_posedge()) { triggered = true; break; }
            else if (sens.edge == EdgeType::NEGEDGE && net.is_negedge()) { triggered = true; break; }
        }

        if (triggered) {
            for (auto& stmt : bp.proc->body) {
                engine.evaluator.execute_statement(stmt.get(), bp);
            }
        }
    }
}

bool StratifiedScheduler::commit_nba_updates() {
    if (nba_queue.empty() && nba_mem_queue.empty()) return false;

    nba_commits_count++;
    for (const auto& update : nba_queue) {
        engine.netlist[update.net_id].set(update.val);
    }
    nba_queue.clear();

    for (const auto& mem_up : nba_mem_queue) {
        if (engine.memory_map.find(mem_up.mem_scoped_name) != engine.memory_map.end()) {
            auto& mem = engine.memory_map.at(mem_up.mem_scoped_name);
            if (mem_up.addr < mem.depth) mem.words[mem_up.addr] = mem_up.val;
        }
    }
    nba_mem_queue.clear();
    return true;
}

void StratifiedScheduler::step_process(size_t pid) {
    if (!simulation_active) return;
    auto& bp = engine.processes[pid];
    if (bp.completed) return;

    while (bp.pc < bp.proc->body.size()) {
        auto stmt = bp.proc->body[bp.pc].get();

        if (auto delay = dynamic_cast<DelayStatementNode*>(stmt)) {
            bp.pc++;
            uint64_t wake_time = current_sim_time + delay->delay;
            time_wheel.schedule(wake_time, (uint32_t)pid);
            return;
        }

        if (auto df = dynamic_cast<DumpFileStatementNode*>(stmt)) {
            engine.vcd_dumper.enable(df->filename);
            bp.pc++;
            continue;
        }

        if (auto dv = dynamic_cast<DumpVarsStatementNode*>(stmt)) {
            engine.vcd_dumper.set_levels(dv->levels);
            engine.vcd_dumper.init_header(engine.netlist, current_sim_time);
            bp.pc++;
            continue;
        }

        engine.evaluator.execute_statement(stmt, bp);
        if (!simulation_active) return;
        bp.pc++;
    }

    if (bp.proc->type == ProcessType::INITIAL) {
        bp.completed = true;
    } else {
        bp.pc = 0;
        step_process(pid);
    }
}

void StratifiedScheduler::execute_time_slot_delta_loop() {
    propagate_comb();
    evaluate_sequential_blocks();
    if (commit_nba_updates()) {
        propagate_comb();
    }
}

void StratifiedScheduler::run_simulation() {
    current_sim_time = 0;
    simulation_active = true;

    // Time 0: Initial activation of all initial and always processes
    for (size_t i = 0; i < engine.processes.size(); ++i) {
        if (engine.processes[i].proc->type == ProcessType::INITIAL || 
            engine.processes[i].proc->type == ProcessType::ALWAYS) {
            step_process(i);
        }
    }

    // Stabilize Time 0 state
    execute_time_slot_delta_loop();

    // Postponed Region at Time 0
    engine.vcd_dumper.dump_sample(engine.netlist, current_sim_time);
    engine.evaluator.check_monitor();

    // Settle history and clear modified flags for Time 0
    for (auto& n : engine.netlist) {
        n.commit_time_slot();
    }

    // Discrete-Event Wheel Loop
    while (simulation_active && !time_wheel.empty()) {
        uint64_t slot_time = time_wheel.next_time();
        current_sim_time = slot_time;

        // Active Region: Drain all events scheduled for this exact timestamp
        while (!time_wheel.empty() && time_wheel.next_time() == slot_time && simulation_active) {
            SimEvent ev = time_wheel.pop();
            step_process(ev.process_id);
        }

        if (!simulation_active) break;

        // Stratified Delta Cycle Execution (Active -> Sequential -> NBA -> Comb)
        execute_time_slot_delta_loop();

        // IEEE Postponed Region: Sample after delta stabilization
        engine.vcd_dumper.dump_sample(engine.netlist, current_sim_time);
        engine.evaluator.check_monitor();

        // Settle history so static signals never re-trigger edges on next timestamps
        for (auto& n : engine.netlist) {
            n.commit_time_slot();
        }
    }
}