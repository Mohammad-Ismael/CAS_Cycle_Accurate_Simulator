#include "../../include/kernel/scheduler.hpp"
#include "../../include/engine.hpp"
#include "../../include/kernel/logic_ops.hpp"
#include <iostream>
#include <algorithm>

StratifiedScheduler::StratifiedScheduler(Engine& eng) : engine(eng) {}

void StratifiedScheduler::schedule_event(uint64_t wake_time, uint32_t pid) {
    time_wheel.schedule(wake_time, pid);
}

void StratifiedScheduler::queue_nba(size_t net_id, const LogicVal& val, uint32_t offset, uint32_t slice_width, bool is_slice) {
    nba_queue.push_back({net_id, val, offset, slice_width, is_slice});
}

void StratifiedScheduler::queue_nba_mem(const std::string& scoped_mem, int64_t addr, const LogicVal& val) {
    nba_mem_queue.push_back({scoped_mem, addr, val});
}

void StratifiedScheduler::queue_delayed_nba(uint64_t wake_time, size_t net_id, const LogicVal& val, uint32_t offset, uint32_t slice_width, bool is_slice) {
    delayed_nba_queue[wake_time].push_back({net_id, val, offset, slice_width, is_slice});
    time_wheel.schedule(wake_time, UINT32_MAX);
}

void StratifiedScheduler::queue_delayed_nba_mem(uint64_t wake_time, const std::string& scoped_mem, int64_t addr, const LogicVal& val) {
    delayed_nba_mem_queue[wake_time].push_back({scoped_mem, addr, val});
    time_wheel.schedule(wake_time, UINT32_MAX);
}

void StratifiedScheduler::queue_delayed_blocking(uint64_t wake_time, size_t pid, const std::string& target_name, const std::string& scope, const LogicVal& val) {
    delayed_blocking_queue[wake_time].push_back({pid, target_name, scope, val});
    time_wheel.schedule(wake_time, UINT32_MAX);
}

void StratifiedScheduler::queue_intra_event_nba(size_t trigger_net_id, EdgeType edge, size_t net_id, const LogicVal& val, uint32_t offset, uint32_t slice_width, bool is_slice) {
    intra_event_nba_queue.push_back({net_id, val, edge, trigger_net_id, current_sim_time, offset, slice_width, is_slice});
}

void StratifiedScheduler::queue_intra_event_blocking(size_t trigger_net_id, EdgeType edge, size_t pid, const std::string& target_name, const std::string& scope, const LogicVal& val) {
    intra_event_blocking_queue.push_back({pid, target_name, scope, val, edge, trigger_net_id, current_sim_time});
}

bool StratifiedScheduler::trigger_intra_event_nbas() {
    if (intra_event_nba_queue.empty()) return false;
    std::vector<IntraEventNbaItem> current_items;
    current_items.swap(intra_event_nba_queue);
    bool triggered_any = false;
    for (const auto& item : current_items) {
        if (item.reg_time >= current_sim_time) {
            intra_event_nba_queue.push_back(item);
            continue;
        }
        const auto& trig_net = engine.netlist[item.trigger_net_id];
        bool event_matched = false;
        if (item.edge == EdgeType::POSEDGE && trig_net.is_posedge()) event_matched = true;
        else if (item.edge == EdgeType::NEGEDGE && trig_net.is_negedge()) event_matched = true;
        else if (item.edge == EdgeType::LEVEL && trig_net.modified) event_matched = true;

        if (event_matched) {
            queue_nba(item.net_id, item.val, item.offset, item.slice_width, item.is_slice);
            triggered_any = true;
        } else {
            intra_event_nba_queue.push_back(item);
        }
    }
    return triggered_any;
}

bool StratifiedScheduler::trigger_intra_event_blocking() {
    if (intra_event_blocking_queue.empty()) return false;
    std::vector<IntraEventBlockingItem> current_items;
    current_items.swap(intra_event_blocking_queue);
    bool triggered_any = false;
    for (const auto& item : current_items) {
        if (item.reg_time >= current_sim_time) {
            intra_event_blocking_queue.push_back(item);
            continue;
        }
        const auto& trig_net = engine.netlist[item.trigger_net_id];
        bool event_matched = false;
        if (item.edge == EdgeType::POSEDGE && trig_net.is_posedge()) event_matched = true;
        else if (item.edge == EdgeType::NEGEDGE && trig_net.is_negedge()) event_matched = true;
        else if (item.edge == EdgeType::LEVEL && trig_net.modified) event_matched = true;

        if (event_matched) {
            size_t net_id = engine.get_net_id(item.scope, item.target_name);

            if (net_id != (size_t)-1) {
                if (!engine.netlist[net_id].has_procedural_assign) {
                    LogicVal casted = LogicOps::cast_width(item.val, engine.netlist[net_id].width, engine.netlist[net_id].is_signed);
                    engine.netlist[net_id].set(casted);
                }
            }
            if (item.pid < engine.processes.size()) {
                engine.processes[item.pid].is_delayed = false;
            }
            step_process(item.pid);
            triggered_any = true;
        } else {
            intra_event_blocking_queue.push_back(item);
        }
    }
    return triggered_any;
}

void StratifiedScheduler::capture_preponed_samples() {
    preponed_net_values.resize(engine.netlist.size());
    for (size_t i = 0; i < engine.netlist.size(); ++i) {
        preponed_net_values[i] = engine.netlist[i].value;
    }
}

