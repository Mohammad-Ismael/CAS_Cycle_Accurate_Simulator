#include "../../include/kernel/scheduler.hpp"
#include "../../include/engine.hpp"
#include "../../include/kernel/logic_ops.hpp"
#include <iostream>

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

void StratifiedScheduler::queue_delayed_nba(uint64_t wake_time, size_t net_id, const LogicVal& val) {
    delayed_nba_queue[wake_time].push_back({net_id, val});
    time_wheel.schedule(wake_time, UINT32_MAX);
}

void StratifiedScheduler::queue_delayed_nba_mem(uint64_t wake_time, const std::string& scoped_mem, uint32_t addr, const LogicVal& val) {
    delayed_nba_mem_queue[wake_time].push_back({scoped_mem, addr, val});
    time_wheel.schedule(wake_time, UINT32_MAX);
}

void StratifiedScheduler::queue_delayed_blocking(uint64_t wake_time, size_t pid, const std::string& target_name, const std::string& scope, const LogicVal& val) {
    delayed_blocking_queue[wake_time].push_back({pid, target_name, scope, val});
    time_wheel.schedule(wake_time, UINT32_MAX);
}

void StratifiedScheduler::transfer_due_delayed_updates(uint64_t target_time) {
    // 1. Commit due delayed NBAs directly so they are immediately visible in this time slot
    auto it = delayed_nba_queue.find(target_time);
    if (it != delayed_nba_queue.end()) {
        for (const auto& item : it->second) {
            if (!engine.netlist[item.net_id].has_procedural_assign) {
                engine.netlist[item.net_id].set(item.val);
            }
            nba_commits_count++;
        }
        delayed_nba_queue.erase(it);
    }

    auto mit = delayed_nba_mem_queue.find(target_time);
    if (mit != delayed_nba_mem_queue.end()) {
        for (const auto& item : mit->second) {
            if (engine.memory_map.find(item.mem_scoped_name) != engine.memory_map.end()) {
                auto& mem = engine.memory_map.at(item.mem_scoped_name);
                if (item.addr < mem.depth) mem.words[item.addr] = item.val;
            }
        }
        delayed_nba_mem_queue.erase(mit);
    }

    // 2. Commit due delayed blocking assignments and wake their processes
    auto bit = delayed_blocking_queue.find(target_time);
    if (bit != delayed_blocking_queue.end()) {
        for (const auto& b_item : bit->second) {
            std::string scoped_target = b_item.scope + "." + b_item.target_name;
            if (engine.net_map.find(scoped_target) != engine.net_map.end()) {
                size_t net_id = engine.net_map.at(scoped_target);
                if (!engine.netlist[net_id].has_procedural_assign) {
                    LogicVal casted = LogicOps::cast_width(b_item.val, engine.netlist[net_id].width, engine.netlist[net_id].is_signed);
                    engine.netlist[net_id].set(casted);
                }
            }
            // Resume the waiting process
            step_process(b_item.pid);
        }
        delayed_blocking_queue.erase(bit);
    }
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
            if (engine.netlist[bind.dst.net_id].has_procedural_assign) continue;
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
                if (g.type == GateType::AND) res = LogicOps::bitwise_and(res, next_in);
                else if (g.type == GateType::OR) res = LogicOps::bitwise_or(res, next_in);
                else if (g.type == GateType::XOR) res = LogicOps::bitwise_xor(res, next_in);
                else if (g.type == GateType::NAND) res = LogicOps::bitwise_and(res, next_in);
                else if (g.type == GateType::NOR) res = LogicOps::bitwise_or(res, next_in);
                else if (g.type == GateType::XNOR) res = LogicOps::bitwise_xor(res, next_in);
            }
            if (g.type == GateType::NAND || g.type == GateType::NOR || g.type == GateType::XNOR || g.type == GateType::NOT) {
                res = LogicOps::bitwise_not(res);
            }
            LogicVal cur_out = engine.evaluator.read_slice(g.out_target);
            if (cur_out != res) {
                engine.evaluator.write_slice(g.out_target, res);
                changed = true;
            }
        }

        for (auto& ca : engine.continuous_assigns) {
            if (engine.netlist[ca.target.net_id].has_procedural_assign) continue;
            LogicVal val = engine.evaluator.eval_expr(ca.rhs, ca.scope);
            LogicVal cur = engine.evaluator.read_slice(ca.target);
            if (cur != val) {
                engine.evaluator.write_slice(ca.target, val);
                changed = true;
            }
        }

        // IEEE 1800-2023 §10.4.2: Procedural Continuous Assignments propagate whenever operands change
        for (auto& net : engine.netlist) {
            if (net.has_procedural_assign && net.procedural_assign_rhs) {
                LogicVal val = engine.evaluator.eval_expr(net.procedural_assign_rhs, net.procedural_assign_scope);
                LogicVal casted = LogicOps::cast_width(val, net.width, net.is_signed);
                if (net.value != casted) {
                    net.set(casted);
                    changed = true;
                }
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
        if (bp.fired_in_time_slot) continue;

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
            bp.fired_in_time_slot = true;
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
        if (!engine.netlist[update.net_id].has_procedural_assign) {
            engine.netlist[update.net_id].set(update.val);
        }
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

bool StratifiedScheduler::resume_waiting_processes() {
    bool resumed_any = false;

    for (size_t pid = 0; pid < engine.processes.size(); ++pid) {
        auto& bp = engine.processes[pid];
        if (!bp.wait_target.active) continue;

        const auto& net = engine.netlist[bp.wait_target.net_id];
        bool event_matched = false;

        if (bp.wait_target.edge == EdgeType::POSEDGE && net.is_posedge()) {
            event_matched = true;
        } else if (bp.wait_target.edge == EdgeType::NEGEDGE && net.is_negedge()) {
            event_matched = true;
        } else if (bp.wait_target.edge == EdgeType::LEVEL && net.modified) {
            event_matched = true;
        }

        if (event_matched) {
            bp.wait_target.active = false;
            step_process(pid);
            resumed_any = true;
        }
    }

    return resumed_any;
}

void StratifiedScheduler::step_process(size_t pid) {
    if (!simulation_active) return;
    auto& bp = engine.processes[pid];
    if (bp.completed || bp.wait_target.active) return;

    while (bp.pc < bp.proc->body.size()) {
        auto stmt = bp.proc->body[bp.pc].get();

        // 0. Unwrap any embedded BlockStatementNode in place
        if (auto blk = dynamic_cast<BlockStatementNode*>(stmt)) {
            std::vector<std::unique_ptr<StatementNode>> new_body;
            for (size_t i = 0; i < bp.pc; ++i) {
                new_body.push_back(std::move(bp.proc->body[i]));
            }
            for (auto& bs : blk->statements) {
                new_body.push_back(std::move(bs));
            }
            for (size_t i = bp.pc + 1; i < bp.proc->body.size(); ++i) {
                new_body.push_back(std::move(bp.proc->body[i]));
            }
            bp.proc->body = std::move(new_body);
            continue;
        }

        // 1. Procedural In-Body Event Control: @(...)
        if (auto ev_stmt = dynamic_cast<EventControlStatementNode*>(stmt)) {
            std::string scoped_sig = bp.scope + "." + ev_stmt->signal_name;
            size_t net_id = 0;
            if (engine.net_map.find(scoped_sig) != engine.net_map.end()) {
                net_id = engine.net_map.at(scoped_sig);
            } else if (engine.net_map.find(ev_stmt->signal_name) != engine.net_map.end()) {
                net_id = engine.net_map.at(ev_stmt->signal_name);
            } else {
                bp.pc++;
                continue;
            }

            bp.pc++;
            bp.wait_target.active = true;
            bp.wait_target.edge = ev_stmt->edge;
            bp.wait_target.signal_name = ev_stmt->signal_name;
            bp.wait_target.net_id = net_id;
            return;
        }

        // 2. Procedural Delay: #delay
        if (auto delay = dynamic_cast<DelayStatementNode*>(stmt)) {
            bp.pc++;
            uint64_t wake_time = current_sim_time + delay->delay;
            time_wheel.schedule(wake_time, (uint32_t)pid);
            return;
        }

        // 3. Forever Loop: Inline body and set loop anchor
        if (auto forever_stmt = dynamic_cast<ForeverStatementNode*>(stmt)) {
            std::vector<std::unique_ptr<StatementNode>> new_body;
            for (size_t i = 0; i < bp.pc; ++i) {
                new_body.push_back(std::move(bp.proc->body[i]));
            }
            bp.loop_start_pc = bp.pc;
            bp.is_looping = true;

            for (auto& s : forever_stmt->body) {
                if (auto blk_inner = dynamic_cast<BlockStatementNode*>(s.get())) {
                    for (auto& bs : blk_inner->statements) {
                        new_body.push_back(std::move(bs));
                    }
                } else {
                    new_body.push_back(std::move(s));
                }
            }
            bp.proc->body = std::move(new_body);
            continue;
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

        // 4. IEEE 1800-2023 §10.6.1: Blocking Assignment with Intra-Assignment Delay
        if (auto assign = dynamic_cast<BlockingAssignNode*>(stmt)) {
            if (assign->intra_delay > 0) {
                // Sample RHS immediately at current sim time
                LogicVal rhs_val = engine.evaluator.eval_expr(assign->rhs.get(), bp.scope);
                std::string target_name = assign->lhs_targets[0];
                bp.pc++; // Advance PC so upon resumption it executes the next statement
                uint64_t wake_time = current_sim_time + assign->intra_delay;
                queue_delayed_blocking(wake_time, pid, target_name, bp.scope, rhs_val);
                return; // Process suspends cleanly
            }
        }

        // 5. Standard Statement Execution
        engine.evaluator.execute_statement(stmt, bp);
        if (!simulation_active) return;
        bp.pc++;
    }

    if (bp.is_looping) {
        bp.pc = bp.loop_start_pc;
        if (bp.pc < bp.proc->body.size()) {
            step_process(pid);
        }
    } else if (bp.proc->type == ProcessType::ALWAYS) {
        bp.pc = 0;
        if (!bp.proc->body.empty()) {
            step_process(pid);
        }
    } else {
        bp.completed = true;
    }
}

void StratifiedScheduler::execute_time_slot_delta_loop() {
    bool activity = true;
    size_t loop_guard = 0;
    const size_t MAX_DELTA_ITERS = 100;

    while (activity && loop_guard < MAX_DELTA_ITERS) {
        activity = false;

        propagate_comb();
        evaluate_sequential_blocks();

        if (commit_nba_updates()) {
            activity = true;
            propagate_comb();
        }

        if (resume_waiting_processes()) {
            activity = true;
            propagate_comb();
        }

        loop_guard++;
    }

    if (loop_guard >= MAX_DELTA_ITERS) {
        std::cerr << "\n[CAS RUNTIME ERROR] Delta cycle limit exceeded (100 delta cycles at T="
                  << current_sim_time << " ns).\n"
                  << "Possible causes: combinatorial feedback loop or oscillating sequential event.\n";
        terminate();
    }
}

void StratifiedScheduler::run_simulation() {
    current_sim_time = 0;
    simulation_active = true;

    for (auto& bp : engine.processes) {
        bp.fired_in_time_slot = false;
    }

    for (size_t i = 0; i < engine.processes.size(); ++i) {
        if (engine.processes[i].proc->type == ProcessType::INITIAL || 
            engine.processes[i].proc->type == ProcessType::ALWAYS) {
            step_process(i);
        }
    }

    transfer_due_delayed_updates(0);
    execute_time_slot_delta_loop();

    engine.vcd_dumper.dump_sample(engine.netlist, current_sim_time);
    engine.evaluator.check_monitor();

    for (auto& n : engine.netlist) {
        n.commit_time_slot();
    }

    while (simulation_active && !time_wheel.empty()) {
        uint64_t slot_time = time_wheel.next_time();
        current_sim_time = slot_time;

        for (auto& bp : engine.processes) {
            bp.fired_in_time_slot = false;
        }

        // 1. Active Region: Drain events for current timestamp
        while (!time_wheel.empty() && time_wheel.next_time() == slot_time && simulation_active) {
            SimEvent ev = time_wheel.pop();
            if (ev.process_id != UINT32_MAX) {
                step_process(ev.process_id);
            }
        }

        if (!simulation_active) break;

        // 2. NBA Region: Transfer matured delayed NBAs into nba_queue so they commit in the NBA region
        auto it = delayed_nba_queue.find(slot_time);
        if (it != delayed_nba_queue.end()) {
            for (const auto& item : it->second) {
                nba_queue.push_back(item);
            }
            delayed_nba_queue.erase(it);
        }

        auto mit = delayed_nba_mem_queue.find(slot_time);
        if (mit != delayed_nba_mem_queue.end()) {
            for (const auto& item : mit->second) {
                nba_mem_queue.push_back(item);
            }
            delayed_nba_mem_queue.erase(mit);
        }

        // Handle due delayed blocking assignments (which wake processes in Active)
        auto bit = delayed_blocking_queue.find(slot_time);
        if (bit != delayed_blocking_queue.end()) {
            for (const auto& b_item : bit->second) {
                std::string scoped_target = b_item.scope + "." + b_item.target_name;
                if (engine.net_map.find(scoped_target) != engine.net_map.end()) {
                    size_t net_id = engine.net_map.at(scoped_target);
                    LogicVal casted = LogicOps::cast_width(b_item.val, engine.netlist[net_id].width, engine.netlist[net_id].is_signed);
                    engine.netlist[net_id].set(casted);
                }
                step_process(b_item.pid);
            }
            delayed_blocking_queue.erase(bit);
        }

        execute_time_slot_delta_loop();

        engine.vcd_dumper.dump_sample(engine.netlist, current_sim_time);
        engine.evaluator.check_monitor();

        for (auto& n : engine.netlist) {
            n.commit_time_slot();
        }
    }
}