bool StratifiedScheduler::evaluate_sva_assertions() {
    if (engine.sva_assertions.empty()) return false;
    bool activity = false;

    for (size_t asrt_idx = 0; asrt_idx < engine.sva_assertions.size(); ++asrt_idx) {
        auto& asrt = engine.sva_assertions[asrt_idx];
        if (asrt.fired_in_time_slot) continue;
        if (asrt.clock_net_id >= engine.netlist.size()) continue;

        const auto& clk_net = engine.netlist[asrt.clock_net_id];
        bool clk_triggered = false;
        if (asrt.clock_edge == EdgeType::POSEDGE && clk_net.is_posedge()) {
            clk_triggered = true;
        } else if (asrt.clock_edge == EdgeType::NEGEDGE && clk_net.is_negedge()) {
            clk_triggered = true;
        } else if (asrt.clock_edge == EdgeType::LEVEL && clk_net.modified) {
            clk_triggered = true;
        }

        if (!clk_triggered) continue;
        asrt.fired_in_time_slot = true;

        auto& ch = sva_clock_history[asrt.clock_net_id];
        if (ch.last_recorded_time != current_sim_time) {
            ch.last_recorded_time = current_sim_time;
            ch.snapshots.push_back(preponed_net_values);
            if (ch.snapshots.size() > 256) {
                ch.snapshots.pop_front();
            }
        }

        // OBSERVED REGION: evaluate property against preponed samples
        std::vector<PendingSubroutineCall> pending_subroutines;
        engine.evaluator.get_expr_eval().set_pending_subroutines(&pending_subroutines);
        engine.evaluator.get_expr_eval().set_preponed_override(&preponed_net_values);
        engine.evaluator.get_expr_eval().set_current_clock_history(&ch.snapshots);

        // Check disable iff
        bool disabled = false;
        if (asrt.disable_expr) {
            LogicVal dis_val = engine.evaluator.get_expr_eval().eval(asrt.disable_expr, asrt.scope);
            if (!dis_val.is_unknown() && dis_val.is_true()) {
                disabled = true;
            }
        }

        if (disabled) {
            asrt.threads.clear();
            engine.evaluator.get_expr_eval().set_pending_subroutines(nullptr);
            engine.evaluator.get_expr_eval().set_current_clock_history(nullptr);
            engine.evaluator.get_expr_eval().set_preponed_override(nullptr);
            continue;
        }

        const auto& steps = asrt.consequent_steps;

        auto advance_thread = [&](SvaThread& th) {
            engine.evaluator.get_expr_eval().set_sva_local_vars(&th.local_vars);
            if (th.remaining_delay > 0) {
                th.remaining_delay--;
            }
            while (th.remaining_delay == 0 && th.state == SvaThreadState::PENDING_CONSEQUENT) {
                if (th.step_index >= steps.size()) {
                    th.state = SvaThreadState::MATCHED;
                    break;
                }
                const auto& step = steps[th.step_index];
                LogicVal val = engine.evaluator.get_expr_eval().eval(step.expr, asrt.scope);
                bool expr_true = (!val.is_unknown() && val.is_true());

                if (step.rep_type == RepetitionType::CONSECUTIVE) {
                    if (expr_true) {
                        th.current_rep_count++;
                        if (th.current_rep_count >= step.max_count) {
                            th.step_index++;
                            if (th.step_index >= steps.size()) {
                                th.state = SvaThreadState::MATCHED;
                                break;
                            } else {
                                th.remaining_delay = steps[th.step_index].cycle_delay;
                                th.current_rep_count = 0;
                                th.target_rep_count = steps[th.step_index].max_count;
                                th.rep_type = steps[th.step_index].rep_type;
                                if (th.remaining_delay > 0) break;
                            }
                        } else {
                            break;
                        }
                    } else {
                        if (th.current_rep_count >= step.min_count) {
                            th.step_index++;
                            if (th.step_index >= steps.size()) {
                                th.state = SvaThreadState::MATCHED;
                                break;
                            } else {
                                th.remaining_delay = steps[th.step_index].cycle_delay;
                                th.current_rep_count = 0;
                                th.target_rep_count = steps[th.step_index].max_count;
                                th.rep_type = steps[th.step_index].rep_type;
                                if (th.remaining_delay > 0) break;
                            }
                        } else {
                            th.state = SvaThreadState::FAILED;
                            break;
                        }
                    }
                } else if (step.rep_type == RepetitionType::GOTO) {
                    if (expr_true) {
                        th.current_rep_count++;
                        if (th.current_rep_count >= step.max_count) {
                            th.step_index++;
                            if (th.step_index >= steps.size()) {
                                th.state = SvaThreadState::MATCHED;
                                break;
                            } else {
                                th.remaining_delay = steps[th.step_index].cycle_delay;
                                th.current_rep_count = 0;
                                th.target_rep_count = steps[th.step_index].max_count;
                                th.rep_type = steps[th.step_index].rep_type;
                                if (th.remaining_delay > 0) break;
                            }
                        } else {
                            break;
                        }
                    } else {
                        break;
                    }
                } else if (step.rep_type == RepetitionType::NON_CONSECUTIVE) {
                    if (expr_true) {
                        th.current_rep_count++;
                        if (th.current_rep_count >= step.max_count) {
                            th.step_index++;
                            if (th.step_index >= steps.size()) {
                                th.state = SvaThreadState::MATCHED;
                                break;
                            } else {
                                th.remaining_delay = steps[th.step_index].cycle_delay;
                                th.current_rep_count = 0;
                                th.target_rep_count = steps[th.step_index].max_count;
                                th.rep_type = steps[th.step_index].rep_type;
                                if (th.remaining_delay > 0) break;
                            }
                        } else {
                            break;
                        }
                    } else {
                        break;
                    }
                }
            }
            engine.evaluator.get_expr_eval().set_sva_local_vars(nullptr);
        };

        // 1. Advance existing pending threads
        for (auto& th : asrt.threads) {
            if (th.state == SvaThreadState::PENDING_CONSEQUENT) {
                advance_thread(th);
            }
        }

        // 2. Evaluate antecedent on this clock edge for new assertion attempt
        if (asrt.antecedent) {
            SvaThread new_th;
            new_th.state = SvaThreadState::PENDING_CONSEQUENT;
            new_th.step_index = 0;
            new_th.current_rep_count = 0;
            for (const auto& lv : asrt.local_vars) {
                new_th.local_vars[lv.name] = LogicVal(lv.width > 0 ? lv.width : 32, 0);
            }
            if (!steps.empty()) {
                new_th.target_rep_count = steps[0].max_count;
                new_th.rep_type = steps[0].rep_type;
            }

            engine.evaluator.get_expr_eval().set_sva_local_vars(&new_th.local_vars);
            LogicVal a_val = engine.evaluator.get_expr_eval().eval(asrt.antecedent, asrt.scope);
            engine.evaluator.get_expr_eval().set_sva_local_vars(nullptr);

            if (!a_val.is_unknown() && a_val.is_true()) {
                if (asrt.imp_type == ImplicationType::NON_OVERLAPPING) {
                    new_th.remaining_delay = 1 + ((!steps.empty()) ? steps[0].cycle_delay : 0);
                    asrt.threads.push_back(new_th);
                } else if (asrt.imp_type == ImplicationType::OVERLAPPING) {
                    new_th.remaining_delay = (!steps.empty()) ? steps[0].cycle_delay : 0;
                    if (new_th.remaining_delay == 0) {
                        advance_thread(new_th);
                    }
                    asrt.threads.push_back(new_th);
                } else {
                    new_th.remaining_delay = (!steps.empty()) ? steps[0].cycle_delay : 0;
                    if (new_th.remaining_delay == 0) {
                        advance_thread(new_th);
                    }
                    asrt.threads.push_back(new_th);
                }
            }
        }

        engine.evaluator.get_expr_eval().set_pending_subroutines(nullptr);
        engine.evaluator.get_expr_eval().set_current_clock_history(nullptr);
        engine.evaluator.get_expr_eval().set_preponed_override(nullptr);

        // REACTIVE REGION: execute pending sequence match subroutines (§16.11)
        for (const auto& sub : pending_subroutines) {
            engine.execute_sva_subroutine(sub.name, sub.args, sub.scope);
            activity = true;
        }

        // REACTIVE REGION: execute action blocks for completed threads
        BoundProcess action_bp;
        action_bp.scope = asrt.scope;

        std::vector<SvaThread> remaining_threads;
        for (const auto& th : asrt.threads) {
            if (th.state == SvaThreadState::MATCHED) {
                for (auto* stmt : asrt.pass_statements) {
                    engine.evaluator.execute_statement(stmt, action_bp);
                }
                activity = true;
            } else if (th.state == SvaThreadState::FAILED) {
                for (auto* stmt : asrt.fail_statements) {
                    engine.evaluator.execute_statement(stmt, action_bp);
                }
                activity = true;
            } else {
                remaining_threads.push_back(th);
            }
        }
        asrt.threads = std::move(remaining_threads);
    }

    return activity;
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
                mem.set_word(item.addr, item.val);
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
            if (b_item.pid < engine.processes.size()) {
                engine.processes[b_item.pid].is_delayed = false;
            }
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

        // IEEE 1800-2023 §6.5 & §10.3 / §10.4.1: Multi-Driver Continuous Assignment Resolution
        for (auto& rca : engine.resolved_continuous_assigns) {
            if (engine.netlist[rca.target.net_id].has_procedural_assign) continue;

            LogicVal resolved;
            if (rca.drivers.empty()) {
                resolved = LogicVal::make_z(rca.target.width);
            } else if (rca.drivers.size() == 1) {
                resolved = engine.evaluator.eval_expr(rca.drivers[0].rhs, rca.drivers[0].scope);
                resolved = LogicOps::cast_width(resolved, rca.target.width, engine.netlist[rca.target.net_id].is_signed);
            } else {
                resolved = engine.evaluator.eval_expr(rca.drivers[0].rhs, rca.drivers[0].scope);
                resolved = LogicOps::cast_width(resolved, rca.target.width, engine.netlist[rca.target.net_id].is_signed);
                NetKind kind = engine.netlist[rca.target.net_id].kind;
                for (size_t i = 1; i < rca.drivers.size(); ++i) {
                    LogicVal d_val = engine.evaluator.eval_expr(rca.drivers[i].rhs, rca.drivers[i].scope);
                    d_val = LogicOps::cast_width(d_val, rca.target.width, engine.netlist[rca.target.net_id].is_signed);
                    resolved = LogicOps::resolve_net(kind, resolved, d_val);
                }
            }

            LogicVal cur = engine.evaluator.read_slice(rca.target);
            if (cur != resolved) {
                engine.evaluator.write_slice(rca.target, resolved);
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
            size_t net_id = engine.get_net_id(bp.scope, sens.signal_name);
            if (net_id == (size_t)-1) continue;
            const auto& net = engine.netlist[net_id];

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

void StratifiedScheduler::evaluate_clocking_blocks() {
    for (size_t cb_idx = 0; cb_idx < engine.clocking_blocks.size(); ++cb_idx) {
        auto& cb = engine.clocking_blocks[cb_idx];
        if (cb.fired_in_time_slot) continue;
        if (cb.clock_net_id >= engine.netlist.size()) continue;

        const auto& clk_net = engine.netlist[cb.clock_net_id];
        bool clk_triggered = false;
        if (cb.clock_edge == EdgeType::POSEDGE && clk_net.is_posedge()) {
            clk_triggered = true;
        } else if (cb.clock_edge == EdgeType::NEGEDGE && clk_net.is_negedge()) {
            clk_triggered = true;
        }

        if (clk_triggered) {
            cb.fired_in_time_slot = true;
            // Preponed sampling of input signals
            for (auto& kv : cb.signals) {
                if (kv.second.direction == "input" || kv.second.direction == "inout") {
                    if (kv.second.net_id < engine.netlist.size()) {
                        kv.second.sampled_value = engine.netlist[kv.second.net_id].value;
                    }
                }
            }

            // Commit pending drives synchronized to this clock edge
            for (const auto& drv : cb.pending_drives) {
                queue_nba(drv.net_id, drv.val);
            }
            cb.pending_drives.clear();

            // Decrement cycle delays and wake up processes
            std::vector<size_t> pids_to_wake;
            for (size_t pid = 0; pid < engine.processes.size(); ++pid) {
                auto& bp = engine.processes[pid];
                if (bp.is_waiting_cycle && bp.clocking_block_id == cb_idx) {
                    if (bp.cycle_delay_count > 0) {
                        bp.cycle_delay_count--;
                    }
                    if (bp.cycle_delay_count == 0) {
                        bp.is_waiting_cycle = false;
                        pids_to_wake.push_back(pid);
                    }
                }
            }
            for (size_t pid : pids_to_wake) {
                step_process(pid);
            }
        }
    }
}

void StratifiedScheduler::evaluate_covergroup_events() {
    for (uint64_t hid : engine.event_triggered_covergroups) {
        auto inst = engine.get_covergroup_instance(hid);
        if (!inst || inst->fired_in_time_slot) continue;
        if (inst->clock_net_id >= engine.netlist.size()) continue;

        const auto& clk_net = engine.netlist[inst->clock_net_id];
        bool clk_triggered = false;
        if (inst->clock_edge == EdgeType::POSEDGE && clk_net.is_posedge()) {
            clk_triggered = true;
        } else if (inst->clock_edge == EdgeType::NEGEDGE && clk_net.is_negedge()) {
            clk_triggered = true;
        } else if (inst->clock_edge == EdgeType::LEVEL && clk_net.modified) {
            clk_triggered = true;
        }

        if (clk_triggered) {
            inst->fired_in_time_slot = true;
            inst->sample([&](ExpressionNode* e, const std::string& sc) {
                return engine.evaluator.eval_expr(e, sc.empty() ? inst->creation_scope : sc);
            });
        }
    }
}

bool StratifiedScheduler::commit_nba_updates() {
    if (nba_queue.empty() && nba_mem_queue.empty() && nba_event_queue.empty()) return false;

    nba_commits_count++;
    for (const auto& update : nba_queue) {
        if (!engine.netlist[update.net_id].has_procedural_assign) {
            if (update.is_slice) {
                auto& net = engine.netlist[update.net_id];
                uint32_t w = update.slice_width;
                uint32_t off = update.offset;
                uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
                uint64_t full_mask = mask << off;
                uint64_t new_a = (net.value.aval & ~full_mask) | ((update.val.aval & mask) << off);
                uint64_t new_b = (net.value.bval & ~full_mask) | ((update.val.bval & mask) << off);
                net.set(LogicVal(net.width, new_a, new_b));
            } else {
                engine.netlist[update.net_id].set(update.val);
            }
        }
    }
    nba_queue.clear();

    for (const auto& mem_up : nba_mem_queue) {
        if (engine.memory_map.find(mem_up.mem_scoped_name) != engine.memory_map.end()) {
            auto& mem = engine.memory_map.at(mem_up.mem_scoped_name);
            mem.set_word(mem_up.addr, mem_up.val);
        }
    }
    nba_mem_queue.clear();

    if (!nba_event_queue.empty()) {
        std::vector<NBAEventItem> current_events = std::move(nba_event_queue);
        nba_event_queue.clear();
        for (const auto& ev_item : current_events) {
            trigger_named_event_id(ev_item.event_id);
        }
    }

    return true;
}

bool StratifiedScheduler::resume_waiting_processes(bool for_reactive) {
    bool resumed_any = false;

    for (size_t pid = 0; pid < engine.processes.size(); ++pid) {
        auto& bp = engine.processes[pid];
        if (!bp.wait_target.active) continue;
        if (bp.is_reactive != for_reactive) continue;

        const auto& net = engine.netlist[bp.wait_target.net_id];
        if (net.change_counter <= bp.wait_target.initial_change_counter) continue;

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

void StratifiedScheduler::check_program_completion() {
    bool has_program = false;
    bool all_completed = true;

    for (const auto& bp : engine.processes) {
        if (bp.is_program_process) {
            has_program = true;
            if (!bp.completed) {
                all_completed = false;
                break;
            }
        }
    }

    if (has_program && all_completed) {
        terminate();
    }
}

static void flatten_task_statements(StatementNode* stmt, std::vector<StatementNode*>& out) {
    if (!stmt) return;
    if (auto blk = dynamic_cast<BlockStatementNode*>(stmt)) {
        for (auto& s : blk->statements) {
            flatten_task_statements(s.get(), out);
        }
    } else {
        out.push_back(stmt);
    }
}

void StratifiedScheduler::invoke_task(BoundProcess& bp, TaskCallNode* tcall) {
    std::string scoped_tname = bp.scope.empty() ? tcall->task_name : (bp.scope + "." + tcall->task_name);
    if (engine.dpi_import_map.find(scoped_tname) != engine.dpi_import_map.end() ||
        engine.dpi_import_map.find(tcall->task_name) != engine.dpi_import_map.end()) {
        engine.evaluator.execute_statement(tcall, bp);
        bp.pc++;
        return;
    }

    TaskNode* target_task = nullptr;
    ClassObject* cl_obj = nullptr;
    auto dot = tcall->task_name.rfind('.');
    if (dot != std::string::npos) {
        std::string var_name = tcall->task_name.substr(0, dot);
        std::string method_name = tcall->task_name.substr(dot + 1);

        if (var_name == "this" || var_name == "super") {
            cl_obj = engine.evaluator.get_expr_eval().get_current_this();
        } else {
            if (engine.evaluator.get_expr_eval().get_context().has_active_frame()) {
                LocalVar* local = engine.evaluator.get_expr_eval().get_context().lookup_local(var_name);
                if (local && local->is_class_handle) {
                    cl_obj = engine.get_class_object(local->value.aval).get();
                }
            }
            if (!cl_obj && !bp.call_stack.empty()) {
                for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                    LocalVar* local = it->local_frame.get_var(var_name);
                    if (local && local->is_class_handle) {
                        cl_obj = engine.get_class_object(local->value.aval).get();
                        break;
                    }
                }
            }
            if (!cl_obj) {
                size_t nid = engine.get_net_id(bp.scope, var_name);
                if (nid != (size_t)-1 && engine.netlist[nid].is_class_handle) {
                    cl_obj = engine.get_class_object(engine.netlist[nid].value.aval).get();
                }
            }
        }

        if (cl_obj && cl_obj->class_def) {
            target_task = cl_obj->class_def->find_task(method_name);
        }
    }

    if (!target_task) {
        if (engine.task_map.find(scoped_tname) != engine.task_map.end()) {
            target_task = engine.task_map.at(scoped_tname);
        } else if (engine.task_map.find(tcall->task_name) != engine.task_map.end()) {
            target_task = engine.task_map.at(tcall->task_name);
        }
    }

    if (!target_task) {
        throw std::runtime_error("Unknown task called: " + tcall->task_name);
    }

    TaskActivationFrame frame(tcall->task_name, target_task, cl_obj);

    // 1. Formal port bindings & argument passing
    for (size_t i = 0; i < target_task->ports.size(); ++i) {
        const auto& p = target_task->ports[i];
        uint32_t w = 32;
        if (p.msb_expr && p.lsb_expr) {
            auto msb_n = dynamic_cast<NumberNode*>(p.msb_expr.get());
            auto lsb_n = dynamic_cast<NumberNode*>(p.lsb_expr.get());
            if (msb_n && lsb_n) {
                w = (msb_n->aval >= lsb_n->aval) ? (msb_n->aval - lsb_n->aval + 1) : (lsb_n->aval - msb_n->aval + 1);
            }
        }
        frame.local_frame.declare_var(p.name, w, p.is_signed);
        if (p.is_virtual_interface) {
            LocalVar* lv = frame.local_frame.get_var(p.name);
            if (lv) {
                lv->is_virtual_interface = true;
                lv->interface_name = p.interface_name;
                lv->modport_name = p.modport_name;
            }
        }

        if (p.direction == "input" || p.direction == "inout") {
            if (i < tcall->args.size()) {
                LogicVal arg_val = engine.evaluator.eval_expr(tcall->args[i].get(), bp.scope);
                LocalVar* lv = frame.local_frame.get_var(p.name);
                if (lv) {
                    lv->value = LogicOps::cast_width(arg_val, w, p.is_signed);
                    if (p.is_virtual_interface) {
                        lv->vif_handle = arg_val.aval;
                    }
                }
            }
        }

        if (p.direction == "output" || p.direction == "inout") {
            if (i < tcall->args.size()) {
                if (auto id = dynamic_cast<IdentifierNode*>(tcall->args[i].get())) {
                    frame.output_bindings.push_back({p.name, id->name});
                }
            }
        }
    }

    // 2. Flatten task body statements
    for (auto& s : target_task->body) {
        flatten_task_statements(s.get(), frame.flattened_body);
    }

    // 3. Pre-declare task locals
    for (auto* s : frame.flattened_body) {
        if (auto vdecl = dynamic_cast<VarDeclStatementNode*>(s)) {
            frame.local_frame.declare_var(vdecl->var_name, vdecl->width, vdecl->is_signed);
        }
    }

    frame.pc = 0;
    bp.call_stack.push_back(std::move(frame));
}

void StratifiedScheduler::write_task_output(BoundProcess& bp, const std::string& actual_name, const LogicVal& val) {
    if (bp.call_stack.size() > 1) {
        auto& caller_frame = bp.call_stack[bp.call_stack.size() - 2];
        LocalVar* lv = caller_frame.local_frame.get_var(actual_name);
        if (lv) {
            lv->value = LogicOps::cast_width(val, lv->width, lv->is_signed);
            return;
        }
        if (caller_frame.this_obj && caller_frame.this_obj->has_property(actual_name)) {
            caller_frame.this_obj->set_property(actual_name, val);
            return;
        }
    }

    if (engine.evaluator.get_expr_eval().get_current_this() && engine.evaluator.get_expr_eval().get_current_this()->has_property(actual_name)) {
        engine.evaluator.get_expr_eval().get_current_this()->set_property(actual_name, val);
        return;
    }

    std::string scoped_target = bp.scope.empty() ? actual_name : (bp.scope + "." + actual_name);
    size_t net_id = (size_t)-1;
    if (engine.net_map.find(scoped_target) != engine.net_map.end()) {
        net_id = engine.net_map.at(scoped_target);
    } else if (engine.net_map.find(actual_name) != engine.net_map.end()) {
        net_id = engine.net_map.at(actual_name);
    }

    if (net_id != (size_t)-1) {
        auto& net = engine.netlist[net_id];
        LogicVal casted = LogicOps::cast_width(val, net.width, net.is_signed);
        net.set(casted);
    }
}

void StratifiedScheduler::invoke_fork(size_t pid, ForkStatementNode* fork_stmt) {
    if (!fork_stmt) return;

    size_t total_children = fork_stmt->branches.size();
    if (total_children == 0) {
        return;
    }

    engine.processes[pid].fork_barrier.join_type = fork_stmt->join_type;
    engine.processes[pid].fork_barrier.total_children = total_children;
    engine.processes[pid].fork_barrier.completed_children = 0;
    engine.processes[pid].fork_barrier.child_pids.clear();
    engine.processes[pid].fork_barrier.in_fork_setup = true;

    if (fork_stmt->join_type == JoinType::JOIN_NONE) {
        engine.processes[pid].fork_barrier.active = false;
    } else {
        engine.processes[pid].fork_barrier.active = true;
    }

    // Spawn child processes
    for (auto& branch : fork_stmt->branches) {
        BoundProcess child_bp;
        child_bp.scope = engine.processes[pid].scope;
        child_bp.proc = engine.processes[pid].proc;
        child_bp.call_stack = engine.processes[pid].call_stack;
        child_bp.parent_pid = pid;
        child_bp.pc = 0;
        child_bp.completed = false;
        child_bp.is_delayed = false;
        child_bp.is_reactive = engine.processes[pid].is_reactive;
        child_bp.is_program_process = engine.processes[pid].is_program_process;

        flatten_task_statements(branch.get(), child_bp.dynamic_statements);

        size_t child_pid = engine.processes.size();
        child_bp.pid = child_pid;
        engine.processes.push_back(std::move(child_bp));
        engine.processes[pid].fork_barrier.child_pids.push_back(child_pid);
        engine.processes[pid].active_children.push_back(child_pid);
    }

    // Step child processes in Active region
    std::vector<size_t> child_pids = engine.processes[pid].fork_barrier.child_pids;
    for (size_t c_pid : child_pids) {
        step_process(c_pid);
    }

    engine.processes[pid].fork_barrier.in_fork_setup = false;

    // Check if barrier was satisfied during synchronous execution
    if (engine.processes[pid].fork_barrier.active) {
        if (engine.processes[pid].fork_barrier.join_type == JoinType::JOIN &&
            engine.processes[pid].fork_barrier.completed_children >= engine.processes[pid].fork_barrier.total_children) {
            engine.processes[pid].fork_barrier.active = false;
        } else if (engine.processes[pid].fork_barrier.join_type == JoinType::JOIN_ANY &&
                   engine.processes[pid].fork_barrier.completed_children >= 1) {
            engine.processes[pid].fork_barrier.active = false;
        }
    }
}

bool StratifiedScheduler::has_active_descendants(size_t pid) {
    if (pid >= engine.processes.size()) return false;
    for (size_t c_pid : engine.processes[pid].active_children) {
        if (c_pid < engine.processes.size()) {
            if (!engine.processes[c_pid].completed) {
                return true;
            }
            if (has_active_descendants(c_pid)) {
                return true;
            }
        }
    }
    return false;
}

void StratifiedScheduler::terminate_process_tree(size_t target_pid) {
    if (target_pid >= engine.processes.size()) return;

    std::vector<size_t> children = engine.processes[target_pid].active_children;
    for (size_t c_pid : children) {
        terminate_process_tree(c_pid);
    }
    engine.processes[target_pid].active_children.clear();

    auto& p = engine.processes[target_pid];
    p.completed = true;
    p.is_delayed = false;
    p.wait_target.active = false;
    p.fork_barrier.active = false;
    p.is_waiting_fork = false;
    p.waiting_event_id = (size_t)-1;
    p.is_waiting_cycle = false;
    p.is_waiting_sync = false;
}

void StratifiedScheduler::trigger_named_event_id(size_t ev_id) {
    if (ev_id >= engine.named_events.size()) return;
    auto& ev = engine.named_events[ev_id];
    ev.last_triggered_time = current_sim_time;
    std::vector<size_t> pids_to_wake = std::move(ev.waiting_pids);
    ev.waiting_pids.clear();
    for (size_t w_pid : pids_to_wake) {
        if (w_pid < engine.processes.size() && !engine.processes[w_pid].completed) {
            engine.processes[w_pid].waiting_event_id = (size_t)-1;
            step_process(w_pid);
        }
    }
}

void StratifiedScheduler::trigger_named_event(const std::string& ev_name, const std::string& scope, bool is_nonblocking) {
    std::string scoped_name = scope.empty() ? ev_name : (scope + "." + ev_name);
    size_t ev_id = (size_t)-1;
    if (engine.named_event_map.find(scoped_name) != engine.named_event_map.end()) {
        ev_id = engine.named_event_map.at(scoped_name);
    } else if (engine.named_event_map.find(ev_name) != engine.named_event_map.end()) {
        ev_id = engine.named_event_map.at(ev_name);
    }

    if (ev_id == (size_t)-1) return;

    if (is_nonblocking) {
        nba_event_queue.push_back({ev_id});
    } else {
        trigger_named_event_id(ev_id);
    }
}

void StratifiedScheduler::disable_fork(size_t caller_pid) {
    if (caller_pid >= engine.processes.size()) return;

    std::vector<size_t> children = engine.processes[caller_pid].active_children;
    for (size_t c_pid : children) {
        terminate_process_tree(c_pid);
    }
    engine.processes[caller_pid].active_children.clear();
}

void StratifiedScheduler::notify_parent_child_completed(size_t child_pid) {
    if (child_pid >= engine.processes.size()) return;
    size_t ppid = engine.processes[child_pid].parent_pid;
    if (ppid == (size_t)-1 || ppid >= engine.processes.size()) return;

    auto& parent = engine.processes[ppid];

    // Remove child from direct parent's active_children
    auto& act = parent.active_children;
    act.erase(std::remove(act.begin(), act.end(), child_pid), act.end());

    // 1. Check fork barrier (fork ... join / join_any)
    if (parent.fork_barrier.active) {
        parent.fork_barrier.completed_children++;

        bool unblock = false;
        if (parent.fork_barrier.join_type == JoinType::JOIN) {
            if (parent.fork_barrier.completed_children >= parent.fork_barrier.total_children) {
                unblock = true;
            }
        } else if (parent.fork_barrier.join_type == JoinType::JOIN_ANY) {
            if (parent.fork_barrier.completed_children >= 1) {
                unblock = true;
            }
        }

        if (unblock) {
            parent.fork_barrier.active = false;
            if (!parent.fork_barrier.in_fork_setup) {
                step_process(ppid);
            }
        }
    }

    // 2. Check wait fork on ancestor chain (§9.6.3)
    size_t anc_pid = ppid;
    while (anc_pid != (size_t)-1 && anc_pid < engine.processes.size()) {
        auto& anc = engine.processes[anc_pid];
        if (anc.is_waiting_fork) {
            if (!has_active_descendants(anc_pid)) {
                anc.is_waiting_fork = false;
                step_process(anc_pid);
            }
        }
        anc_pid = anc.parent_pid;
    }
}
static bool stmt_has_timing_control(const StatementNode* stmt) {
    if (!stmt) return false;
    if (dynamic_cast<const EventControlStatementNode*>(stmt)) return true;
    if (dynamic_cast<const DelayStatementNode*>(stmt)) return true;
    if (dynamic_cast<const CycleDelayStatementNode*>(stmt)) return true;
    if (dynamic_cast<const WaitEventTriggeredStatementNode*>(stmt)) return true;
    if (auto blk = dynamic_cast<const BlockStatementNode*>(stmt)) {
        for (const auto& s : blk->statements) {
            if (stmt_has_timing_control(s.get())) return true;
        }
    }
    if (auto if_s = dynamic_cast<const IfStatementNode*>(stmt)) {
        for (const auto& s : if_s->then_branch) {
            if (stmt_has_timing_control(s.get())) return true;
        }
        for (const auto& s : if_s->else_branch) {
            if (stmt_has_timing_control(s.get())) return true;
        }
    }
    if (auto for_s = dynamic_cast<const ForStatementNode*>(stmt)) {
        if (stmt_has_timing_control(for_s->init_stmt.get())) return true;
        if (stmt_has_timing_control(for_s->step_stmt.get())) return true;
        for (const auto& s : for_s->body) {
            if (stmt_has_timing_control(s.get())) return true;
        }
    }
    if (auto wh_s = dynamic_cast<const WhileStatementNode*>(stmt)) {
        for (const auto& s : wh_s->body) {
            if (stmt_has_timing_control(s.get())) return true;
        }
    }
    if (auto rep_s = dynamic_cast<const RepeatStatementNode*>(stmt)) {
        for (const auto& s : rep_s->body) {
            if (stmt_has_timing_control(s.get())) return true;
        }
    }
    return false;
}

void StratifiedScheduler::step_process(size_t pid) {
    if (!simulation_active) return;
    if (pid >= engine.processes.size()) return;
    auto& initial_bp = engine.processes[pid];
    if (initial_bp.completed || initial_bp.wait_target.active || initial_bp.fork_barrier.active || initial_bp.is_delayed || initial_bp.is_waiting_fork || initial_bp.waiting_event_id != (size_t)-1 || initial_bp.is_waiting_cycle || initial_bp.is_waiting_sync) return;

    engine.evaluator.set_current_process(&initial_bp);

    while (simulation_active) {
        if (pid >= engine.processes.size()) break;
        auto& bp = engine.processes[pid];
        if (bp.completed || bp.wait_target.active || bp.fork_barrier.active || bp.is_delayed || bp.is_waiting_fork || bp.waiting_event_id != (size_t)-1 || bp.is_waiting_cycle || bp.is_waiting_sync) break;
        engine.evaluator.set_current_process(&bp);

        // A. Execution within an active task activation frame
        if (!bp.call_stack.empty()) {
            auto& cur_frame = bp.call_stack.back();
            if (cur_frame.this_obj) {
                engine.evaluator.set_current_this(cur_frame.this_obj);
            }
            if (cur_frame.pc >= cur_frame.flattened_body.size()) {
                // Task body finished: write back output bindings and pop frame
                for (const auto& binding : cur_frame.output_bindings) {
                    LocalVar* lv = cur_frame.local_frame.get_var(binding.first);
                    if (lv) {
                        write_task_output(bp, binding.second, lv->value);
                    }
                }
                bp.call_stack.pop_back();
                ClassObject* next_this = nullptr;
                for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                    if (it->this_obj) {
                        next_this = it->this_obj;
                        break;
                    }
                }
                engine.evaluator.set_current_this(next_this);
                if (bp.call_stack.empty()) {
                    bp.pc++;
                } else {
                    bp.call_stack.back().pc++;
                }
                continue;
            }

            StatementNode* stmt = cur_frame.flattened_body[cur_frame.pc];

            // Cycle Delay inside task: ##N
            if (auto cdelay = dynamic_cast<CycleDelayStatementNode*>(stmt)) {
                cur_frame.pc++;
                int64_t cycles = 1;
                if (cdelay->cycle_expr) {
                    LogicVal cval = engine.evaluator.eval_expr(cdelay->cycle_expr.get(), bp.scope);
                    cycles = cval.is_unknown() ? 0 : (int64_t)cval.aval;
                }
                if (cycles > 0) {
                    size_t cb_id = engine.get_clocking_block_id(cdelay->clocking_name, bp.scope);
                    bp.is_waiting_cycle = true;
                    bp.cycle_delay_count = (size_t)cycles;
                    bp.clocking_block_id = cb_id;
                    break;
                }
                continue;
            }

            // Procedural Delay inside task: #delay
            if (auto delay = dynamic_cast<DelayStatementNode*>(stmt)) {
                cur_frame.pc++;
                uint64_t d = delay->delay;
                if (delay->delay_expr) {
                    LogicVal dval = engine.evaluator.eval_expr(delay->delay_expr.get(), bp.scope);
                    d = dval.is_unknown() ? 0 : dval.aval;
                }
                uint64_t wake_time = current_sim_time + d;
                bp.is_delayed = true;
                time_wheel.schedule(wake_time, (uint32_t)pid);
                break;
            }

            // Procedural Event Control inside task: @(...)
            if (auto ev_stmt = dynamic_cast<EventControlStatementNode*>(stmt)) {
                std::string scoped_sig = bp.scope.empty() ? ev_stmt->signal_name : (bp.scope + "." + ev_stmt->signal_name);
                size_t ev_id = (size_t)-1;
                if (engine.named_event_map.find(scoped_sig) != engine.named_event_map.end()) {
                    ev_id = engine.named_event_map.at(scoped_sig);
                } else if (engine.named_event_map.find(ev_stmt->signal_name) != engine.named_event_map.end()) {
                    ev_id = engine.named_event_map.at(ev_stmt->signal_name);
                }
                if (ev_id != (size_t)-1) {
                    cur_frame.pc++;
                    bp.waiting_event_id = ev_id;
                    engine.named_events[ev_id].waiting_pids.push_back(pid);
                    break;
                }

                size_t net_id = (size_t)-1;
                auto dot_p = ev_stmt->signal_name.find('.');
                if (dot_p != std::string::npos) {
                    std::string base = ev_stmt->signal_name.substr(0, dot_p);
                    std::string mem = ev_stmt->signal_name.substr(dot_p + 1);
                    VirtualInterfaceHandle vif = engine.lookup_virtual_interface(base, bp.scope, engine.evaluator.get_expr_eval().get_current_this(), &bp);
                    if (vif.is_valid_vif) {
                        net_id = engine.resolve_vif_net_id(vif, mem);
                    }
                }
                if (net_id == (size_t)-1) {
                    net_id = engine.get_net_id(bp.scope, ev_stmt->signal_name);
                }
                if (net_id != (size_t)-1) {
                    cur_frame.pc++;
                    bp.wait_target.active = true;
                    bp.wait_target.edge = ev_stmt->edge;
                    bp.wait_target.signal_name = ev_stmt->signal_name;
                    bp.wait_target.net_id = net_id;
                    bp.wait_target.initial_change_counter = engine.netlist[net_id].change_counter;
                    break;
                }
                cur_frame.pc++;
                continue;
            }

            // Event Trigger inside task: -> e or ->> e (IEEE 1800-2023 §15.4)
            if (auto trig = dynamic_cast<EventTriggerStatementNode*>(stmt)) {
                cur_frame.pc++;
                trigger_named_event(trig->event_name, bp.scope, trig->is_nonblocking);
                continue;
            }

            // wait(e.triggered) inside task: (IEEE 1800-2023 §15.5)
            if (auto we = dynamic_cast<WaitEventTriggeredStatementNode*>(stmt)) {
                std::string scoped_ev = bp.scope.empty() ? we->event_name : (bp.scope + "." + we->event_name);
                size_t ev_id = (size_t)-1;
                if (engine.named_event_map.find(scoped_ev) != engine.named_event_map.end()) {
                    ev_id = engine.named_event_map.at(scoped_ev);
                } else if (engine.named_event_map.find(we->event_name) != engine.named_event_map.end()) {
                    ev_id = engine.named_event_map.at(we->event_name);
                }
                cur_frame.pc++;
                if (ev_id != (size_t)-1) {
                    auto& ev = engine.named_events[ev_id];
                    if (ev.last_triggered_time == current_sim_time) {
                        continue;
                    } else {
                        bp.waiting_event_id = ev_id;
                        ev.waiting_pids.push_back(pid);
                        break;
                    }
                }
                continue;
            }

            // Sub-task invocation inside task
            if (auto sub_tcall = dynamic_cast<TaskCallNode*>(stmt)) {
                std::string tname = sub_tcall->task_name;
                std::string scoped_tname = bp.scope.empty() ? tname : (bp.scope + "." + tname);
                if (engine.dpi_import_map.find(scoped_tname) != engine.dpi_import_map.end() ||
                    engine.dpi_import_map.find(tname) != engine.dpi_import_map.end()) {
                    engine.evaluator.execute_statement(stmt, bp);
                    cur_frame.pc++;
                    continue;
                }
                auto dot = tname.rfind('.');
                if (dot != std::string::npos) {
                    std::string var_name = tname.substr(0, dot);
                    std::string method_name = tname.substr(dot + 1);
                    ClassObject* cl_obj = nullptr;
                    if (var_name == "this" || var_name == "super") {
                        cl_obj = engine.evaluator.get_expr_eval().get_current_this();
                    } else {
                        if (engine.evaluator.get_expr_eval().get_context().has_active_frame()) {
                            LocalVar* local = engine.evaluator.get_expr_eval().get_context().lookup_local(var_name);
                            if (local && local->is_class_handle) cl_obj = engine.get_class_object(local->value.aval).get();
                        }
                        if (!cl_obj && !bp.call_stack.empty()) {
                            for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                                LocalVar* local = it->local_frame.get_var(var_name);
                                if (local && local->is_class_handle) { cl_obj = engine.get_class_object(local->value.aval).get(); break; }
                            }
                        }
                        if (!cl_obj) {
                            size_t nid = engine.get_net_id(bp.scope, var_name);
                            if (nid != (size_t)-1 && engine.netlist[nid].is_class_handle) {
                                cl_obj = engine.get_class_object(engine.netlist[nid].value.aval).get();
                            }
                        }
                    }
                    if (cl_obj && cl_obj->class_def && cl_obj->class_def->find_task(method_name)) {
                        invoke_task(bp, sub_tcall);
                        continue;
                    }
                    if (engine.is_queue(var_name, bp.scope) || engine.is_dynamic_array(var_name, bp.scope) || engine.is_assoc_array(var_name, bp.scope) || engine.is_class_handle(var_name, bp.scope) || engine.is_covergroup_handle(var_name, bp.scope) || engine.is_semaphore(var_name, bp.scope) || engine.is_mailbox(var_name, bp.scope) || var_name == "this" || var_name == "super") {
                        engine.evaluator.execute_statement(stmt, bp);
                        if (bp.is_waiting_sync) break;
                        cur_frame.pc++;
                        continue;
                    }
                }
                invoke_task(bp, sub_tcall);
                continue;
            }

            // Var declaration inside task
            if (auto vdecl = dynamic_cast<VarDeclStatementNode*>(stmt)) {
                cur_frame.local_frame.declare_var(vdecl->var_name, vdecl->width, vdecl->is_signed);
                cur_frame.pc++;
                continue;
            }

            // Fork inside task: IEEE 1800-2023 §9.3.2
            if (auto fork_stmt = dynamic_cast<ForkStatementNode*>(stmt)) {
                cur_frame.pc++;
                invoke_fork(pid, fork_stmt);
                if (engine.processes[pid].fork_barrier.active) {
                    break;
                }
                continue;
            }

            // Disable fork inside task: IEEE 1800-2023 §9.6.2
            if (auto df = dynamic_cast<DisableForkStatementNode*>(stmt)) {
                cur_frame.pc++;
                disable_fork(pid);
                continue;
            }

            // Wait fork inside task: IEEE 1800-2023 §9.6.3
            if (auto wf = dynamic_cast<WaitForkStatementNode*>(stmt)) {
                cur_frame.pc++;
                if (has_active_descendants(pid)) {
                    engine.processes[pid].is_waiting_fork = true;
                    break;
                }
                continue;
            }

            // Standard statement execution inside task
            engine.evaluator.execute_statement(stmt, bp);
            if (bp.is_waiting_sync) break;
            cur_frame.pc++;
            continue;
        }

        // B. Dynamic Statements (Child Process from Fork)
        if (bp.parent_pid != (size_t)-1) {
            if (bp.pc >= bp.dynamic_statements.size()) {
                bp.completed = true;
                notify_parent_child_completed(pid);
                break;
            }

            StatementNode* stmt = bp.dynamic_statements[bp.pc];

            // 1. Procedural In-Body Event Control: @(...)
            if (auto ev_stmt = dynamic_cast<EventControlStatementNode*>(stmt)) {
                std::string scoped_sig = bp.scope.empty() ? ev_stmt->signal_name : (bp.scope + "." + ev_stmt->signal_name);
                size_t ev_id = (size_t)-1;
                if (engine.named_event_map.find(scoped_sig) != engine.named_event_map.end()) {
                    ev_id = engine.named_event_map.at(scoped_sig);
                } else if (engine.named_event_map.find(ev_stmt->signal_name) != engine.named_event_map.end()) {
                    ev_id = engine.named_event_map.at(ev_stmt->signal_name);
                }
                if (ev_id != (size_t)-1) {
                    bp.pc++;
                    bp.waiting_event_id = ev_id;
                    engine.named_events[ev_id].waiting_pids.push_back(pid);
                    break;
                }

                size_t net_id = (size_t)-1;
                auto dot_p = ev_stmt->signal_name.find('.');
                if (dot_p != std::string::npos) {
                    std::string base = ev_stmt->signal_name.substr(0, dot_p);
                    std::string mem = ev_stmt->signal_name.substr(dot_p + 1);
                    VirtualInterfaceHandle vif = engine.lookup_virtual_interface(base, bp.scope, engine.evaluator.get_expr_eval().get_current_this(), &bp);
                    if (vif.is_valid_vif) {
                        net_id = engine.resolve_vif_net_id(vif, mem);
                    }
                }
                if (net_id == (size_t)-1) {
                    net_id = engine.get_net_id(bp.scope, ev_stmt->signal_name);
                }
                if (net_id == (size_t)-1) {
                    bp.pc++;
                    continue;
                }

                bp.pc++;
                bp.wait_target.active = true;
                bp.wait_target.edge = ev_stmt->edge;
                bp.wait_target.signal_name = ev_stmt->signal_name;
                bp.wait_target.net_id = net_id;
                bp.wait_target.initial_change_counter = engine.netlist[net_id].change_counter;
                break;
            }

            // Event Trigger: -> e or ->> e (IEEE 1800-2023 §15.4)
            if (auto trig = dynamic_cast<EventTriggerStatementNode*>(stmt)) {
                bp.pc++;
                trigger_named_event(trig->event_name, bp.scope, trig->is_nonblocking);
                continue;
            }

            // wait(e.triggered): (IEEE 1800-2023 §15.5)
            if (auto we = dynamic_cast<WaitEventTriggeredStatementNode*>(stmt)) {
                std::string scoped_ev = bp.scope.empty() ? we->event_name : (bp.scope + "." + we->event_name);
                size_t ev_id = (size_t)-1;
                if (engine.named_event_map.find(scoped_ev) != engine.named_event_map.end()) {
                    ev_id = engine.named_event_map.at(scoped_ev);
                } else if (engine.named_event_map.find(we->event_name) != engine.named_event_map.end()) {
                    ev_id = engine.named_event_map.at(we->event_name);
                }
                bp.pc++;
                if (ev_id != (size_t)-1) {
                    auto& ev = engine.named_events[ev_id];
                    if (ev.last_triggered_time == current_sim_time) {
                        continue;
                    } else {
                        bp.waiting_event_id = ev_id;
                        ev.waiting_pids.push_back(pid);
                        break;
                    }
                }
                continue;
            }

            // 2. Procedural Delay: #delay
            if (auto delay = dynamic_cast<DelayStatementNode*>(stmt)) {
                bp.pc++;
                uint64_t d = delay->delay;
                if (delay->delay_expr) {
                    LogicVal dval = engine.evaluator.eval_expr(delay->delay_expr.get(), bp.scope);
                    d = dval.is_unknown() ? 0 : dval.aval;
                }
                uint64_t wake_time = current_sim_time + d;
                bp.is_delayed = true;
                time_wheel.schedule(wake_time, (uint32_t)pid);
                break;
            }

            // 3. Task Invocation: invoke task activation frame
            if (auto tcall = dynamic_cast<TaskCallNode*>(stmt)) {
                std::string tname = tcall->task_name;
                std::string scoped_tname = bp.scope.empty() ? tname : (bp.scope + "." + tname);
                if (engine.dpi_import_map.find(scoped_tname) != engine.dpi_import_map.end() ||
                    engine.dpi_import_map.find(tname) != engine.dpi_import_map.end()) {
                    engine.evaluator.execute_statement(stmt, bp);
                    bp.pc++;
                    continue;
                }
                auto dot = tname.rfind('.');
                if (dot != std::string::npos) {
                    std::string var_name = tname.substr(0, dot);
                    std::string method_name = tname.substr(dot + 1);
                    ClassObject* cl_obj = nullptr;
                    if (var_name == "this" || var_name == "super") {
                        cl_obj = engine.evaluator.get_expr_eval().get_current_this();
                    } else {
                        if (engine.evaluator.get_expr_eval().get_context().has_active_frame()) {
                            LocalVar* local = engine.evaluator.get_expr_eval().get_context().lookup_local(var_name);
                            if (local && local->is_class_handle) cl_obj = engine.get_class_object(local->value.aval).get();
                        }
                        if (!cl_obj && !bp.call_stack.empty()) {
                            for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                                LocalVar* local = it->local_frame.get_var(var_name);
                                if (local && local->is_class_handle) { cl_obj = engine.get_class_object(local->value.aval).get(); break; }
                            }
                        }
                        if (!cl_obj) {
                            size_t nid = engine.get_net_id(bp.scope, var_name);
                            if (nid != (size_t)-1 && engine.netlist[nid].is_class_handle) {
                                cl_obj = engine.get_class_object(engine.netlist[nid].value.aval).get();
                            }
                        }
                    }
                    if (cl_obj && cl_obj->class_def && cl_obj->class_def->find_task(method_name)) {
                        invoke_task(bp, tcall);
                        continue;
                    }
                    if (engine.is_queue(var_name, bp.scope) || engine.is_dynamic_array(var_name, bp.scope) || engine.is_assoc_array(var_name, bp.scope) || engine.is_class_handle(var_name, bp.scope) || engine.is_covergroup_handle(var_name, bp.scope) || engine.is_semaphore(var_name, bp.scope) || engine.is_mailbox(var_name, bp.scope) || var_name == "this" || var_name == "super") {
                        engine.evaluator.execute_statement(stmt, bp);
                        if (bp.is_waiting_sync) break;
                        bp.pc++;
                        continue;
                    }
                }
                invoke_task(bp, tcall);
                continue;
            }

            // 4. Nested Fork: IEEE 1800-2023 §9.3.2
            if (auto fork_stmt = dynamic_cast<ForkStatementNode*>(stmt)) {
                bp.pc++;
                invoke_fork(pid, fork_stmt);
                if (engine.processes[pid].fork_barrier.active) {
                    break;
                }
                continue;
            }

            // Disable fork in child: IEEE 1800-2023 §9.6.2
            if (auto df = dynamic_cast<DisableForkStatementNode*>(stmt)) {
                bp.pc++;
                disable_fork(pid);
                continue;
            }

            // Wait fork in child: IEEE 1800-2023 §9.6.3
            if (auto wf = dynamic_cast<WaitForkStatementNode*>(stmt)) {
                bp.pc++;
                if (has_active_descendants(pid)) {
                    engine.processes[pid].is_waiting_fork = true;
                    break;
                }
                continue;
            }

            // 5. Intra-assignment event / delay
            if (auto assign = dynamic_cast<BlockingAssignNode*>(stmt)) {
                if (assign->has_intra_event) {
                    LogicVal rhs_val = engine.evaluator.eval_expr(assign->rhs.get(), bp.scope);
                    std::string target_name = assign->lhs_targets[0];
                    bp.pc++;

                    size_t trig_net_id = (size_t)-1;
                    std::string scoped_sig = bp.scope.empty() ? assign->intra_signal : (bp.scope + "." + assign->intra_signal);
                    if (engine.net_map.find(scoped_sig) != engine.net_map.end()) trig_net_id = engine.net_map.at(scoped_sig);
                    else if (engine.net_map.find(assign->intra_signal) != engine.net_map.end()) trig_net_id = engine.net_map.at(assign->intra_signal);

                    if (trig_net_id != (size_t)-1) {
                        bp.is_delayed = true;
                        queue_intra_event_blocking(trig_net_id, assign->intra_edge, pid, target_name, bp.scope, rhs_val);
                    }
                    break;
                }
                if (assign->intra_delay > 0) {
                    LogicVal rhs_val = engine.evaluator.eval_expr(assign->rhs.get(), bp.scope);
                    std::string target_name = assign->lhs_targets[0];
                    bp.pc++;
                    uint64_t wake_time = current_sim_time + assign->intra_delay;
                    bp.is_delayed = true;
                    queue_delayed_blocking(wake_time, pid, target_name, bp.scope, rhs_val);
                    break;
                }
            }

            // 6. Standard Statement Execution
            engine.evaluator.execute_statement(stmt, bp);
            if (!simulation_active) break;
            if (bp.is_waiting_sync) break;
            bp.pc++;
            continue;
        }

        // C. Top-level Root Process Body Execution
        engine.evaluator.set_current_this(nullptr);
        if (bp.pc >= bp.proc->body.size()) {
            if (bp.is_looping) {
                bp.pc = bp.loop_start_pc;
                if (bp.pc < bp.proc->body.size()) continue;
            } else if (bp.proc->type == ProcessType::ALWAYS) {
                bp.pc = 0;
                if (!bp.proc->body.empty()) continue;
            } else {
                bp.completed = true;
            }
            break;
        }

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

        // 0b. Loop unrolling / expansion for ForStatementNode with blocking timing controls
        if (auto for_stmt = dynamic_cast<ForStatementNode*>(stmt)) {
            if (stmt_has_timing_control(for_stmt)) {
                if (for_stmt->init_stmt) {
                    engine.evaluator.execute_statement(for_stmt->init_stmt.get(), bp);
                    for_stmt->init_stmt.reset();
                }
                bool cond = true;
                if (for_stmt->condition) {
                    cond = engine.evaluator.eval_expr(for_stmt->condition.get(), bp.scope).to_bool();
                }
                if (cond) {
                    std::vector<std::unique_ptr<StatementNode>> new_body;
                    for (size_t i = 0; i < bp.pc; ++i) {
                        new_body.push_back(std::move(bp.proc->body[i]));
                    }
                    for (auto& s : for_stmt->body) {
                        if (auto blk = dynamic_cast<BlockStatementNode*>(s.get())) {
                            for (auto& bs : blk->statements) {
                                if (bs) new_body.push_back(bs->clone());
                            }
                        } else if (s) {
                            new_body.push_back(s->clone());
                        }
                    }
                    if (for_stmt->step_stmt) {
                        new_body.push_back(for_stmt->step_stmt->clone());
                    }
                    new_body.push_back(std::move(bp.proc->body[bp.pc]));
                    for (size_t i = bp.pc + 1; i < bp.proc->body.size(); ++i) {
                        new_body.push_back(std::move(bp.proc->body[i]));
                    }
                    bp.proc->body = std::move(new_body);
                    continue;
                } else {
                    bp.pc++;
                    continue;
                }
            }
        }

        // 1. Procedural In-Body Event Control: @(...)
        if (auto ev_stmt = dynamic_cast<EventControlStatementNode*>(stmt)) {
            std::string scoped_sig = bp.scope.empty() ? ev_stmt->signal_name : (bp.scope + "." + ev_stmt->signal_name);
            size_t ev_id = (size_t)-1;
            if (engine.named_event_map.find(scoped_sig) != engine.named_event_map.end()) {
                ev_id = engine.named_event_map.at(scoped_sig);
            } else if (engine.named_event_map.find(ev_stmt->signal_name) != engine.named_event_map.end()) {
                ev_id = engine.named_event_map.at(ev_stmt->signal_name);
            }
            if (ev_id != (size_t)-1) {
                bp.pc++;
                bp.waiting_event_id = ev_id;
                engine.named_events[ev_id].waiting_pids.push_back(pid);
                break;
            }

            size_t net_id = (size_t)-1;
            auto dot_p = ev_stmt->signal_name.find('.');
            if (dot_p != std::string::npos) {
                std::string base = ev_stmt->signal_name.substr(0, dot_p);
                std::string mem = ev_stmt->signal_name.substr(dot_p + 1);
                VirtualInterfaceHandle vif = engine.lookup_virtual_interface(base, bp.scope, engine.evaluator.get_expr_eval().get_current_this(), &bp);
                if (vif.id != 0 || !vif.inst_path.empty()) {
                    net_id = engine.resolve_vif_net_id(vif, mem);
                }
            }
            if (net_id == (size_t)-1) {
                net_id = engine.get_net_id(bp.scope, ev_stmt->signal_name);
            }
            if (net_id == (size_t)-1) {
                bp.pc++;
                continue;
            }

            bp.pc++;
            bp.wait_target.active = true;
            bp.wait_target.edge = ev_stmt->edge;
            bp.wait_target.signal_name = ev_stmt->signal_name;
            bp.wait_target.net_id = net_id;
            bp.wait_target.initial_change_counter = engine.netlist[net_id].change_counter;
            break;
        }

        // Event Trigger: -> e or ->> e (IEEE 1800-2023 §15.4)
        if (auto trig = dynamic_cast<EventTriggerStatementNode*>(stmt)) {
            bp.pc++;
            trigger_named_event(trig->event_name, bp.scope, trig->is_nonblocking);
            continue;
        }

        // wait(e.triggered): (IEEE 1800-2023 §15.5)
        if (auto we = dynamic_cast<WaitEventTriggeredStatementNode*>(stmt)) {
            std::string scoped_ev = bp.scope.empty() ? we->event_name : (bp.scope + "." + we->event_name);
            size_t ev_id = (size_t)-1;
            if (engine.named_event_map.find(scoped_ev) != engine.named_event_map.end()) {
                ev_id = engine.named_event_map.at(scoped_ev);
            } else if (engine.named_event_map.find(we->event_name) != engine.named_event_map.end()) {
                ev_id = engine.named_event_map.at(we->event_name);
            }
            bp.pc++;
            if (ev_id != (size_t)-1) {
                auto& ev = engine.named_events[ev_id];
                if (ev.last_triggered_time == current_sim_time) {
                    continue;
                } else {
                    bp.waiting_event_id = ev_id;
                    ev.waiting_pids.push_back(pid);
                    break;
                }
            }
            continue;
        }

        // Cycle Delay Statement: ##N
        if (auto cdelay = dynamic_cast<CycleDelayStatementNode*>(stmt)) {
            bp.pc++;
            int64_t cycles = 1;
            if (cdelay->cycle_expr) {
                LogicVal cval = engine.evaluator.eval_expr(cdelay->cycle_expr.get(), bp.scope);
                cycles = cval.is_unknown() ? 0 : (int64_t)cval.aval;
            }
            if (cycles > 0) {
                size_t cb_id = engine.get_clocking_block_id(cdelay->clocking_name, bp.scope);
                bp.is_waiting_cycle = true;
                bp.cycle_delay_count = (size_t)cycles;
                bp.clocking_block_id = cb_id;
                break;
            }
            continue;
        }

        // 2. Procedural Delay: #delay
        if (auto delay = dynamic_cast<DelayStatementNode*>(stmt)) {
            bp.pc++;
            uint64_t d = delay->delay;
            if (delay->delay_expr) {
                LogicVal dval = engine.evaluator.eval_expr(delay->delay_expr.get(), bp.scope);
                d = dval.is_unknown() ? 0 : dval.aval;
            }
            uint64_t wake_time = current_sim_time + d;
            bp.is_delayed = true;
            time_wheel.schedule(wake_time, (uint32_t)pid);
            break;
        }

        // 3. Task Invocation: invoke task activation frame
        if (auto tcall = dynamic_cast<TaskCallNode*>(stmt)) {
            std::string tname = tcall->task_name;
            std::string scoped_tname = bp.scope.empty() ? tname : (bp.scope + "." + tname);
            if (engine.dpi_import_map.find(scoped_tname) != engine.dpi_import_map.end() ||
                engine.dpi_import_map.find(tname) != engine.dpi_import_map.end()) {
                engine.evaluator.execute_statement(stmt, bp);
                bp.pc++;
                continue;
            }
            auto dot = tname.rfind('.');
            if (dot != std::string::npos) {
                std::string var_name = tname.substr(0, dot);
                std::string method_name = tname.substr(dot + 1);
                ClassObject* cl_obj = nullptr;
                if (var_name == "this" || var_name == "super") {
                    cl_obj = engine.evaluator.get_expr_eval().get_current_this();
                } else {
                    if (engine.evaluator.get_expr_eval().get_context().has_active_frame()) {
                        LocalVar* local = engine.evaluator.get_expr_eval().get_context().lookup_local(var_name);
                        if (local && local->is_class_handle) cl_obj = engine.get_class_object(local->value.aval).get();
                    }
                    if (!cl_obj && !bp.call_stack.empty()) {
                        for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                            LocalVar* local = it->local_frame.get_var(var_name);
                            if (local && local->is_class_handle) { cl_obj = engine.get_class_object(local->value.aval).get(); break; }
                        }
                    }
                    if (!cl_obj) {
                        size_t nid = engine.get_net_id(bp.scope, var_name);
                        if (nid != (size_t)-1 && engine.netlist[nid].is_class_handle) {
                            cl_obj = engine.get_class_object(engine.netlist[nid].value.aval).get();
                        }
                    }
                }
                if (cl_obj && cl_obj->class_def && cl_obj->class_def->find_task(method_name)) {
                    invoke_task(bp, tcall);
                    continue;
                }
                if (engine.is_queue(var_name, bp.scope) || engine.is_dynamic_array(var_name, bp.scope) || engine.is_assoc_array(var_name, bp.scope) || engine.is_class_handle(var_name, bp.scope) || engine.is_covergroup_handle(var_name, bp.scope) || engine.is_semaphore(var_name, bp.scope) || engine.is_mailbox(var_name, bp.scope) || var_name == "this" || var_name == "super") {
                    engine.evaluator.execute_statement(stmt, bp);
                    if (bp.is_waiting_sync) break;
                    bp.pc++;
                    continue;
                }
            }
            invoke_task(bp, tcall);
            continue;
        }

        // 4. Fork / Join Statements: IEEE 1800-2023 §9.3.2
        if (auto fork_stmt = dynamic_cast<ForkStatementNode*>(stmt)) {
            bp.pc++;
            invoke_fork(pid, fork_stmt);
            if (engine.processes[pid].fork_barrier.active) {
                break;
            }
            continue;
        }

        // Disable fork: IEEE 1800-2023 §9.6.2
        if (auto df = dynamic_cast<DisableForkStatementNode*>(stmt)) {
            bp.pc++;
            disable_fork(pid);
            continue;
        }

        // Wait fork: IEEE 1800-2023 §9.6.3
        if (auto wf = dynamic_cast<WaitForkStatementNode*>(stmt)) {
            bp.pc++;
            if (has_active_descendants(pid)) {
                engine.processes[pid].is_waiting_fork = true;
                break;
            }
            continue;
        }

        // 5. Forever Loop: Inline body and set loop anchor
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

        // IEEE 1800-2023 §10.6.2: Blocking Assignment with Intra-Assignment Event Control
        if (auto assign = dynamic_cast<BlockingAssignNode*>(stmt)) {
            if (assign->has_intra_event) {
                LogicVal rhs_val = engine.evaluator.eval_expr(assign->rhs.get(), bp.scope);
                std::string target_name = assign->lhs_targets[0];
                bp.pc++;

                size_t trig_net_id = (size_t)-1;
                std::string scoped_sig = bp.scope.empty() ? assign->intra_signal : (bp.scope + "." + assign->intra_signal);
                if (engine.net_map.find(scoped_sig) != engine.net_map.end()) trig_net_id = engine.net_map.at(scoped_sig);
                else if (engine.net_map.find(assign->intra_signal) != engine.net_map.end()) trig_net_id = engine.net_map.at(assign->intra_signal);

                if (trig_net_id != (size_t)-1) {
                    bp.is_delayed = true;
                    queue_intra_event_blocking(trig_net_id, assign->intra_edge, pid, target_name, bp.scope, rhs_val);
                }
                break;
            }
        }

        // 6. IEEE 1800-2023 §10.6.1: Blocking Assignment with Intra-Assignment Delay
        if (auto assign = dynamic_cast<BlockingAssignNode*>(stmt)) {
            if (assign->intra_delay > 0) {
                LogicVal rhs_val = engine.evaluator.eval_expr(assign->rhs.get(), bp.scope);
                std::string target_name = assign->lhs_targets[0];
                bp.pc++;
                uint64_t wake_time = current_sim_time + assign->intra_delay;
                bp.is_delayed = true;
                queue_delayed_blocking(wake_time, pid, target_name, bp.scope, rhs_val);
                break;
            }
        }

        // 7. Standard Statement Execution
        engine.evaluator.execute_statement(stmt, bp);
        if (!simulation_active) break;
        if (bp.is_waiting_sync) break;
        bp.pc++;
    }

    engine.evaluator.set_current_this(nullptr);
    engine.evaluator.set_current_process(nullptr);
}

void StratifiedScheduler::execute_time_slot_delta_loop() {
    bool activity = true;
    size_t loop_guard = 0;
    const size_t MAX_DELTA_ITERS = 100;

    while (activity && loop_guard < MAX_DELTA_ITERS) {
        activity = false;

        // 1. Active Region Delta Loop
        bool active_activity = true;
        size_t active_guard = 0;
        while (active_activity && active_guard < 50) {
            active_activity = false;

            propagate_comb();
            evaluate_sequential_blocks();
            evaluate_clocking_blocks();
            evaluate_covergroup_events();

            if (trigger_intra_event_nbas()) {
                active_activity = true;
            }

            if (trigger_intra_event_blocking()) {
                active_activity = true;
                propagate_comb();
            }

            if (resume_waiting_processes(false /* active only */)) {
                active_activity = true;
                propagate_comb();
            }

            active_guard++;
        }

        // 2. NBA Region
        if (commit_nba_updates()) {
            activity = true;
            propagate_comb();
        }

        // 3. Observed Region
        if (evaluate_sva_assertions()) {
            activity = true;
            propagate_comb();
        }

        // 4. Reactive Region (IEEE 1800-2023 §24)
        bool reactive_activity = false;
        if (!due_reactive_pids.empty()) {
            auto pids = std::move(due_reactive_pids);
            due_reactive_pids.clear();
            for (uint32_t pid : pids) {
                if (!simulation_active) break;
                step_process(pid);
                reactive_activity = true;
            }
        }

        if (resume_waiting_processes(true /* reactive only */)) {
            reactive_activity = true;
        }

        if (reactive_activity) {
            activity = true;
            propagate_comb();
        }

        check_program_completion();

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
    for (auto& cb : engine.clocking_blocks) {
        cb.fired_in_time_slot = false;
    }
    for (auto& asrt : engine.sva_assertions) {
        asrt.fired_in_time_slot = false;
    }
    for (uint64_t hid : engine.event_triggered_covergroups) {
        if (auto inst = engine.get_covergroup_instance(hid)) {
            inst->fired_in_time_slot = false;
        }
    }
    capture_preponed_samples();
    for (auto& cb : engine.clocking_blocks) {
        for (auto& kv : cb.signals) {
            if (kv.second.direction == "input" || kv.second.direction == "inout") {
                if (kv.second.net_id < engine.netlist.size()) {
                    kv.second.sampled_value = engine.netlist[kv.second.net_id].value;
                }
            }
        }
    }

    size_t num_root_processes = engine.processes.size();
    for (size_t i = 0; i < num_root_processes; ++i) {
        if (engine.processes[i].parent_pid != (size_t)-1) continue;
        if (engine.processes[i].is_reactive) continue;
        if (engine.processes[i].proc->type == ProcessType::INITIAL || 
            engine.processes[i].proc->type == ProcessType::ALWAYS) {
            step_process(i);
            propagate_comb();
        }
    }

    for (size_t i = 0; i < num_root_processes; ++i) {
        if (engine.processes[i].parent_pid != (size_t)-1) continue;
        if (engine.processes[i].is_reactive) {
            if (engine.processes[i].proc->type == ProcessType::INITIAL || 
                engine.processes[i].proc->type == ProcessType::ALWAYS) {
                due_reactive_pids.push_back((uint32_t)i);
            }
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
        for (auto& cb : engine.clocking_blocks) {
            cb.fired_in_time_slot = false;
        }
        for (auto& asrt : engine.sva_assertions) {
            asrt.fired_in_time_slot = false;
        }
        for (uint64_t hid : engine.event_triggered_covergroups) {
            if (auto inst = engine.get_covergroup_instance(hid)) {
                inst->fired_in_time_slot = false;
            }
        }
        capture_preponed_samples();

        // 1. Active Region: Drain events for current timestamp
        std::vector<uint32_t> due_active_pids;
        due_reactive_pids.clear();
        while (!time_wheel.empty() && time_wheel.next_time() == slot_time && simulation_active) {
            SimEvent ev = time_wheel.pop();
            if (ev.process_id != UINT32_MAX) {
                if (ev.process_id < engine.processes.size()) {
                    engine.processes[ev.process_id].is_delayed = false;
                    if (engine.processes[ev.process_id].is_reactive) {
                        due_reactive_pids.push_back(ev.process_id);
                    } else {
                        due_active_pids.push_back(ev.process_id);
                    }
                }
            }
        }

        for (uint32_t pid : due_active_pids) {
            if (!simulation_active) break;
            step_process(pid);
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
                if (b_item.pid < engine.processes.size()) {
                    engine.processes[b_item.pid].is_delayed = false;
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
