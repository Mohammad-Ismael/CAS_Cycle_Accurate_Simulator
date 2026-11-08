#include "../../include/kernel/scheduler.hpp"
#include "../../include/engine.hpp"
#include "../../include/kernel/logic_ops.hpp"
#include "../../include/frontend/lexer.hpp"
#include "../../include/kernel/trace_buffer.hpp"
#include "../../include/kernel/simd_ops.hpp"
#include <iostream>
#include <algorithm>

StratifiedScheduler::StratifiedScheduler(Engine& eng) : engine(eng) {}

void StratifiedScheduler::schedule_event(uint64_t wake_time, uint32_t pid) {
    time_wheel.schedule(wake_time, pid);
}

void StratifiedScheduler::queue_nba(size_t net_id, const LogicVal& val, uint32_t offset, uint32_t slice_width, bool is_slice) {
    nba_queue.push_back({net_id, val, offset, slice_width, is_slice});
}

void StratifiedScheduler::queue_nba_mem(const std::string& scoped_mem, int64_t addr, const LogicVal& val, uint32_t offset, uint32_t slice_width, bool is_slice) {
    nba_mem_queue.push_back({scoped_mem, addr, val, offset, slice_width, is_slice});
}

void StratifiedScheduler::queue_delayed_nba(uint64_t wake_time, size_t net_id, const LogicVal& val, uint32_t offset, uint32_t slice_width, bool is_slice) {
    delayed_nba_queue[wake_time].push_back({net_id, val, offset, slice_width, is_slice});
    time_wheel.schedule(wake_time, UINT32_MAX);
}

void StratifiedScheduler::queue_delayed_nba_mem(uint64_t wake_time, const std::string& scoped_mem, int64_t addr, const LogicVal& val, uint32_t offset, uint32_t slice_width, bool is_slice) {
    delayed_nba_mem_queue[wake_time].push_back({scoped_mem, addr, val, offset, slice_width, is_slice});
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
                    LogicVal old_v = engine.netlist[net_id].value;
                    LogicVal casted = LogicOps::cast_width(item.val, engine.netlist[net_id].width, engine.netlist[net_id].is_signed);
                    engine.netlist[net_id].set(casted);
                    if (engine.netlist[net_id].value != old_v) {
                        notify_net_changed(net_id);
                    }
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
    net_time_history[current_sim_time] = preponed_net_values;
    while (net_time_history.size() > 1024) {
        net_time_history.erase(net_time_history.begin());
    }
}

LogicVal StratifiedScheduler::get_historical_net_value(size_t net_id, uint64_t sample_time) const {
    if (net_time_history.empty()) {
        return net_id < engine.netlist.size() ? engine.netlist[net_id].value : LogicVal::make_x(1);
    }
    auto it = net_time_history.upper_bound(sample_time);
    if (it != net_time_history.begin()) {
        --it;
        if (net_id < it->second.size()) {
            return it->second[net_id];
        }
    }
    return net_id < engine.netlist.size() ? engine.netlist[net_id].value : LogicVal::make_x(1);
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

        // REACTIVE REGION: execute pending sequence match subroutines (Â§16.11)
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
                    if (net_id < engine.alias_members.size()) {
                        for (size_t other_id : engine.alias_members[net_id]) {
                            if (other_id != net_id && other_id < engine.netlist.size()) {
                                engine.netlist[other_id].set(casted);
                            }
                        }
                    }
                    if (net_id < engine.alias_members.size()) {
                        for (size_t other_id : engine.alias_members[net_id]) {
                            if (other_id != net_id && other_id < engine.netlist.size()) {
                                engine.netlist[other_id].set(casted);
                            }
                        }
                    }
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

    // 3. Commit due specify path events
    std::vector<size_t> committed_specify_nets;
    for (auto& kv : pending_specify_events) {
        if (kv.second.target_time <= target_time) {
            size_t nid = kv.second.net_id;
            engine.evaluator.write_slice({nid, 0, engine.netlist[nid].width}, kv.second.new_val);
            committed_specify_nets.push_back(kv.first);
        }
    }
    for (size_t nid : committed_specify_nets) {
        pending_specify_events.erase(nid);
    }
}

void StratifiedScheduler::terminate() {
    simulation_active = false;
    time_wheel.clear();
}

static double t_slice_ms = 0;
static double t_rca_ms = 0;
static double t_proc_ms = 0;

static bool comb_dirty = true;

void StratifiedScheduler::enqueue_rca(uint32_t rca_idx) {
    bool has_topo = (llvm_dispatch.is_active && llvm_dispatch.topological_comb_fn != nullptr) ||
                    (native_dispatch.is_active && native_dispatch.topological_comb_fn != nullptr);
    if (has_topo) {
        if (rca_idx < is_topological_rca.size() && is_topological_rca[rca_idx]) {
            comb_dirty = true;
            return;
        }
    }
    if (rca_enqueued.size() != engine.resolved_continuous_assigns.size()) {
        rca_enqueued.assign(engine.resolved_continuous_assigns.size(), 0);
    }
    if (rca_idx < rca_enqueued.size() && !rca_enqueued[rca_idx]) {
        rca_enqueued[rca_idx] = 1;
        dirty_rcas.push_back(rca_idx);
        comb_dirty = true;
    }
}

void StratifiedScheduler::enqueue_comb_proc(uint32_t proc_id) {
    if (comb_proc_enqueued.size() != engine.processes.size()) {
        comb_proc_enqueued.assign(engine.processes.size(), 0);
    }
    if (proc_id < comb_proc_enqueued.size() && !comb_proc_enqueued[proc_id]) {
        comb_proc_enqueued[proc_id] = 1;
        dirty_comb_procs.push_back(proc_id);
        comb_dirty = true;
    }
}

void StratifiedScheduler::sync_all_native_nets() {
    native_nets_u64.resize(engine.netlist.size(), 0);
    native_nets_next.resize(engine.netlist.size(), 0);
    native_modified_mask.assign(engine.netlist.size(), 0);
    dirty_native_nets.clear();
    for (size_t i = 0; i < engine.netlist.size(); ++i) {
        native_nets_u64[i] = engine.netlist[i].value.aval;
        native_nets_next[i] = engine.netlist[i].value.aval;
    }

    native_clk_net_ids.clear();
    for (auto& bp : engine.processes) {
        if (bp.is_native_seq && bp.proc) {
            for (auto& sens : bp.proc->sensitivity_list) {
                if (sens.edge == EdgeType::POSEDGE) {
                    if (sens.net_id == (size_t)-1) {
                        sens.net_id = engine.get_net_id(bp.scope, sens.signal_name);
                    }
                    if (sens.net_id < engine.netlist.size()) {
                        uint32_t cid = (sens.net_id < engine.canonical_net_map.size()) ?
                                       (uint32_t)engine.canonical_net_map[sens.net_id] : (uint32_t)sens.net_id;
                        bool exists = false;
                        for (uint32_t existing : native_clk_net_ids) {
                            if (existing == cid) { exists = true; break; }
                        }
                        if (!exists) {
                            native_clk_net_ids.push_back(cid);
                        }
                        if (sens.net_id != cid) {
                            exists = false;
                            for (uint32_t existing : native_clk_net_ids) {
                                if (existing == (uint32_t)sens.net_id) { exists = true; break; }
                            }
                            if (!exists) {
                                native_clk_net_ids.push_back((uint32_t)sens.net_id);
                            }
                        }
                    }
                }
            }
        }
    }

    for (size_t i = 0; i < engine.netlist.size(); ++i) {
        const auto& nname = engine.netlist[i].name;
        if (nname == "clk_i" || nname == "clk" || nname == "clk_sys" || nname == "IO_CLK") {
            uint32_t cid = (i < engine.canonical_net_map.size()) ?
                           (uint32_t)engine.canonical_net_map[i] : (uint32_t)i;
            bool exists = false;
            for (uint32_t existing : native_clk_net_ids) {
                if (existing == cid) { exists = true; break; }
            }
            if (!exists) {
                native_clk_net_ids.push_back(cid);
            }
        }
    }

    // Populate topological RCA lookup and target records
    is_topological_rca.assign(engine.resolved_continuous_assigns.size(), 0);
    topological_target_records.clear();
    for (uint32_t rca_idx : engine.get_topological_order().topological_rca_order) {
        if (rca_idx < engine.resolved_continuous_assigns.size()) {
            is_topological_rca[rca_idx] = 1;
            uint32_t nid = engine.resolved_continuous_assigns[rca_idx].target.net_id;
            if (nid < engine.netlist.size()) {
                uint32_t cid = (nid < engine.canonical_net_map.size()) ?
                               (uint32_t)engine.canonical_net_map[nid] : nid;
                topological_target_records.push_back({nid, cid, engine.netlist[nid].width, engine.netlist[nid].is_signed});
            }
        }
    }
    std::sort(topological_target_records.begin(), topological_target_records.end(),
        [](const TopoTargetRecord& a, const TopoTargetRecord& b) { return a.net_id < b.net_id; });
    topological_target_records.erase(
        std::unique(topological_target_records.begin(), topological_target_records.end(),
            [](const TopoTargetRecord& a, const TopoTargetRecord& b) { return a.net_id == b.net_id; }),
        topological_target_records.end());
}

void StratifiedScheduler::init_scheduler() {
    sync_all_native_nets();
    native_seq_dst_nets.clear();
    for (const auto& bp : engine.processes) {
        if (bp.is_native_seq) {
            for (uint32_t tid : bp.comb_target_nets) {
                native_seq_dst_nets.push_back(tid);
            }
        }
    }
    memory_fastpath.init(engine);
}

void StratifiedScheduler::notify_net_changed(size_t net_id) {
    if (net_id >= engine.netlist.size()) return;
    size_t canon_nid = engine.get_canonical_net_id(net_id);
    if (native_dispatch.is_active || llvm_dispatch.is_active) {
        if (net_id < native_nets_u64.size()) {
            native_nets_u64[net_id] = engine.netlist[net_id].value.aval;
        }
        if (canon_nid < native_nets_u64.size()) {
            native_nets_u64[canon_nid] = engine.netlist[canon_nid].value.aval;
        }
        if (canon_nid < engine.alias_members.size()) {
            for (size_t m_id : engine.alias_members[canon_nid]) {
                if (m_id < native_nets_u64.size()) {
                    native_nets_u64[m_id] = engine.netlist[m_id].value.aval;
                }
            }
        }
    }
    if (canon_nid < engine.fanout.net_to_rca.size()) {
        const auto& rca_list = engine.fanout.net_to_rca[canon_nid];
        for (uint32_t rca_idx : rca_list) {
            enqueue_rca(rca_idx);
        }
    }
    if (canon_nid < engine.fanout.net_to_comb_proc.size()) {
        const auto& proc_list = engine.fanout.net_to_comb_proc[canon_nid];
        for (uint32_t proc_id : proc_list) {
            enqueue_comb_proc(proc_id);
        }
    }
    if (canon_nid < engine.fanout.net_to_mca.size() && !engine.fanout.net_to_mca[canon_nid].empty()) {
        comb_dirty = true;
    }
}

void StratifiedScheduler::notify_mem_changed() {
    comb_dirty = true;
    for (uint32_t rca_idx : engine.fanout.mem_reading_rcas) {
        enqueue_rca(rca_idx);
    }
    for (uint32_t proc_id : engine.fanout.mem_reading_comb_procs) {
        enqueue_comb_proc(proc_id);
    }
}

struct U64StackItem {
    uint64_t val;
    uint32_t width;
};

bool StratifiedScheduler::execute_bytecode_2state_64(const CompiledExpr& expr, uint64_t& out_val) {
    U64StackItem stack[64];
    int sp = 0;

    for (const auto& inst : expr.instructions) {
        switch (inst.op) {
            case Opcode::PUSH_NET_VAL:
            case Opcode::PUSH_NET_U64: {
                if (inst.arg0 >= engine.netlist.size()) return false;
                const auto& net = engine.netlist[inst.arg0];
                if (__builtin_expect(net.value.bval != 0, 0)) return false;
                stack[sp++] = {net.value.aval, net.width};
                break;
            }
            case Opcode::PUSH_CONST_VAL:
            case Opcode::PUSH_CONST_U64: {
                stack[sp++] = {inst.imm64, inst.arg0};
                break;
            }
            case Opcode::ADD: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                uint32_t w = std::min(64U, std::max(a.width, b.width) + 1);
                stack[sp++] = {a.val + b.val, w};
                break;
            }
            case Opcode::SUB: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                uint32_t w = std::min(64U, std::max(a.width, b.width) + 1);
                stack[sp++] = {a.val - b.val, w};
                break;
            }
            case Opcode::MUL: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                uint32_t w = std::min(64U, a.width + b.width);
                stack[sp++] = {a.val * b.val, w};
                break;
            }
            case Opcode::DIV: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                if (b.val == 0) return false;
                stack[sp++] = {a.val / b.val, a.width};
                break;
            }
            case Opcode::MOD: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                if (b.val == 0) return false;
                stack[sp++] = {a.val % b.val, a.width};
                break;
            }
            case Opcode::AND: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                uint32_t w = std::max(a.width, b.width);
                stack[sp++] = {a.val & b.val, w};
                break;
            }
            case Opcode::OR: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                uint32_t w = std::max(a.width, b.width);
                stack[sp++] = {a.val | b.val, w};
                break;
            }
            case Opcode::XOR: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                uint32_t w = std::max(a.width, b.width);
                stack[sp++] = {a.val ^ b.val, w};
                break;
            }
            case Opcode::XNOR: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                uint32_t w = std::max(a.width, b.width);
                uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
                stack[sp++] = {(~(a.val ^ b.val)) & mask, w};
                break;
            }
            case Opcode::SHL: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                uint64_t res = (b.val >= a.width) ? 0ULL : (a.val << b.val);
                stack[sp++] = {res, a.width};
                break;
            }
            case Opcode::SHR: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                uint64_t res = (b.val >= a.width) ? 0ULL : (a.val >> b.val);
                stack[sp++] = {res, a.width};
                break;
            }
            case Opcode::SRA: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                bool is_s = (inst.arg1 != 0);
                if (!is_s) {
                    uint64_t res = (b.val >= a.width) ? 0ULL : (a.val >> b.val);
                    stack[sp++] = {res, a.width};
                } else {
                    if (a.width == 0) { stack[sp++] = {0, 0}; break; }
                    bool sign_bit = (a.val >> (a.width - 1)) & 1ULL;
                    if (b.val >= a.width) {
                        uint64_t res = sign_bit ? ((a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1)) : 0ULL;
                        stack[sp++] = {res, a.width};
                    } else {
                        uint64_t res = a.val >> b.val;
                        if (sign_bit) {
                            uint64_t fill_mask = (~0ULL) << (a.width - b.val);
                            uint64_t width_mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                            res |= (fill_mask & width_mask);
                        }
                        stack[sp++] = {res, a.width};
                    }
                }
                break;
            }
            case Opcode::EQ:
            case Opcode::CASE_EQ: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                stack[sp++] = {(a.val == b.val) ? 1ULL : 0ULL, 1};
                break;
            }
            case Opcode::NE:
            case Opcode::CASE_NE: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                stack[sp++] = {(a.val != b.val) ? 1ULL : 0ULL, 1};
                break;
            }
            case Opcode::LT: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                bool is_s = (inst.arg1 != 0);
                uint64_t res = is_s ? (static_cast<int64_t>(a.val) < static_cast<int64_t>(b.val) ? 1ULL : 0ULL)
                                    : (a.val < b.val ? 1ULL : 0ULL);
                stack[sp++] = {res, 1};
                break;
            }
            case Opcode::LE: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                bool is_s = (inst.arg1 != 0);
                uint64_t res = is_s ? (static_cast<int64_t>(a.val) <= static_cast<int64_t>(b.val) ? 1ULL : 0ULL)
                                    : (a.val <= b.val ? 1ULL : 0ULL);
                stack[sp++] = {res, 1};
                break;
            }
            case Opcode::GT: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                bool is_s = (inst.arg1 != 0);
                uint64_t res = is_s ? (static_cast<int64_t>(a.val) > static_cast<int64_t>(b.val) ? 1ULL : 0ULL)
                                    : (a.val > b.val ? 1ULL : 0ULL);
                stack[sp++] = {res, 1};
                break;
            }
            case Opcode::GE: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                bool is_s = (inst.arg1 != 0);
                uint64_t res = is_s ? (static_cast<int64_t>(a.val) >= static_cast<int64_t>(b.val) ? 1ULL : 0ULL)
                                    : (a.val >= b.val ? 1ULL : 0ULL);
                stack[sp++] = {res, 1};
                break;
            }
            case Opcode::LOGIC_AND: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                stack[sp++] = {(a.val != 0 && b.val != 0) ? 1ULL : 0ULL, 1};
                break;
            }
            case Opcode::LOGIC_OR: {
                if (sp < 2) return false;
                auto b = stack[--sp];
                auto a = stack[--sp];
                stack[sp++] = {(a.val != 0 || b.val != 0) ? 1ULL : 0ULL, 1};
                break;
            }
            case Opcode::NOT: {
                if (sp < 1) return false;
                auto a = stack[--sp];
                stack[sp++] = {(a.val == 0) ? 1ULL : 0ULL, 1};
                break;
            }
            case Opcode::BIT_NOT: {
                if (sp < 1) return false;
                auto a = stack[--sp];
                uint64_t mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                stack[sp++] = {(~a.val) & mask, a.width};
                break;
            }
            case Opcode::NEG: {
                if (sp < 1) return false;
                auto a = stack[--sp];
                uint64_t mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                stack[sp++] = {(~a.val + 1) & mask, a.width};
                break;
            }
            case Opcode::RED_AND: {
                if (sp < 1) return false;
                auto a = stack[--sp];
                uint64_t mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                stack[sp++] = {((a.val & mask) == mask) ? 1ULL : 0ULL, 1};
                break;
            }
            case Opcode::RED_NAND: {
                if (sp < 1) return false;
                auto a = stack[--sp];
                uint64_t mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                stack[sp++] = {((a.val & mask) == mask) ? 0ULL : 1ULL, 1};
                break;
            }
            case Opcode::RED_OR: {
                if (sp < 1) return false;
                auto a = stack[--sp];
                uint64_t mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                stack[sp++] = {((a.val & mask) != 0) ? 1ULL : 0ULL, 1};
                break;
            }
            case Opcode::RED_NOR: {
                if (sp < 1) return false;
                auto a = stack[--sp];
                uint64_t mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                stack[sp++] = {((a.val & mask) != 0) ? 0ULL : 1ULL, 1};
                break;
            }
            case Opcode::RED_XOR: {
                if (sp < 1) return false;
                auto a = stack[--sp];
                uint64_t mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                stack[sp++] = {static_cast<uint64_t>(__builtin_parityll(a.val & mask)), 1};
                break;
            }
            case Opcode::RED_XNOR: {
                if (sp < 1) return false;
                auto a = stack[--sp];
                uint64_t mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                stack[sp++] = {static_cast<uint64_t>(__builtin_parityll(a.val & mask) ^ 1), 1};
                break;
            }
            case Opcode::TERNARY_MUX: {
                if (sp < 3) return false;
                auto f_val = stack[--sp];
                auto t_val = stack[--sp];
                auto cond  = stack[--sp];
                uint32_t w = std::max(t_val.width, f_val.width);
                stack[sp++] = {(cond.val != 0) ? t_val.val : f_val.val, w};
                break;
            }
            case Opcode::EXTRACT_SLICE: {
                if (sp < 1) return false;
                auto v = stack[--sp];
                uint32_t slice_w = inst.arg0;
                uint32_t lsb = inst.arg1;
                uint64_t mask = (slice_w >= 64) ? ~0ULL : ((1ULL << slice_w) - 1);
                uint64_t res = (lsb >= 64) ? 0ULL : ((v.val >> lsb) & mask);
                stack[sp++] = {res, slice_w};
                break;
            }
            case Opcode::CONCAT: {
                uint32_t count = inst.arg0;
                if ((uint32_t)sp < count) return false;
                uint32_t total_w = 0;
                for (uint32_t i = 0; i < count; ++i) {
                    total_w += stack[sp - count + i].width;
                }
                if (total_w > 64) return false;
                uint64_t new_a = 0;
                int shift = (int)total_w;
                for (uint32_t i = 0; i < count; ++i) {
                    const auto& elem = stack[sp - count + i];
                    shift -= elem.width;
                    uint64_t mask = (elem.width >= 64) ? ~0ULL : ((1ULL << elem.width) - 1);
                    if (shift >= 0) {
                        new_a |= ((elem.val & mask) << shift);
                    }
                }
                sp -= count;
                stack[sp++] = {new_a, total_w};
                break;
            }
            case Opcode::DONE: {
                if (sp < 1) return false;
                out_val = stack[--sp].val;
                return true;
            }
        }
    }
    if (sp >= 1) {
        out_val = stack[--sp].val;
        return true;
    }
    return false;
}

LogicVal StratifiedScheduler::execute_bytecode_logicval(const CompiledExpr& expr) {
    LogicVal stack[64];
    int sp = 0;

    for (const auto& inst : expr.instructions) {
        switch (inst.op) {
            case Opcode::PUSH_NET_VAL:
            case Opcode::PUSH_NET_U64: {
                if (inst.arg0 < engine.netlist.size()) {
                    stack[sp++] = engine.netlist[inst.arg0].value;
                } else {
                    stack[sp++] = LogicVal::make_x(inst.arg1 > 0 ? inst.arg1 : 1);
                }
                break;
            }
            case Opcode::PUSH_CONST_VAL:
            case Opcode::PUSH_CONST_U64: {
                stack[sp++] = inst.imm_val;
                break;
            }
            case Opcode::ADD: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                bool expr_signed = (inst.arg1 != 0);
                uint32_t w = std::max(a.width, b.width);
                LogicVal ext_a = (a.width != w) ? LogicOps::cast_width(a, w, expr_signed) : a;
                LogicVal ext_b = (b.width != w) ? LogicOps::cast_width(b, w, expr_signed) : b;
                if (ext_a.is_unknown() || ext_b.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(w);
                } else {
                    stack[sp++] = LogicVal::make_known(w, ext_a.aval + ext_b.aval, expr_signed);
                }
                break;
            }
            case Opcode::SUB: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                bool expr_signed = (inst.arg1 != 0);
                uint32_t w = std::max(a.width, b.width);
                LogicVal ext_a = (a.width != w) ? LogicOps::cast_width(a, w, expr_signed) : a;
                LogicVal ext_b = (b.width != w) ? LogicOps::cast_width(b, w, expr_signed) : b;
                if (ext_a.is_unknown() || ext_b.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(w);
                } else {
                    stack[sp++] = LogicVal::make_known(w, ext_a.aval - ext_b.aval, expr_signed);
                }
                break;
            }
            case Opcode::MUL: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                bool expr_signed = (inst.arg1 != 0);
                uint32_t w = std::min(64U, a.width + b.width);
                if (a.is_unknown() || b.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(w);
                } else {
                    stack[sp++] = LogicVal::make_known(w, a.aval * b.aval, expr_signed);
                }
                break;
            }
            case Opcode::DIV: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                if (a.is_unknown() || b.is_unknown() || b.aval == 0) {
                    stack[sp++] = LogicVal::make_x(a.width);
                } else {
                    stack[sp++] = LogicVal::make_known(a.width, a.aval / b.aval, inst.arg1 != 0);
                }
                break;
            }
            case Opcode::MOD: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                if (a.is_unknown() || b.is_unknown() || b.aval == 0) {
                    stack[sp++] = LogicVal::make_x(a.width);
                } else {
                    stack[sp++] = LogicVal::make_known(a.width, a.aval % b.aval, inst.arg1 != 0);
                }
                break;
            }
            case Opcode::AND: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::bitwise_and(a, b);
                break;
            }
            case Opcode::OR: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::bitwise_or(a, b);
                break;
            }
            case Opcode::XOR: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::bitwise_xor(a, b);
                break;
            }
            case Opcode::XNOR: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::bitwise_not(LogicOps::bitwise_xor(a, b));
                break;
            }
            case Opcode::SHL: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::shift_left(a, b);
                break;
            }
            case Opcode::SHR: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::shift_right(a, b);
                break;
            }
            case Opcode::SRA: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::arith_shift_right(a, b, inst.arg1 != 0);
                break;
            }
            case Opcode::EQ: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                uint32_t w = std::max(a.width, b.width);
                LogicVal ea = (a.width != w) ? LogicOps::cast_width(a, w, false) : a;
                LogicVal eb = (b.width != w) ? LogicOps::cast_width(b, w, false) : b;
                stack[sp++] = LogicOps::logical_eq(ea, eb);
                break;
            }
            case Opcode::NE: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                uint32_t w = std::max(a.width, b.width);
                LogicVal ea = (a.width != w) ? LogicOps::cast_width(a, w, false) : a;
                LogicVal eb = (b.width != w) ? LogicOps::cast_width(b, w, false) : b;
                stack[sp++] = LogicOps::logical_neq(ea, eb);
                break;
            }
            case Opcode::CASE_EQ: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::case_eq(a, b);
                break;
            }
            case Opcode::CASE_NE: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::case_neq(a, b);
                break;
            }
            case Opcode::LT: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                if (a.is_unknown() || b.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(1);
                } else if (inst.arg1 != 0) {
                    stack[sp++] = LogicVal::make_known(1, (a.to_signed() < b.to_signed()) ? 1 : 0);
                } else {
                    stack[sp++] = LogicVal::make_known(1, (a.aval < b.aval) ? 1 : 0);
                }
                break;
            }
            case Opcode::LE: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                if (a.is_unknown() || b.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(1);
                } else if (inst.arg1 != 0) {
                    stack[sp++] = LogicVal::make_known(1, (a.to_signed() <= b.to_signed()) ? 1 : 0);
                } else {
                    stack[sp++] = LogicVal::make_known(1, (a.aval <= b.aval) ? 1 : 0);
                }
                break;
            }
            case Opcode::GT: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                if (a.is_unknown() || b.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(1);
                } else if (inst.arg1 != 0) {
                    stack[sp++] = LogicVal::make_known(1, (a.to_signed() > b.to_signed()) ? 1 : 0);
                } else {
                    stack[sp++] = LogicVal::make_known(1, (a.aval > b.aval) ? 1 : 0);
                }
                break;
            }
            case Opcode::GE: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                if (a.is_unknown() || b.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(1);
                } else if (inst.arg1 != 0) {
                    stack[sp++] = LogicVal::make_known(1, (a.to_signed() >= b.to_signed()) ? 1 : 0);
                } else {
                    stack[sp++] = LogicVal::make_known(1, (a.aval >= b.aval) ? 1 : 0);
                }
                break;
            }
            case Opcode::LOGIC_AND: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                if (!a.is_unknown() && a.aval == 0) {
                    stack[sp++] = LogicVal::make_known(1, 0);
                } else if (!b.is_unknown() && b.aval == 0) {
                    stack[sp++] = LogicVal::make_known(1, 0);
                } else if (a.is_unknown() || b.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(1);
                } else {
                    stack[sp++] = LogicVal::make_known(1, 1);
                }
                break;
            }
            case Opcode::LOGIC_OR: {
                if (sp < 2) return LogicVal::make_x(1);
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                if (!a.is_unknown() && a.aval != 0) {
                    stack[sp++] = LogicVal::make_known(1, 1);
                } else if (!b.is_unknown() && b.aval != 0) {
                    stack[sp++] = LogicVal::make_known(1, 1);
                } else if (a.is_unknown() || b.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(1);
                } else {
                    stack[sp++] = LogicVal::make_known(1, 0);
                }
                break;
            }
            case Opcode::NOT: {
                if (sp < 1) return LogicVal::make_x(1);
                LogicVal a = std::move(stack[--sp]);
                if (a.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(1);
                } else {
                    stack[sp++] = LogicVal::make_known(1, (a.aval == 0) ? 1 : 0);
                }
                break;
            }
            case Opcode::BIT_NOT: {
                if (sp < 1) return LogicVal::make_x(1);
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::bitwise_not(a);
                break;
            }
            case Opcode::NEG: {
                if (sp < 1) return LogicVal::make_x(1);
                LogicVal a = std::move(stack[--sp]);
                if (a.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(a.width);
                } else {
                    uint64_t mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                    uint64_t neg_a = (~a.aval + 1) & mask;
                    stack[sp++] = LogicVal(a.width, neg_a, 0, true);
                }
                break;
            }
            case Opcode::RED_AND: {
                if (sp < 1) return LogicVal::make_x(1);
                stack[sp - 1] = LogicOps::reduce_and(stack[sp - 1]);
                break;
            }
            case Opcode::RED_NAND: {
                if (sp < 1) return LogicVal::make_x(1);
                stack[sp - 1] = LogicOps::reduce_nand(stack[sp - 1]);
                break;
            }
            case Opcode::RED_OR: {
                if (sp < 1) return LogicVal::make_x(1);
                stack[sp - 1] = LogicOps::reduce_or(stack[sp - 1]);
                break;
            }
            case Opcode::RED_NOR: {
                if (sp < 1) return LogicVal::make_x(1);
                stack[sp - 1] = LogicOps::reduce_nor(stack[sp - 1]);
                break;
            }
            case Opcode::RED_XOR: {
                if (sp < 1) return LogicVal::make_x(1);
                stack[sp - 1] = LogicOps::reduce_xor(stack[sp - 1]);
                break;
            }
            case Opcode::RED_XNOR: {
                if (sp < 1) return LogicVal::make_x(1);
                stack[sp - 1] = LogicOps::reduce_xnor(stack[sp - 1]);
                break;
            }
            case Opcode::TERNARY_MUX: {
                if (sp < 3) return LogicVal::make_x(1);
                LogicVal f_val = std::move(stack[--sp]);
                LogicVal t_val = std::move(stack[--sp]);
                LogicVal cond  = std::move(stack[--sp]);
                stack[sp++] = LogicOps::ternary(cond, t_val, f_val);
                break;
            }
            case Opcode::EXTRACT_SLICE: {
                if (sp < 1) return LogicVal::make_x(1);
                LogicVal v = std::move(stack[--sp]);
                uint32_t slice_w = inst.arg0;
                uint32_t lsb = inst.arg1;
                stack[sp++] = v.extract_slice(lsb, slice_w);
                break;
            }
            case Opcode::CONCAT: {
                uint32_t count = inst.arg0;
                if ((uint32_t)sp < count) return LogicVal::make_x(1);
                uint32_t total_w = 0;
                for (uint32_t i = 0; i < count; ++i) {
                    total_w += stack[sp - count + i].width;
                }
                if (total_w <= 64) {
                    uint64_t new_a = 0;
                    uint64_t new_b = 0;
                    int shift = (int)total_w;
                    for (uint32_t i = 0; i < count; ++i) {
                        const auto& v = stack[sp - count + i];
                        shift -= v.width;
                        uint64_t mask = (v.width >= 64) ? ~0ULL : ((1ULL << v.width) - 1);
                        if (shift >= 0) {
                            new_a |= ((v.aval & mask) << shift);
                            new_b |= ((v.bval & mask) << shift);
                        }
                    }
                    sp -= count;
                    stack[sp++] = LogicVal(total_w, new_a, new_b);
                } else {
                    LogicVal res(total_w, 0, 0);
                    uint32_t cur_offset = 0;
                    for (int i = (int)count - 1; i >= 0; --i) {
                        const auto& v = stack[sp - count + i];
                        res.deposit_slice(cur_offset, v.width, v);
                        cur_offset += v.width;
                    }
                    sp -= count;
                    stack[sp++] = std::move(res);
                }
                break;
            }
            case Opcode::DONE: {
                if (sp < 1) return LogicVal::make_x(1);
                LogicVal res = stack[--sp];
                if (expr.result_signed) {
                    res.is_signed = true;
                }
                return res;
            }
        }
    }
    if (sp >= 1) {
        LogicVal res = stack[--sp];
        if (expr.result_signed) {
            res.is_signed = true;
        }
        return res;
    }
    return LogicVal::make_x(1);
}

bool StratifiedScheduler::execute_process_bytecode_2state_64(BoundProcess& bp) {
    const auto& instructions = bp.compiled_body.instructions;
    U64StackItem stack[64];
    int sp = 0;

    struct UndoEntry {
        uint32_t net_id;
        LogicVal old_value;
    };
    UndoEntry undo_log[32];
    int num_undos = 0;

    size_t pc = 0;
    while (pc < instructions.size()) {
        const auto& inst = instructions[pc];
        switch (inst.op) {
            case Opcode::PUSH_NET_VAL:
            case Opcode::PUSH_NET_U64: {
                if (inst.arg0 >= engine.netlist.size() || sp >= 64) goto fail;
                const auto& net = engine.netlist[inst.arg0];
                if (__builtin_expect(net.value.bval != 0, 0)) goto fail;
                stack[sp++] = {net.value.aval, net.width};
                pc++;
                break;
            }
            case Opcode::PUSH_CONST_VAL:
            case Opcode::PUSH_CONST_U64: {
                if (sp >= 64) goto fail;
                stack[sp++] = {inst.imm64, inst.arg0};
                pc++;
                break;
            }
            case Opcode::ADD: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                uint32_t w = std::min(64U, std::max(a.width, b.width) + 1);
                stack[sp++] = {a.val + b.val, w};
                pc++;
                break;
            }
            case Opcode::SUB: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                uint32_t w = std::min(64U, std::max(a.width, b.width) + 1);
                stack[sp++] = {a.val - b.val, w};
                pc++;
                break;
            }
            case Opcode::MUL: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                uint32_t w = std::min(64U, a.width + b.width);
                stack[sp++] = {a.val * b.val, w};
                pc++;
                break;
            }
            case Opcode::DIV: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                if (b.val == 0) goto fail;
                stack[sp++] = {a.val / b.val, a.width};
                pc++;
                break;
            }
            case Opcode::MOD: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                if (b.val == 0) goto fail;
                stack[sp++] = {a.val % b.val, a.width};
                pc++;
                break;
            }
            case Opcode::AND: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                uint32_t w = std::max(a.width, b.width);
                stack[sp++] = {a.val & b.val, w};
                pc++;
                break;
            }
            case Opcode::OR: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                uint32_t w = std::max(a.width, b.width);
                stack[sp++] = {a.val | b.val, w};
                pc++;
                break;
            }
            case Opcode::XOR: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                uint32_t w = std::max(a.width, b.width);
                stack[sp++] = {a.val ^ b.val, w};
                pc++;
                break;
            }
            case Opcode::XNOR: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                uint32_t w = std::max(a.width, b.width);
                uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
                stack[sp++] = {(~(a.val ^ b.val)) & mask, w};
                pc++;
                break;
            }
            case Opcode::SHL: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                uint64_t res = (b.val >= a.width) ? 0ULL : (a.val << b.val);
                stack[sp++] = {res, a.width};
                pc++;
                break;
            }
            case Opcode::SHR: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                uint64_t res = (b.val >= a.width) ? 0ULL : (a.val >> b.val);
                stack[sp++] = {res, a.width};
                pc++;
                break;
            }
            case Opcode::SRA: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                bool is_s = (inst.arg1 != 0);
                if (!is_s) {
                    uint64_t res = (b.val >= a.width) ? 0ULL : (a.val >> b.val);
                    stack[sp++] = {res, a.width};
                } else {
                    if (a.width == 0) { stack[sp++] = {0, 0}; pc++; break; }
                    bool sign_bit = (a.val >> (a.width - 1)) & 1ULL;
                    if (b.val >= a.width) {
                        uint64_t res = sign_bit ? ((a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1)) : 0ULL;
                        stack[sp++] = {res, a.width};
                    } else {
                        uint64_t res = a.val >> b.val;
                        if (sign_bit) {
                            uint64_t fill_mask = (~0ULL) << (a.width - b.val);
                            uint64_t width_mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                            res |= (fill_mask & width_mask);
                        }
                        stack[sp++] = {res, a.width};
                    }
                }
                pc++;
                break;
            }
            case Opcode::EQ:
            case Opcode::CASE_EQ: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                stack[sp++] = {(a.val == b.val) ? 1ULL : 0ULL, 1};
                pc++;
                break;
            }
            case Opcode::NE:
            case Opcode::CASE_NE: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                stack[sp++] = {(a.val != b.val) ? 1ULL : 0ULL, 1};
                pc++;
                break;
            }
            case Opcode::LT: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                bool is_s = (inst.arg1 != 0);
                uint64_t res = is_s ? (static_cast<int64_t>(a.val) < static_cast<int64_t>(b.val) ? 1ULL : 0ULL)
                                    : (a.val < b.val ? 1ULL : 0ULL);
                stack[sp++] = {res, 1};
                pc++;
                break;
            }
            case Opcode::LE: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                bool is_s = (inst.arg1 != 0);
                uint64_t res = is_s ? (static_cast<int64_t>(a.val) <= static_cast<int64_t>(b.val) ? 1ULL : 0ULL)
                                    : (a.val <= b.val ? 1ULL : 0ULL);
                stack[sp++] = {res, 1};
                pc++;
                break;
            }
            case Opcode::GT: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                bool is_s = (inst.arg1 != 0);
                uint64_t res = is_s ? (static_cast<int64_t>(a.val) > static_cast<int64_t>(b.val) ? 1ULL : 0ULL)
                                    : (a.val > b.val ? 1ULL : 0ULL);
                stack[sp++] = {res, 1};
                pc++;
                break;
            }
            case Opcode::GE: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                bool is_s = (inst.arg1 != 0);
                uint64_t res = is_s ? (static_cast<int64_t>(a.val) >= static_cast<int64_t>(b.val) ? 1ULL : 0ULL)
                                    : (a.val >= b.val ? 1ULL : 0ULL);
                stack[sp++] = {res, 1};
                pc++;
                break;
            }
            case Opcode::LOGIC_AND: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                stack[sp++] = {(a.val != 0 && b.val != 0) ? 1ULL : 0ULL, 1};
                pc++;
                break;
            }
            case Opcode::LOGIC_OR: {
                if (sp < 2) goto fail;
                auto b = stack[--sp];
                auto a = stack[--sp];
                stack[sp++] = {(a.val != 0 || b.val != 0) ? 1ULL : 0ULL, 1};
                pc++;
                break;
            }
            case Opcode::NOT: {
                if (sp < 1) goto fail;
                auto a = stack[--sp];
                stack[sp++] = {(a.val == 0) ? 1ULL : 0ULL, 1};
                pc++;
                break;
            }
            case Opcode::BIT_NOT: {
                if (sp < 1) goto fail;
                auto a = stack[--sp];
                uint64_t mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                stack[sp++] = {(~a.val) & mask, a.width};
                pc++;
                break;
            }
            case Opcode::NEG: {
                if (sp < 1) goto fail;
                auto a = stack[--sp];
                uint64_t mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                stack[sp++] = {((~a.val + 1)) & mask, a.width};
                pc++;
                break;
            }
            case Opcode::RED_AND: {
                if (sp < 1) goto fail;
                auto a = stack[--sp];
                uint64_t mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                stack[sp++] = {((a.val & mask) == mask && a.width > 0) ? 1ULL : 0ULL, 1};
                pc++;
                break;
            }
            case Opcode::RED_NAND: {
                if (sp < 1) goto fail;
                auto a = stack[--sp];
                uint64_t mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                stack[sp++] = {((a.val & mask) == mask && a.width > 0) ? 0ULL : 1ULL, 1};
                pc++;
                break;
            }
            case Opcode::RED_OR: {
                if (sp < 1) goto fail;
                auto a = stack[--sp];
                uint64_t mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                stack[sp++] = {((a.val & mask) != 0) ? 1ULL : 0ULL, 1};
                pc++;
                break;
            }
            case Opcode::RED_NOR: {
                if (sp < 1) goto fail;
                auto a = stack[--sp];
                uint64_t mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                stack[sp++] = {((a.val & mask) == 0) ? 1ULL : 0ULL, 1};
                pc++;
                break;
            }
            case Opcode::RED_XOR: {
                if (sp < 1) goto fail;
                auto a = stack[--sp];
                uint64_t mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                uint64_t v = a.val & mask;
                stack[sp++] = {static_cast<uint64_t>(__builtin_parityll(v)), 1};
                pc++;
                break;
            }
            case Opcode::RED_XNOR: {
                if (sp < 1) goto fail;
                auto a = stack[--sp];
                uint64_t mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                uint64_t v = a.val & mask;
                stack[sp++] = {static_cast<uint64_t>(__builtin_parityll(v) ^ 1), 1};
                pc++;
                break;
            }
            case Opcode::TERNARY_MUX: {
                if (sp < 3) goto fail;
                auto f = stack[--sp];
                auto t = stack[--sp];
                auto c = stack[--sp];
                stack[sp++] = (c.val != 0) ? t : f;
                pc++;
                break;
            }
            case Opcode::CONCAT: {
                uint32_t count = inst.arg0;
                uint32_t tot_w = inst.arg1;
                if ((uint32_t)sp < count || tot_w > 64) goto fail;
                uint64_t res = 0;
                int start_idx = sp - (int)count;
                for (int i = 0; i < (int)count; ++i) {
                    const auto& elem = stack[start_idx + i];
                    uint64_t m = (elem.width >= 64) ? ~0ULL : ((1ULL << elem.width) - 1);
                    res = (res << elem.width) | (elem.val & m);
                }
                sp = start_idx;
                stack[sp++] = {res, tot_w};
                pc++;
                break;
            }
            case Opcode::STORE_NET_U64: {
                if (sp < 1) goto fail;
                auto val_item = stack[--sp];
                uint32_t target_id = inst.arg0;
                if (target_id >= engine.netlist.size()) goto fail;
                auto& target_net = engine.netlist[target_id];
                if (!target_net.has_procedural_assign) {
                    uint64_t mask = (target_net.width >= 64) ? ~0ULL : ((1ULL << target_net.width) - 1);
                    uint64_t new_aval = val_item.val & mask;
                    if (target_net.is_signed && val_item.width < target_net.width && val_item.width > 0) {
                        bool sign_bit = (val_item.val >> (val_item.width - 1)) & 1ULL;
                        if (sign_bit) {
                            uint64_t sign_ext = (~0ULL) << val_item.width;
                            new_aval = (val_item.val | sign_ext) & mask;
                        }
                    }
                    bool already_recorded = false;
                    for (int i = 0; i < num_undos; ++i) {
                        if (undo_log[i].net_id == target_id) {
                            already_recorded = true;
                            break;
                        }
                    }
                    if (!already_recorded) {
                        if (num_undos >= 32) goto fail;
                        undo_log[num_undos++] = {target_id, target_net.value};
                    }
                    target_net.value.aval = new_aval;
                    target_net.value.bval = 0;
                }
                pc++;
                break;
            }
            case Opcode::STORE_NET_VAL: {
                goto fail;
            }
            case Opcode::JUMP: {
                int32_t offset = (int32_t)inst.arg0;
                pc = (size_t)((int64_t)pc + offset);
                break;
            }
            case Opcode::JUMP_IF_ZERO: {
                if (sp < 1) goto fail;
                auto cond = stack[--sp];
                if (cond.val == 0) {
                    int32_t offset = (int32_t)inst.arg0;
                    pc = (size_t)((int64_t)pc + offset);
                } else {
                    pc++;
                }
                break;
            }
            case Opcode::DONE: {
                pc = instructions.size();
                break;
            }
            default:
                goto fail;
        }
    }

    // Success! Commit all stores
    for (int i = 0; i < num_undos; ++i) {
        uint32_t tid = undo_log[i].net_id;
        auto& target_net = engine.netlist[tid];
        if (target_net.value.aval != undo_log[i].old_value.aval ||
            target_net.value.bval != undo_log[i].old_value.bval) {
            LogicVal final_val = target_net.value;
            target_net.value = undo_log[i].old_value;
            target_net.set(final_val);
            if (tid < engine.alias_members.size()) {
                for (size_t other_id : engine.alias_members[tid]) {
                    if (other_id != tid && other_id < engine.netlist.size()) {
                        engine.netlist[other_id].set(final_val);
                    }
                }
            }
            notify_net_changed(tid);
        }
    }
    return true;

fail:
    for (int i = 0; i < num_undos; ++i) {
        engine.netlist[undo_log[i].net_id].value = undo_log[i].old_value;
    }
    return false;
}

void StratifiedScheduler::execute_process_bytecode_logicval(BoundProcess& bp) {
    const auto& instructions = bp.compiled_body.instructions;
    LogicVal stack[128];
    int sp = 0;

    size_t pc = 0;
    while (pc < instructions.size()) {
        const auto& inst = instructions[pc];
        switch (inst.op) {
            case Opcode::PUSH_NET_VAL:
            case Opcode::PUSH_NET_U64: {
                if (inst.arg0 >= engine.netlist.size() || sp >= 128) return;
                stack[sp++] = engine.netlist[inst.arg0].value;
                pc++;
                break;
            }
            case Opcode::PUSH_CONST_VAL:
            case Opcode::PUSH_CONST_U64: {
                if (sp >= 128) return;
                stack[sp++] = inst.imm_val;
                pc++;
                break;
            }
            case Opcode::ADD: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                bool expr_signed = (inst.arg1 != 0);
                uint32_t w = std::max(a.width, b.width);
                LogicVal ext_a = (a.width != w) ? LogicOps::cast_width(a, w, expr_signed) : a;
                LogicVal ext_b = (b.width != w) ? LogicOps::cast_width(b, w, expr_signed) : b;
                if (ext_a.is_unknown() || ext_b.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(w);
                } else {
                    stack[sp++] = LogicVal::make_known(w, ext_a.aval + ext_b.aval, expr_signed);
                }
                pc++;
                break;
            }
            case Opcode::SUB: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                bool expr_signed = (inst.arg1 != 0);
                uint32_t w = std::max(a.width, b.width);
                LogicVal ext_a = (a.width != w) ? LogicOps::cast_width(a, w, expr_signed) : a;
                LogicVal ext_b = (b.width != w) ? LogicOps::cast_width(b, w, expr_signed) : b;
                if (ext_a.is_unknown() || ext_b.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(w);
                } else {
                    stack[sp++] = LogicVal::make_known(w, ext_a.aval - ext_b.aval, expr_signed);
                }
                pc++;
                break;
            }
            case Opcode::MUL: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                bool expr_signed = (inst.arg1 != 0);
                uint32_t w = std::min(64U, a.width + b.width);
                if (a.is_unknown() || b.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(w);
                } else {
                    stack[sp++] = LogicVal::make_known(w, a.aval * b.aval, expr_signed);
                }
                pc++;
                break;
            }
            case Opcode::DIV: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                if (a.is_unknown() || b.is_unknown() || b.aval == 0) {
                    stack[sp++] = LogicVal::make_x(a.width);
                } else {
                    stack[sp++] = LogicVal::make_known(a.width, a.aval / b.aval, inst.arg1 != 0);
                }
                pc++;
                break;
            }
            case Opcode::MOD: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                if (a.is_unknown() || b.is_unknown() || b.aval == 0) {
                    stack[sp++] = LogicVal::make_x(a.width);
                } else {
                    stack[sp++] = LogicVal::make_known(a.width, a.aval % b.aval, inst.arg1 != 0);
                }
                pc++;
                break;
            }
            case Opcode::AND: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::bitwise_and(a, b);
                pc++;
                break;
            }
            case Opcode::OR: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::bitwise_or(a, b);
                pc++;
                break;
            }
            case Opcode::XOR: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::bitwise_xor(a, b);
                pc++;
                break;
            }
            case Opcode::XNOR: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::bitwise_not(LogicOps::bitwise_xor(a, b));
                pc++;
                break;
            }
            case Opcode::SHL: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::shift_left(a, b);
                pc++;
                break;
            }
            case Opcode::SHR: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::shift_right(a, b);
                pc++;
                break;
            }
            case Opcode::SRA: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::arith_shift_right(a, b, inst.arg1 != 0);
                pc++;
                break;
            }
            case Opcode::EQ: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                uint32_t w = std::max(a.width, b.width);
                LogicVal ea = (a.width != w) ? LogicOps::cast_width(a, w, false) : a;
                LogicVal eb = (b.width != w) ? LogicOps::cast_width(b, w, false) : b;
                stack[sp++] = LogicOps::logical_eq(ea, eb);
                pc++;
                break;
            }
            case Opcode::NE: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                uint32_t w = std::max(a.width, b.width);
                LogicVal ea = (a.width != w) ? LogicOps::cast_width(a, w, false) : a;
                LogicVal eb = (b.width != w) ? LogicOps::cast_width(b, w, false) : b;
                stack[sp++] = LogicOps::logical_neq(ea, eb);
                pc++;
                break;
            }
            case Opcode::CASE_EQ: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::case_eq(a, b);
                pc++;
                break;
            }
            case Opcode::CASE_NE: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::case_neq(a, b);
                pc++;
                break;
            }
            case Opcode::LT: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                if (a.is_unknown() || b.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(1);
                } else if (inst.arg1 != 0) {
                    stack[sp++] = LogicVal::make_known(1, (a.to_signed() < b.to_signed()) ? 1 : 0);
                } else {
                    stack[sp++] = LogicVal::make_known(1, (a.aval < b.aval) ? 1 : 0);
                }
                pc++;
                break;
            }
            case Opcode::LE: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                if (a.is_unknown() || b.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(1);
                } else if (inst.arg1 != 0) {
                    stack[sp++] = LogicVal::make_known(1, (a.to_signed() <= b.to_signed()) ? 1 : 0);
                } else {
                    stack[sp++] = LogicVal::make_known(1, (a.aval <= b.aval) ? 1 : 0);
                }
                pc++;
                break;
            }
            case Opcode::GT: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                if (a.is_unknown() || b.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(1);
                } else if (inst.arg1 != 0) {
                    stack[sp++] = LogicVal::make_known(1, (a.to_signed() > b.to_signed()) ? 1 : 0);
                } else {
                    stack[sp++] = LogicVal::make_known(1, (a.aval > b.aval) ? 1 : 0);
                }
                pc++;
                break;
            }
            case Opcode::GE: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                if (a.is_unknown() || b.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(1);
                } else if (inst.arg1 != 0) {
                    stack[sp++] = LogicVal::make_known(1, (a.to_signed() >= b.to_signed()) ? 1 : 0);
                } else {
                    stack[sp++] = LogicVal::make_known(1, (a.aval >= b.aval) ? 1 : 0);
                }
                pc++;
                break;
            }
            case Opcode::LOGIC_AND: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                if (!a.is_unknown() && a.aval == 0) {
                    stack[sp++] = LogicVal::make_known(1, 0);
                } else if (!b.is_unknown() && b.aval == 0) {
                    stack[sp++] = LogicVal::make_known(1, 0);
                } else if (a.is_unknown() || b.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(1);
                } else {
                    stack[sp++] = LogicVal::make_known(1, 1);
                }
                pc++;
                break;
            }
            case Opcode::LOGIC_OR: {
                if (sp < 2) return;
                LogicVal b = std::move(stack[--sp]);
                LogicVal a = std::move(stack[--sp]);
                if (!a.is_unknown() && a.aval != 0) {
                    stack[sp++] = LogicVal::make_known(1, 1);
                } else if (!b.is_unknown() && b.aval != 0) {
                    stack[sp++] = LogicVal::make_known(1, 1);
                } else if (a.is_unknown() || b.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(1);
                } else {
                    stack[sp++] = LogicVal::make_known(1, 0);
                }
                pc++;
                break;
            }
            case Opcode::NOT: {
                if (sp < 1) return;
                LogicVal a = std::move(stack[--sp]);
                if (a.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(1);
                } else {
                    stack[sp++] = LogicVal::make_known(1, (a.aval == 0) ? 1 : 0);
                }
                pc++;
                break;
            }
            case Opcode::BIT_NOT: {
                if (sp < 1) return;
                LogicVal a = std::move(stack[--sp]);
                stack[sp++] = LogicOps::bitwise_not(a);
                pc++;
                break;
            }
            case Opcode::NEG: {
                if (sp < 1) return;
                LogicVal a = std::move(stack[--sp]);
                if (a.is_unknown()) {
                    stack[sp++] = LogicVal::make_x(a.width);
                } else {
                    uint64_t mask = (a.width >= 64) ? ~0ULL : ((1ULL << a.width) - 1);
                    uint64_t neg_a = (~a.aval + 1) & mask;
                    stack[sp++] = LogicVal(a.width, neg_a, 0, true);
                }
                pc++;
                break;
            }
            case Opcode::RED_AND: {
                if (sp < 1) return;
                stack[sp - 1] = LogicOps::reduce_and(stack[sp - 1]);
                pc++;
                break;
            }
            case Opcode::RED_NAND: {
                if (sp < 1) return;
                stack[sp - 1] = LogicOps::reduce_nand(stack[sp - 1]);
                pc++;
                break;
            }
            case Opcode::RED_OR: {
                if (sp < 1) return;
                stack[sp - 1] = LogicOps::reduce_or(stack[sp - 1]);
                pc++;
                break;
            }
            case Opcode::RED_NOR: {
                if (sp < 1) return;
                stack[sp - 1] = LogicOps::reduce_nor(stack[sp - 1]);
                pc++;
                break;
            }
            case Opcode::RED_XOR: {
                if (sp < 1) return;
                stack[sp - 1] = LogicOps::reduce_xor(stack[sp - 1]);
                pc++;
                break;
            }
            case Opcode::RED_XNOR: {
                if (sp < 1) return;
                stack[sp - 1] = LogicOps::reduce_xnor(stack[sp - 1]);
                pc++;
                break;
            }
            case Opcode::TERNARY_MUX: {
                if (sp < 3) return;
                LogicVal f_val = std::move(stack[--sp]);
                LogicVal t_val = std::move(stack[--sp]);
                LogicVal cond  = std::move(stack[--sp]);
                stack[sp++] = LogicOps::ternary(cond, t_val, f_val);
                pc++;
                break;
            }
            case Opcode::EXTRACT_SLICE: {
                if (sp < 1) return;
                LogicVal v = std::move(stack[--sp]);
                uint32_t slice_w = inst.arg0;
                uint32_t lsb = inst.arg1;
                stack[sp++] = v.extract_slice(lsb, slice_w);
                pc++;
                break;
            }
            case Opcode::CONCAT: {
                uint32_t count = inst.arg0;
                if ((uint32_t)sp < count) return;
                uint32_t total_w = 0;
                for (uint32_t i = 0; i < count; ++i) {
                    total_w += stack[sp - count + i].width;
                }
                if (total_w <= 64) {
                    uint64_t new_a = 0;
                    uint64_t new_b = 0;
                    int shift = (int)total_w;
                    for (uint32_t i = 0; i < count; ++i) {
                        const auto& v = stack[sp - count + i];
                        shift -= v.width;
                        uint64_t mask = (v.width >= 64) ? ~0ULL : ((1ULL << v.width) - 1);
                        if (shift >= 0) {
                            new_a |= ((v.aval & mask) << shift);
                            new_b |= ((v.bval & mask) << shift);
                        }
                    }
                    sp -= count;
                    stack[sp++] = LogicVal(total_w, new_a, new_b);
                } else {
                    LogicVal res(total_w, 0, 0);
                    uint32_t cur_offset = 0;
                    for (int i = (int)count - 1; i >= 0; --i) {
                        const auto& v = stack[sp - count + i];
                        res.deposit_slice(cur_offset, v.width, v);
                        cur_offset += v.width;
                    }
                    sp -= count;
                    stack[sp++] = std::move(res);
                }
                pc++;
                break;
            }
            case Opcode::STORE_NET_U64:
            case Opcode::STORE_NET_VAL: {
                if (sp < 1) return;
                LogicVal rhs_val = std::move(stack[--sp]);
                uint32_t target_id = inst.arg0;
                if (target_id < engine.netlist.size()) {
                    auto& target_net = engine.netlist[target_id];
                    if (!target_net.has_procedural_assign) {
                        LogicVal old_v = target_net.value;
                        LogicVal casted = (target_net.type_name == "string" || rhs_val.is_string)
                            ? rhs_val
                            : (target_net.width == rhs_val.width && target_net.is_signed == rhs_val.is_signed)
                                ? rhs_val
                                : LogicOps::cast_width(rhs_val, target_net.width, target_net.is_signed);
                        target_net.set(casted);
                        if (target_id < engine.alias_members.size()) {
                            for (size_t other_id : engine.alias_members[target_id]) {
                                if (other_id != target_id && other_id < engine.netlist.size()) {
                                    engine.netlist[other_id].set(casted);
                                }
                            }
                        }
                        if (target_net.value != old_v) {
                            notify_net_changed(target_id);
                        }
                    }
                }
                pc++;
                break;
            }
            case Opcode::JUMP: {
                int32_t offset = (int32_t)inst.arg0;
                pc = (size_t)((int64_t)pc + offset);
                break;
            }
            case Opcode::JUMP_IF_ZERO: {
                if (sp < 1) return;
                LogicVal cond = std::move(stack[--sp]);
                if (!cond.to_bool()) {
                    int32_t offset = (int32_t)inst.arg0;
                    pc = (size_t)((int64_t)pc + offset);
                } else {
                    pc++;
                }
                break;
            }
            case Opcode::DONE: {
                return;
            }
            default:
                pc++;
                break;
        }
    }
}

void StratifiedScheduler::execute_comb_proc_bytecode(BoundProcess& bp) {
    if (bp.compiled_body.is_pure_2state_64) {
        if (execute_process_bytecode_2state_64(bp)) {
            return;
        }
    }
    execute_process_bytecode_logicval(bp);
}

void StratifiedScheduler::propagate_comb() {
    if (!comb_dirty && dirty_rcas.empty() && dirty_comb_procs.empty()) return;
    delta_cycles_count++;
    size_t iters = 0;
    bool changed = true;
    const size_t max_delta_cycles = 100;

    while (changed && iters < max_delta_cycles) {
        changed = false;
        iters++;

        // 0. Monolithic Topological RCA Execution (Acyclic JIT Fast Path)
        auto topo_fn = (llvm_dispatch.is_active && llvm_dispatch.topological_comb_fn != nullptr)
                       ? llvm_dispatch.topological_comb_fn
                       : (native_dispatch.is_active ? native_dispatch.topological_comb_fn : nullptr);
        if (topo_fn != nullptr) {
            auto t_topo_0 = std::chrono::high_resolution_clock::now();
            topo_fn(native_nets_u64.data());
            auto t_topo_1 = std::chrono::high_resolution_clock::now();
            t_rca_ms += std::chrono::duration<double, std::milli>(t_topo_1 - t_topo_0).count();

            for (const auto& rec : topological_target_records) {
                uint64_t val = native_nets_u64[rec.net_id];
                if (val != engine.netlist[rec.net_id].value.aval || engine.netlist[rec.net_id].value.bval != 0) {
                    LogicVal lval = LogicVal::from_u64_2state(val, rec.width, rec.is_signed);
                    engine.netlist[rec.net_id].set(lval);
                    if (rec.net_id < engine.alias_members.size()) {
                        for (size_t other_id : engine.alias_members[rec.net_id]) {
                            if (other_id != rec.net_id && other_id < engine.netlist.size()) {
                                engine.netlist[other_id].set(lval);
                                native_nets_u64[other_id] = val;
                            }
                        }
                    }
                    notify_net_changed(rec.canon_id);
                }
            }
        }

        if (memory_fastpath.bus.is_valid) {
            memory_fastpath.post_comb_sync(engine, native_nets_u64.data());
        }

        auto t0 = std::chrono::high_resolution_clock::now();
        for (auto& bind : engine.slice_bindings) {
            if (engine.netlist[bind.dst.net_id].has_procedural_assign) continue;
            LogicVal src_val = engine.evaluator.read_slice(bind.src);
            if (src_val.width != bind.dst.width && !src_val.is_string) {
                src_val = LogicOps::cast_width(src_val, bind.dst.width, engine.netlist[bind.dst.net_id].is_signed);
            }
            LogicVal dst_val = engine.evaluator.read_slice(bind.dst);
            if (src_val != dst_val) {
                engine.evaluator.write_slice(bind.dst, src_val);
                notify_net_changed(bind.dst.net_id);
                changed = true;
            }
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        t_slice_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();

        for (auto& mbind : engine.memory_bindings) {
            auto it_src = engine.memory_map.find(mbind.src);
            auto it_dst = engine.memory_map.find(mbind.dst);
            if (it_src != engine.memory_map.end() && it_dst != engine.memory_map.end()) {
                for (size_t i = 0; i < it_src->second.words.size() && i < it_dst->second.words.size(); ++i) {
                    if (it_dst->second.words[i] != it_src->second.words[i]) {
                        it_dst->second.words[i] = it_src->second.words[i];
                        notify_mem_changed();
                        changed = true;
                    }
                }
            }
        }

        // Continuous assignments targeting memory array words (IEEE 1800-2023 Â§27)
        for (auto& mca : engine.memory_continuous_assigns) {
            std::string cur_s = mca.scope;
            auto it = engine.memory_map.end();
            while (true) {
                std::string scoped = cur_s.empty() ? mca.mem_name : (cur_s + "." + mca.mem_name);
                it = engine.memory_map.find(scoped);
                if (it != engine.memory_map.end()) break;
                if (cur_s.empty()) break;
                auto dot = cur_s.rfind('.');
                if (dot == std::string::npos) cur_s = "";
                else cur_s = cur_s.substr(0, dot);
            }
            if (it == engine.memory_map.end()) {
                it = engine.memory_map.find(mca.mem_name);
            }
            if (it != engine.memory_map.end()) {
                LogicVal idx_val;
                if (auto idx_node = dynamic_cast<IndexSelectNode*>(mca.index_expr.get())) {
                    idx_val = engine.evaluator.eval_expr(idx_node->msb_expr.get(), mca.scope);
                } else {
                    idx_val = engine.evaluator.eval_expr(mca.index_expr.get(), mca.scope);
                }
                if (!idx_val.is_unknown()) {
                    int64_t addr = (int64_t)idx_val.aval;
                    LogicVal rhs_val = engine.evaluator.eval_expr(mca.rhs.get(), mca.scope);
                    LogicVal cur_val = it->second.get_word(addr);
                    if (cur_val != rhs_val) {
                        it->second.set_word(addr, rhs_val);
                        notify_mem_changed();
                        changed = true;
                    }
                }
            }
        }

        auto t2 = std::chrono::high_resolution_clock::now();
        // 1. Drain and evaluate all dirty_rcas
        if (!dirty_rcas.empty()) {
            changed = true;
            size_t rca_inner_guard = 0;
            while (!dirty_rcas.empty() && rca_inner_guard < 100) {
                rca_inner_guard++;
                std::vector<uint32_t> current_batch;
                current_batch.swap(dirty_rcas);
                for (uint32_t idx : current_batch) {
                    if (idx < rca_enqueued.size()) rca_enqueued[idx] = 0;
                }

                for (uint32_t rca_idx : current_batch) {
                    if (rca_idx >= engine.resolved_continuous_assigns.size()) continue;
                    auto& rca = engine.resolved_continuous_assigns[rca_idx];
                    if (engine.netlist[rca.target.net_id].has_procedural_assign) continue;

                    // Native JIT Fast Path
                    if (native_dispatch.is_active && rca_idx < native_dispatch.rca_count && native_dispatch.rca_table[rca_idx] != nullptr) {
                        size_t canon_id = (rca.target.net_id < engine.canonical_net_map.size()) ? engine.canonical_net_map[rca.target.net_id] : rca.target.net_id;
                        bool has_mpd = false;
                        if (!engine.module_path_delays.empty()) {
                            for (const auto& mpd : engine.module_path_delays) {
                                if (mpd.dst_net_id == canon_id) { has_mpd = true; break; }
                            }
                        }
                        if (!has_mpd) {
                            uint32_t target_net_id = rca.target.net_id;
                            uint64_t old_u64 = native_nets_u64[target_net_id];
                            native_dispatch.rca_table[rca_idx](native_nets_u64.data());
                            uint64_t new_u64 = native_nets_u64[target_net_id];
                            if (new_u64 != old_u64 || engine.netlist[target_net_id].value.bval != 0) {
                                LogicVal lval = LogicVal::from_u64_2state(
                                    new_u64, engine.netlist[target_net_id].width, engine.netlist[target_net_id].is_signed);
                                engine.netlist[target_net_id].set(lval);
                                if (target_net_id < engine.alias_members.size()) {
                                    for (size_t other_id : engine.alias_members[target_net_id]) {
                                        if (other_id != target_net_id && other_id < engine.netlist.size()) {
                                            engine.netlist[other_id].set(lval);
                                            native_nets_u64[other_id] = new_u64;
                                        }
                                    }
                                }
                                notify_net_changed(canon_id);
                            }
                            continue;
                        }
                    }

                    auto eval_driver = [&](const ContinuousAssignDriver& drv) -> LogicVal {
                        if (drv.is_gate) {
                            return engine.evaluator.eval_gate(engine.gates[drv.gate_idx]);
                        } else {
                            if (auto ucat = dynamic_cast<UnpackedConcatNode*>(drv.rhs)) {
                                std::string tname = engine.netlist[rca.target.net_id].type_name;
                                if (tname.empty()) {
                                    std::string nname = engine.netlist[rca.target.net_id].name;
                                    if (engine.var_to_struct_type.count(nname)) tname = engine.var_to_struct_type.at(nname);
                                }
                                if (!tname.empty()) {
                                    if (const PackedStructType* st = engine.evaluator.get_expr_eval().find_struct_type(tname, drv.scope)) {
                                        return engine.evaluator.get_expr_eval().eval_struct_pattern(ucat, *st, drv.scope);
                                    }
                                }
                            }
                            return engine.evaluator.eval_expr(drv.rhs, drv.scope);
                        }
                    };

                    LogicVal resolved;
                    bool computed = false;

                    if (rca.compiled_expr.is_compiled) {
                        if (rca.compiled_expr.is_pure_2state_64 && rca.target.width <= 64) {
                            uint64_t res_u64 = 0;
                            if (execute_bytecode_2state_64(rca.compiled_expr, res_u64)) {
                                if (rca.compiled_expr.result_signed && rca.compiled_expr.result_width > 0 && rca.compiled_expr.result_width < rca.target.width) {
                                    if ((res_u64 >> (rca.compiled_expr.result_width - 1)) & 1ULL) {
                                        uint64_t sign_mask = (~0ULL) << rca.compiled_expr.result_width;
                                        res_u64 |= sign_mask;
                                    }
                                }
                                uint64_t mask = (rca.target.width >= 64) ? ~0ULL : ((1ULL << rca.target.width) - 1);
                                res_u64 &= mask;
                                const auto& cur_net = engine.netlist[rca.target.net_id];
                                if (rca.target.offset == 0 && rca.target.width == cur_net.width) {
                                    if (cur_net.value.is_clean_2state_64() && cur_net.value.aval == res_u64) {
                                        continue;
                                    }
                                }
                                resolved = LogicVal::from_u64_2state(res_u64, rca.target.width, engine.netlist[rca.target.net_id].is_signed);
                                computed = true;
                            }
                        }
                        if (!computed) {
                            resolved = execute_bytecode_logicval(rca.compiled_expr);
                            if (resolved.width != rca.target.width || resolved.is_signed != engine.netlist[rca.target.net_id].is_signed) {
                                resolved = LogicOps::cast_width(resolved, rca.target.width, engine.netlist[rca.target.net_id].is_signed);
                            }
                            computed = true;
                        }
                    }

                    if (!computed) {
                        if (rca.drivers.empty()) {
                            resolved = LogicVal::make_z(rca.target.width);
                        } else if (rca.drivers.size() == 1) {
                            resolved = eval_driver(rca.drivers[0]);
                            resolved = LogicOps::cast_width(resolved, rca.target.width, engine.netlist[rca.target.net_id].is_signed);
                        } else {
                            resolved = eval_driver(rca.drivers[0]);
                            resolved = LogicOps::cast_width(resolved, rca.target.width, engine.netlist[rca.target.net_id].is_signed);
                            NetKind kind = engine.netlist[rca.target.net_id].kind;
                            for (size_t i = 1; i < rca.drivers.size(); ++i) {
                                LogicVal d_val = eval_driver(rca.drivers[i]);
                                d_val = LogicOps::cast_width(d_val, rca.target.width, engine.netlist[rca.target.net_id].is_signed);
                                resolved = LogicOps::resolve_net(kind, resolved, d_val);
                            }
                        }
                    }

                    LogicVal cur = engine.evaluator.read_slice(rca.target);
                    if (cur != resolved) {
                        size_t target_id = (rca.target.net_id < engine.canonical_net_map.size()) ? engine.canonical_net_map[rca.target.net_id] : rca.target.net_id;

                        bool has_path_delay = false;
                        uint64_t min_delay = UINT64_MAX;
                        for (const auto& mpd : engine.module_path_delays) {
                            if (mpd.dst_net_id == target_id) {
                                if (engine.netlist[mpd.src_net_id].changed_in_current_sim_time) {
                                    bool cond_met = true;
                                    if (mpd.cond_expr) {
                                        LogicVal cval = engine.evaluator.eval_expr(mpd.cond_expr, mpd.scope);
                                        cond_met = cval.to_bool();
                                    }
                                    if (cond_met) {
                                        has_path_delay = true;
                                        if (mpd.delay_ticks < min_delay) {
                                            min_delay = mpd.delay_ticks;
                                        }
                                    }
                                }
                            }
                        }

                        if (has_path_delay && min_delay > 0) {
                            uint64_t target_time = current_sim_time + min_delay;
                            pending_specify_events[target_id] = {target_id, resolved, target_time};
                            time_wheel.schedule(target_time, UINT32_MAX);
                            continue;
                        }

                        if (pending_specify_events.find(target_id) != pending_specify_events.end()) {
                            continue;
                        }

                        engine.evaluator.write_slice(rca.target, resolved);
                        notify_net_changed(target_id);
                    } else {
                        size_t target_id = (rca.target.net_id < engine.canonical_net_map.size()) ? engine.canonical_net_map[rca.target.net_id] : rca.target.net_id;
                        pending_specify_events.erase(target_id);
                    }
                }
            }
            if (rca_inner_guard >= 100) {
                for (uint32_t idx : dirty_rcas) {
                    if (idx < rca_enqueued.size()) rca_enqueued[idx] = 0;
                }
                dirty_rcas.clear();
            }
        }
        auto t3 = std::chrono::high_resolution_clock::now();
        t_rca_ms += std::chrono::duration<double, std::milli>(t3 - t2).count();

        // 4. Procedural Continuous Assignments
        for (size_t net_idx = 0; net_idx < engine.netlist.size(); ++net_idx) {
            auto& net = engine.netlist[net_idx];
            if (net.has_procedural_assign && net.procedural_assign_rhs) {
                LogicVal val = engine.evaluator.eval_expr(net.procedural_assign_rhs, net.procedural_assign_scope);
                LogicVal casted = LogicOps::cast_width(val, net.width, net.is_signed);
                if (net.value != casted) {
                    net.set(casted);
                    notify_net_changed(net_idx);
                    changed = true;
                }
            }
        }

        // 5. Drain and evaluate all dirty_comb_procs
        auto t4 = std::chrono::high_resolution_clock::now();
        if (!dirty_comb_procs.empty()) {
            changed = true;
            std::vector<uint32_t> current_comb_batch;
            current_comb_batch.swap(dirty_comb_procs);
            for (uint32_t pid : current_comb_batch) {
                if (pid < comb_proc_enqueued.size()) comb_proc_enqueued[pid] = 0;
            }

            for (uint32_t pid : current_comb_batch) {
                if (pid >= engine.processes.size()) continue;
                auto& bp = engine.processes[pid];
                if (native_dispatch.is_active && pid < native_dispatch.proc_count && native_dispatch.proc_table[pid] != nullptr) {
                    uint64_t old_vals[32];
                    size_t num_targets = std::min((size_t)32, bp.comb_target_nets.size());
                    for (size_t k = 0; k < num_targets; ++k) {
                        old_vals[k] = native_nets_u64[bp.comb_target_nets[k]];
                    }
                    native_dispatch.proc_table[pid](native_nets_u64.data());
                    for (size_t k = 0; k < num_targets; ++k) {
                        uint32_t tid = bp.comb_target_nets[k];
                        uint64_t new_val = native_nets_u64[tid];
                        if (new_val != old_vals[k] || engine.netlist[tid].value.bval != 0) {
                            LogicVal lval = LogicVal::from_u64_2state(new_val, engine.netlist[tid].width, engine.netlist[tid].is_signed);
                            engine.netlist[tid].set(lval);
                            if (tid < engine.alias_members.size()) {
                                for (size_t other_id : engine.alias_members[tid]) {
                                    if (other_id != tid && other_id < engine.netlist.size()) {
                                        engine.netlist[other_id].set(lval);
                                        native_nets_u64[other_id] = new_val;
                                    }
                                }
                            }
                            size_t canon_id = (tid < engine.canonical_net_map.size()) ? engine.canonical_net_map[tid] : tid;
                            notify_net_changed(canon_id);
                        }
                    }
                    continue;
                }
                if (bp.is_pure_fast_proc) {
                    engine.evaluator.execute_fast_assignments(bp);
                } else if (bp.is_bytecode_compiled) {
                    execute_comb_proc_bytecode(bp);
                } else {
                    for (auto& stmt : bp.proc->body) {
                        engine.evaluator.execute_statement(stmt.get(), bp);
                    }
                }
            }
        }
        auto t5 = std::chrono::high_resolution_clock::now();
        t_proc_ms += std::chrono::duration<double, std::milli>(t5 - t4).count();

        if (!dirty_rcas.empty() || !dirty_comb_procs.empty()) {
            changed = true;
        }
    }

    if (iters >= max_delta_cycles) {
        std::cerr << "[StratifiedScheduler] Warning: Delta cycle limit (" << max_delta_cycles
                  << ") reached in propagate_comb(); breaking potential combinational loop.\n";
        for (uint32_t idx : dirty_rcas) {
            if (idx < rca_enqueued.size()) rca_enqueued[idx] = 0;
        }
        dirty_rcas.clear();
        for (uint32_t pid : dirty_comb_procs) {
            if (pid < comb_proc_enqueued.size()) comb_proc_enqueued[pid] = 0;
        }
        dirty_comb_procs.clear();
    }
    comb_dirty = false;
}

bool StratifiedScheduler::evaluate_timing_checks() {
    if (engine.timing_checks.empty()) return false;
    bool activity = false;
    uint64_t ratio = Lexer::get_timescale_ratio();
    if (ratio == 0) ratio = 1;

    for (auto& rtc : engine.timing_checks) {
        if (rtc.ref_net_id >= engine.netlist.size()) continue;
        const auto& ref_net = engine.netlist[rtc.ref_net_id];

        // Check if reference event occurred
        bool ref_edge_occurred = false;
        if (ref_net.change_counter != rtc.last_ref_change_counter) {
            rtc.last_ref_change_counter = ref_net.change_counter;
            if (rtc.ref_edge == TimingCheckEdge::POSEDGE && ref_net.is_posedge()) {
                ref_edge_occurred = true;
            } else if (rtc.ref_edge == TimingCheckEdge::NEGEDGE && ref_net.is_negedge()) {
                ref_edge_occurred = true;
            } else if (rtc.ref_edge == TimingCheckEdge::ANY && ref_net.modified) {
                ref_edge_occurred = true;
            }
        }

        // Check if data event occurred
        bool data_edge_occurred = false;
        if (rtc.data_net_id < engine.netlist.size()) {
            const auto& data_net = engine.netlist[rtc.data_net_id];
            if (data_net.change_counter != rtc.last_data_change_counter) {
                rtc.last_data_change_counter = data_net.change_counter;
                if (rtc.data_edge == TimingCheckEdge::POSEDGE && data_net.is_posedge()) {
                    data_edge_occurred = true;
                } else if (rtc.data_edge == TimingCheckEdge::NEGEDGE && data_net.is_negedge()) {
                    data_edge_occurred = true;
                } else if (rtc.data_edge == TimingCheckEdge::ANY && data_net.modified) {
                    data_edge_occurred = true;
                }
            }
        }

        auto trigger_notifier = [&]() {
            if (rtc.notifier_net_id < engine.netlist.size()) {
                LogicVal cur_val = engine.netlist[rtc.notifier_net_id].value;
                LogicVal toggled = cur_val.to_bool() ? LogicVal(1, 0) : LogicVal(1, 1);
                engine.netlist[rtc.notifier_net_id].set(toggled);
                if (rtc.notifier_net_id < engine.alias_members.size()) {
                    for (size_t other_id : engine.alias_members[rtc.notifier_net_id]) {
                        if (other_id != rtc.notifier_net_id && other_id < engine.netlist.size()) {
                            engine.netlist[other_id].set(toggled);
                        }
                    }
                }
                activity = true;
            }
        };

        std::string fname = rtc.file;
        auto p = fname.rfind('/');
        if (p != std::string::npos) fname = fname.substr(p + 1);
        auto p2 = fname.rfind('\\');
        if (p2 != std::string::npos) fname = fname.substr(p2 + 1);
        if (fname.empty()) fname = "design.sv";

        std::string ref_edge_str = (rtc.ref_edge == TimingCheckEdge::POSEDGE) ? "posedge " : (rtc.ref_edge == TimingCheckEdge::NEGEDGE ? "negedge " : "");
        std::string data_edge_str = (rtc.data_edge == TimingCheckEdge::POSEDGE) ? "posedge " : (rtc.data_edge == TimingCheckEdge::NEGEDGE ? "negedge " : "");

        if (rtc.type == TimingCheckType::SETUP) {
            if (data_edge_occurred) {
                rtc.t_last_data = current_sim_time;
            }
            if (ref_edge_occurred) {
                uint64_t t_ref = current_sim_time;
                if (rtc.t_last_data != (uint64_t)-1) {
                    uint64_t elapsed = t_ref - rtc.t_last_data;
                    if (elapsed < rtc.limit_ticks) {
                        std::cout << "\"" << fname << "\", " << rtc.line << ": Timing violation in " << rtc.scope << "\n";
                        std::cout << "    $setup( " << rtc.data_name << ":" << (rtc.t_last_data / ratio) << ", "
                                  << ref_edge_str << rtc.ref_name << ":" << (t_ref / ratio) << ", limit: " << (rtc.limit_ticks / ratio) << " );\n";
                        trigger_notifier();
                    }
                }
                rtc.t_last_ref = t_ref;
            }
        } else if (rtc.type == TimingCheckType::HOLD) {
            if (ref_edge_occurred) {
                rtc.t_last_ref = current_sim_time;
            }
            if (data_edge_occurred) {
                uint64_t t_data = current_sim_time;
                if (rtc.t_last_ref != (uint64_t)-1) {
                    uint64_t elapsed = t_data - rtc.t_last_ref;
                    if (elapsed < rtc.limit_ticks) {
                        std::cout << "\"" << fname << "\", " << rtc.line << ": Timing violation in " << rtc.scope << "\n";
                        std::cout << "    $hold( " << ref_edge_str << rtc.ref_name << ":" << (rtc.t_last_ref / ratio) << ", "
                                  << rtc.data_name << ":" << (t_data / ratio) << ", limit: " << (rtc.limit_ticks / ratio) << " );\n";
                        trigger_notifier();
                    }
                }
                rtc.t_last_data = t_data;
            }
        } else if (rtc.type == TimingCheckType::WIDTH) {
            if (ref_edge_occurred) {
                if (rtc.t_last_ref != (uint64_t)-1) {
                    uint64_t elapsed = current_sim_time - rtc.t_last_ref;
                    if (elapsed < rtc.limit_ticks && elapsed > rtc.threshold_ticks) {
                        std::cout << "\"" << fname << "\", " << rtc.line << ": Timing violation in " << rtc.scope << "\n";
                        std::cout << "    $width( " << ref_edge_str << rtc.ref_name << ":" << (current_sim_time / ratio) << ", limit: " << (rtc.limit_ticks / ratio) << " );\n";
                        trigger_notifier();
                    }
                }
                rtc.t_last_ref = current_sim_time;
            }
        } else if (rtc.type == TimingCheckType::RECOVERY) {
            if (ref_edge_occurred) {
                rtc.t_last_ref = current_sim_time;
            }
            if (data_edge_occurred) {
                uint64_t t_data = current_sim_time;
                if (rtc.t_last_ref != (uint64_t)-1) {
                    uint64_t elapsed = t_data - rtc.t_last_ref;
                    if (elapsed < rtc.limit_ticks) {
                        std::cout << "\"" << fname << "\", " << rtc.line << ": Timing violation in " << rtc.scope << "\n";
                        std::cout << "    $recovery( " << ref_edge_str << rtc.ref_name << ":" << (rtc.t_last_ref / ratio) << ", "
                                  << data_edge_str << rtc.data_name << ":" << (t_data / ratio) << ", limit: " << (rtc.limit_ticks / ratio) << " );\n";
                        trigger_notifier();
                    }
                }
                rtc.t_last_data = t_data;
            }
        }
    }
    return activity;
}

void StratifiedScheduler::evaluate_sequential_blocks(uint32_t triggered_net_id, EdgeType edge) {
    auto seq_fn = (llvm_dispatch.is_active && llvm_dispatch.seq_clock_fn != nullptr)
                  ? llvm_dispatch.seq_clock_fn
                  : (native_dispatch.is_active ? native_dispatch.seq_clock_fn : nullptr);
    if (!native_seq_fired_in_time_slot && seq_fn != nullptr) {
        bool clock_triggered = false;
        if (triggered_net_id != UINT32_MAX) {
            if (edge == EdgeType::POSEDGE) {
                size_t canon_trig = (triggered_net_id < engine.canonical_net_map.size()) ?
                                     engine.canonical_net_map[triggered_net_id] : triggered_net_id;
                for (uint32_t cid : native_clk_net_ids) {
                    if (cid == triggered_net_id || cid == canon_trig) {
                        clock_triggered = true;
                        break;
                    }
                }
                if (!clock_triggered && triggered_net_id < engine.netlist.size()) {
                    const auto& nname = engine.netlist[triggered_net_id].name;
                    if (nname == "clk_i" || nname == "clk" || nname == "clk_sys" || nname == "IO_CLK") {
                        clock_triggered = true;
                    }
                }
            }
        } else {
            for (uint32_t cid : native_clk_net_ids) {
                if (cid < engine.netlist.size() && engine.netlist[cid].is_posedge()) {
                    clock_triggered = true;
                    break;
                }
            }
        }

        if (clock_triggered) {
            native_seq_fired_in_time_slot = true;
            if (native_nets_next.size() < native_nets_u64.size()) {
                native_nets_next.resize(native_nets_u64.size(), 0);
            }
            std::copy(native_nets_u64.begin(), native_nets_u64.end(), native_nets_next.begin());

            if (native_modified_mask.size() < native_nets_u64.size()) {
                native_modified_mask.assign(native_nets_u64.size(), 0);
            }

            auto whole_fn = (native_dispatch.is_active && native_dispatch.whole_cycle_fn != nullptr)
                            ? native_dispatch.whole_cycle_fn
                            : nullptr;

            if (whole_fn != nullptr) {
                // Fused monolithic execution: posedge flops + bulk commit + topological comb!
                whole_fn(
                    native_nets_u64.data(),
                    native_nets_next.data(),
                    native_modified_mask.data()
                );
            } else {
                seq_fn(
                    native_nets_u64.data(),
                    native_nets_next.data(),
                    native_modified_mask.data()
                );
            }

            dirty_native_nets.clear();
            cas::simd::simd_extract_dirty_indices(
                native_modified_mask.data(),
                native_modified_mask.size(),
                dirty_native_nets
            );
            for (uint32_t dst : native_seq_dst_nets) {
                if (dst < engine.netlist.size() && engine.netlist[dst].value.bval != 0) {
                    if (dst < native_modified_mask.size() && !native_modified_mask[dst]) {
                        native_modified_mask[dst] = 1;
                        dirty_native_nets.push_back(dst);
                    }
                }
            }

            if (memory_fastpath.bus.is_valid) {
                memory_fastpath.service_memory_cycle(
                    engine,
                    native_nets_u64.data(),
                    native_nets_next.data(),
                    native_modified_mask.data(),
                    dirty_native_nets
                );
            }

            if (whole_fn != nullptr) {
                // Commit native clock updates to engine.netlist immediately
                commit_native_clock_updates();

                if (memory_fastpath.bus.is_valid) {
                    auto topo_fn = (llvm_dispatch.is_active && llvm_dispatch.topological_comb_fn != nullptr)
                                   ? llvm_dispatch.topological_comb_fn
                                   : (native_dispatch.is_active ? native_dispatch.topological_comb_fn : nullptr);
                    if (topo_fn != nullptr) {
                        topo_fn(native_nets_u64.data());
                    }
                }

                // Sync topological target nets to engine.netlist
                for (const auto& rec : topological_target_records) {
                    uint64_t val = native_nets_u64[rec.net_id];
                    if (val != engine.netlist[rec.net_id].value.aval || engine.netlist[rec.net_id].value.bval != 0) {
                        LogicVal lval = LogicVal::from_u64_2state(val, rec.width, rec.is_signed);
                        engine.netlist[rec.net_id].set(lval);
                        if (rec.net_id < engine.alias_members.size()) {
                            for (size_t other_id : engine.alias_members[rec.net_id]) {
                                if (other_id != rec.net_id && other_id < engine.netlist.size()) {
                                    engine.netlist[other_id].set(lval);
                                    native_nets_u64[other_id] = val;
                                }
                            }
                        }
                        notify_net_changed(rec.canon_id);
                    }
                }
                comb_dirty = false;
            } else {
                comb_dirty = true;
            }
            for (auto& bp : engine.processes) {
                if (bp.is_native_seq) {
                    bp.fired_in_time_slot = true;
                }
            }
        }
    }

    for (auto& bp : engine.processes) {
        if (bp.proc->type != ProcessType::SEQUENTIAL) continue;
        if (bp.fired_in_time_slot) continue;
        if (seq_fn != nullptr && bp.is_native_seq) continue;

        bool triggered = false;
        for (auto& sens : bp.proc->sensitivity_list) {
            if (sens.net_id == (size_t)-1) {
                sens.net_id = engine.get_net_id(bp.scope, sens.signal_name);
            }
            size_t net_id = sens.net_id;
            if (net_id == (size_t)-1) continue;
            const auto& net = engine.netlist[net_id];

            if (sens.edge == EdgeType::POSEDGE && net.is_posedge()) { triggered = true; break; }
            else if (sens.edge == EdgeType::NEGEDGE && net.is_negedge()) { triggered = true; break; }
            else if (sens.edge == EdgeType::LEVEL && (net.modified || net.changed_in_current_sim_time || net.value != net.prev_value)) { triggered = true; break; }
        }

        if (triggered) {
            comb_dirty = true;
            bp.fired_in_time_slot = true;
            if (bp.is_pure_fast_proc) {
                engine.evaluator.execute_fast_assignments(bp);
            } else {
                for (auto& stmt : bp.proc->body) {
                    engine.evaluator.execute_statement(stmt.get(), bp);
                }
            }
        }
    }
}

void StratifiedScheduler::evaluate_clocking_blocks() {
    if (engine.clocking_blocks.empty()) return;
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
            // Preponed / historical sampling of input signals
            for (auto& kv : cb.signals) {
                if (kv.second.direction == "input" || kv.second.direction == "inout") {
                    if (kv.second.net_id < engine.netlist.size()) {
                        if (kv.second.is_1step) {
                            if (kv.second.net_id < preponed_net_values.size()) {
                                kv.second.sampled_value = preponed_net_values[kv.second.net_id];
                            } else {
                                kv.second.sampled_value = engine.netlist[kv.second.net_id].value;
                            }
                        } else if (kv.second.skew_ticks > 0) {
                            uint64_t sample_time = current_sim_time >= kv.second.skew_ticks ? current_sim_time - kv.second.skew_ticks : 0;
                            kv.second.sampled_value = get_historical_net_value(kv.second.net_id, sample_time);
                        } else {
                            kv.second.sampled_value = engine.netlist[kv.second.net_id].value;
                        }
                    }
                }
            }

            // Commit pending drives synchronized to this clock edge
            for (const auto& drv : cb.pending_drives) {
                uint64_t wake_time = current_sim_time + drv.skew_ticks;
                if (drv.skew_ticks == 0) {
                    queue_nba(drv.net_id, drv.val);
                } else {
                    queue_delayed_nba(wake_time, drv.net_id, drv.val);
                }
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
    if (engine.event_triggered_covergroups.empty()) return;
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

bool StratifiedScheduler::commit_native_clock_updates() {
    if (dirty_native_nets.empty()) return false;

    comb_dirty = true;
    nba_commits_count++;

    for (uint32_t dst : dirty_native_nets) {
        if (dst >= engine.netlist.size()) continue;
        native_modified_mask[dst] = 0;

        uint64_t new_val = native_nets_next[dst];
        native_nets_u64[dst] = new_val;

        size_t target_id = (dst < engine.canonical_net_map.size()) ? engine.canonical_net_map[dst] : dst;
        if (!engine.netlist[target_id].has_procedural_assign) {
            LogicVal lval = LogicVal::from_u64_2state(
                new_val, engine.netlist[target_id].width, engine.netlist[target_id].is_signed);
            engine.netlist[target_id].set(lval);
            if (dst != target_id) {
                engine.netlist[dst].set(lval);
                if (dst < native_nets_u64.size()) {
                    native_nets_u64[dst] = new_val;
                }
            }

            if (target_id < engine.alias_members.size()) {
                for (size_t other_id : engine.alias_members[target_id]) {
                    if (other_id != target_id && other_id < engine.netlist.size()) {
                        engine.netlist[other_id].set(lval);
                        if (other_id < native_nets_u64.size()) {
                            native_nets_u64[other_id] = new_val;
                        }
                    }
                }
            }
            notify_net_changed(target_id);
        }
    }
    dirty_native_nets.clear();
    return true;
}

bool StratifiedScheduler::commit_nba_updates() {
    bool native_committed = commit_native_clock_updates();

    if (!native_committed && nba_queue.empty() && nba_mem_queue.empty() && nba_event_queue.empty()) return false;
    comb_dirty = true;

    if (!nba_queue.empty()) {
        nba_commits_count++;
        for (const auto& update : nba_queue) {
            size_t target_id = (update.net_id < engine.canonical_net_map.size()) ? engine.canonical_net_map[update.net_id] : update.net_id;
            if (!engine.netlist[target_id].has_procedural_assign) {
                LogicVal old_val = engine.netlist[target_id].value;
                if (update.is_slice) {
                    auto& net = engine.netlist[target_id];
                    LogicVal new_val = net.value;
                    new_val.deposit_slice(update.offset, update.slice_width, update.val);
                    net.set(new_val);
                    if (target_id < engine.alias_members.size()) {
                        for (size_t other_id : engine.alias_members[target_id]) {
                            if (other_id != target_id && other_id < engine.netlist.size()) {
                                engine.netlist[other_id].set(new_val);
                            }
                        }
                    }
                } else {
                    engine.netlist[target_id].set(update.val);
                    if (target_id < engine.alias_members.size()) {
                        for (size_t other_id : engine.alias_members[target_id]) {
                            if (other_id != target_id && other_id < engine.netlist.size()) {
                                engine.netlist[other_id].set(update.val);
                            }
                        }
                    }
                }
                if (engine.netlist[target_id].value != old_val) {
                    notify_net_changed(target_id);
                }
            }
        }
        nba_queue.clear();
    }

    for (const auto& mem_up : nba_mem_queue) {
        if (engine.memory_map.find(mem_up.mem_scoped_name) != engine.memory_map.end()) {
            auto& mem = engine.memory_map.at(mem_up.mem_scoped_name);
            LogicVal old_v = mem.get_word(mem_up.addr);
            if (mem_up.is_slice) {
                LogicVal cur = old_v;
                cur.deposit_slice(mem_up.offset, mem_up.slice_width, mem_up.val);
                mem.set_word(mem_up.addr, cur);
            } else {
                mem.set_word(mem_up.addr, mem_up.val);
            }
            if (mem.get_word(mem_up.addr) != old_v) {
                notify_mem_changed();
            }
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
        if (!bp.call_stack.empty()) {
            bp.call_stack.back().pc++;
        } else {
            bp.pc++;
        }
        return;
    }

    TaskNode* target_task = nullptr;
    ClassObject* cl_obj = nullptr;
    auto dot = tcall->task_name.rfind('.');
    if (dot != std::string::npos) {
        std::string var_name = tcall->task_name.substr(0, dot);
        std::string method_name = tcall->task_name.substr(dot + 1);

        if (engine.is_semaphore(var_name, bp.scope) || engine.is_mailbox(var_name, bp.scope) ||
            method_name == "putc" || method_name == "len" || method_name == "toupper" ||
            method_name == "tolower" || method_name == "substr" || method_name == "getc" ||
            method_name == "atoi" || method_name == "atohex" || method_name == "atooct" ||
            method_name == "atobin" || method_name == "atoreal" || method_name == "compare" ||
            method_name == "icompare") {
            engine.evaluator.execute_statement(tcall, bp);
            if (!bp.call_stack.empty()) {
                bp.call_stack.back().pc++;
            } else {
                bp.pc++;
            }
            return;
        }

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
            target_task = cl_obj->class_def->dispatch_task(method_name);
            if (!target_task) {
                FunctionNode* fn = cl_obj->class_def->dispatch_function(method_name);
                if (fn) {
                    engine.evaluator.execute_statement(tcall, bp);
                    if (!bp.call_stack.empty()) {
                        bp.call_stack.back().pc++;
                    } else {
                        bp.pc++;
                    }
                    return;
                }
            }
        }
    }

    if (!target_task) {
        if (engine.task_map.find(scoped_tname) != engine.task_map.end()) {
            target_task = engine.task_map.at(scoped_tname);
        } else if (engine.task_map.find(tcall->task_name) != engine.task_map.end()) {
            target_task = engine.task_map.at(tcall->task_name);
        } else if (engine.function_map.find(scoped_tname) != engine.function_map.end() ||
                   engine.function_map.find(tcall->task_name) != engine.function_map.end()) {
            engine.evaluator.execute_statement(tcall, bp);
            if (!bp.call_stack.empty()) {
                bp.call_stack.back().pc++;
            } else {
                bp.pc++;
            }
            return;
        } else {
            if (!bp.call_stack.empty() && bp.call_stack.back().this_obj) {
                cl_obj = bp.call_stack.back().this_obj;
            } else if (engine.evaluator.get_expr_eval().get_current_this()) {
                cl_obj = engine.evaluator.get_expr_eval().get_current_this();
            }
            if (cl_obj && cl_obj->class_def) {
                target_task = cl_obj->class_def->dispatch_task(tcall->task_name);
                if (!target_task) {
                    FunctionNode* fn = cl_obj->class_def->dispatch_function(tcall->task_name);
                    if (fn) {
                        engine.evaluator.execute_statement(tcall, bp);
                        if (!bp.call_stack.empty()) {
                            bp.call_stack.back().pc++;
                        } else {
                            bp.pc++;
                        }
                        return;
                    }
                }
            }
        }
    }

    if (!target_task) {
        throw std::runtime_error("Unknown task called: " + tcall->task_name);
    }

    TaskActivationFrame frame(tcall->task_name, target_task, cl_obj);

    const size_t num_formals = target_task->ports.size();
    struct ArgBinding {
        ExpressionNode* actual_expr = nullptr;
        bool is_passed = false;
    };
    std::vector<ArgBinding> bindings(num_formals);

    for (size_t i = 0; i < tcall->args.size(); ++i) {
        bool is_named = (i < tcall->arg_names.size() && !tcall->arg_names[i].empty());
        if (!is_named) {
            if (i >= num_formals) {
                throw std::runtime_error("[CAS-DTINPCIL] Too many positional arguments in subroutine call '" + tcall->task_name + "' per IEEE 1800-2023 section 13.5");
            }
            bindings[i].is_passed = true;
            bindings[i].actual_expr = tcall->args[i].get();
        } else {
            const std::string& port_name = tcall->arg_names[i];
            int formal_idx = -1;
            for (size_t k = 0; k < num_formals; ++k) {
                if (target_task->ports[k].name == port_name) {
                    formal_idx = (int)k;
                    break;
                }
            }
            if (formal_idx == -1) {
                throw std::runtime_error("[CAS-DTINPCIL] Unrecognized formal argument name '" + port_name + "' in task call '" + tcall->task_name + "' per IEEE 1800-2023 section 13.5.1");
            }
            if (bindings[formal_idx].is_passed) {
                throw std::runtime_error("[CAS-DTINPCIL] Duplicate binding for formal argument '" + port_name + "' in task call '" + tcall->task_name + "' per IEEE 1800-2023 section 13.5.1");
            }
            bindings[formal_idx].is_passed = true;
            bindings[formal_idx].actual_expr = tcall->args[i].get();
        }
    }

    // 1. Formal port bindings & argument passing
    for (size_t i = 0; i < num_formals; ++i) {
        const auto& p = target_task->ports[i];
        uint32_t w = 32;
        if (p.msb_expr && p.lsb_expr) {
            auto msb_n = dynamic_cast<NumberNode*>(p.msb_expr.get());
            auto lsb_n = dynamic_cast<NumberNode*>(p.lsb_expr.get());
            if (msb_n && lsb_n) {
                w = (msb_n->aval >= lsb_n->aval) ? (uint32_t)(msb_n->aval - lsb_n->aval + 1) : (uint32_t)(lsb_n->aval - msb_n->aval + 1);
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

        ExpressionNode* val_expr = nullptr;
        if (bindings[i].is_passed) {
            if (bindings[i].actual_expr != nullptr) {
                val_expr = bindings[i].actual_expr;
            } else {
                if (p.default_value) {
                    val_expr = p.default_value.get();
                } else {
                    throw std::runtime_error("[CAS-DTINPCIL] Missing mandatory subroutine argument '" + p.name + "' without default value in task call '" + tcall->task_name + "' per IEEE 1800-2023 section 13.5.3");
                }
            }
        } else {
            if (p.default_value) {
                val_expr = p.default_value.get();
            } else {
                throw std::runtime_error("[CAS-DTINPCIL] Missing mandatory subroutine argument '" + p.name + "' without default value in task call '" + tcall->task_name + "' per IEEE 1800-2023 section 13.5.3");
            }
        }

        if (p.direction == "input" || p.direction == "inout") {
            if (val_expr) {
                LogicVal arg_val = engine.evaluator.eval_expr(val_expr, bp.scope);
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
            if (bindings[i].actual_expr) {
                if (auto id = dynamic_cast<IdentifierNode*>(bindings[i].actual_expr)) {
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

    if (engine.evaluator.get_expr_eval().get_context().has_active_frame()) {
        LocalVar* local = engine.evaluator.get_expr_eval().get_context().lookup_local(actual_name);
        if (local) {
            local->value = LogicOps::cast_width(val, local->width, local->is_signed);
            return;
        }
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
        if (net.value != casted) {
            net.set(casted);
            notify_net_changed(net_id);
        }
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

    // 2. Check wait fork on ancestor chain (Â§9.6.3)
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
    comb_dirty = true;

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
                    if (cb_id == (size_t)-1) {
                        throw std::runtime_error("[CAS-DTINPCIL] [CAS-DCNS] Default clocking not specified: Cycle delay operator requires a default clocking to be specified.");
                    }
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

            // Event Trigger inside task: -> e or ->> e (IEEE 1800-2023 Â§15.4)
            if (auto trig = dynamic_cast<EventTriggerStatementNode*>(stmt)) {
                cur_frame.pc++;
                trigger_named_event(trig->event_name, bp.scope, trig->is_nonblocking);
                continue;
            }

            // wait(e.triggered) inside task: (IEEE 1800-2023 Â§15.5)
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

            if (dynamic_cast<LetStatementNode*>(stmt)) {
                cur_frame.pc++;
                continue;
            }

            // Fork inside task: IEEE 1800-2023 Â§9.3.2
            if (auto fork_stmt = dynamic_cast<ForkStatementNode*>(stmt)) {
                cur_frame.pc++;
                invoke_fork(pid, fork_stmt);
                if (engine.processes[pid].fork_barrier.active) {
                    break;
                }
                continue;
            }

            // Disable fork inside task: IEEE 1800-2023 Â§9.6.2
            if (auto df = dynamic_cast<DisableForkStatementNode*>(stmt)) {
                cur_frame.pc++;
                disable_fork(pid);
                continue;
            }

            // Wait fork inside task: IEEE 1800-2023 Â§9.6.3
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

            // Event Trigger: -> e or ->> e (IEEE 1800-2023 Â§15.4)
            if (auto trig = dynamic_cast<EventTriggerStatementNode*>(stmt)) {
                bp.pc++;
                trigger_named_event(trig->event_name, bp.scope, trig->is_nonblocking);
                continue;
            }

            // wait(e.triggered): (IEEE 1800-2023 Â§15.5)
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

            // 4. Nested Fork: IEEE 1800-2023 Â§9.3.2
            if (auto fork_stmt = dynamic_cast<ForkStatementNode*>(stmt)) {
                bp.pc++;
                invoke_fork(pid, fork_stmt);
                if (engine.processes[pid].fork_barrier.active) {
                    break;
                }
                continue;
            }

            // Disable fork in child: IEEE 1800-2023 Â§9.6.2
            if (auto df = dynamic_cast<DisableForkStatementNode*>(stmt)) {
                bp.pc++;
                disable_fork(pid);
                continue;
            }

            // Wait fork in child: IEEE 1800-2023 Â§9.6.3
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

        if (dynamic_cast<LetStatementNode*>(stmt)) {
            bp.pc++;
            continue;
        }

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

        // Event Trigger: -> e or ->> e (IEEE 1800-2023 Â§15.4)
        if (auto trig = dynamic_cast<EventTriggerStatementNode*>(stmt)) {
            bp.pc++;
            trigger_named_event(trig->event_name, bp.scope, trig->is_nonblocking);
            continue;
        }

        // wait(e.triggered): (IEEE 1800-2023 Â§15.5)
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
                if (cb_id == (size_t)-1) {
                    throw std::runtime_error("[CAS-DTINPCIL] [CAS-DCNS] Default clocking not specified: Cycle delay operator requires a default clocking to be specified.");
                }
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

        // 4. Fork / Join Statements: IEEE 1800-2023 Â§9.3.2
        if (auto fork_stmt = dynamic_cast<ForkStatementNode*>(stmt)) {
            bp.pc++;
            invoke_fork(pid, fork_stmt);
            if (engine.processes[pid].fork_barrier.active) {
                break;
            }
            continue;
        }

        // Disable fork: IEEE 1800-2023 Â§9.6.2
        if (auto df = dynamic_cast<DisableForkStatementNode*>(stmt)) {
            bp.pc++;
            disable_fork(pid);
            continue;
        }

        // Wait fork: IEEE 1800-2023 Â§9.6.3
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

        // IEEE 1800-2023 Â§10.6.2: Blocking Assignment with Intra-Assignment Event Control
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

        // 6. IEEE 1800-2023 Â§10.6.1: Blocking Assignment with Intra-Assignment Delay
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
    comb_dirty = true;
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
            if (evaluate_timing_checks()) {
                active_activity = true;
            }
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

        // 4. Reactive Region (IEEE 1800-2023 Â§24)
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
    AsyncTraceBuffer::instance().start();
    current_sim_time = 0;
    simulation_active = true;

    rca_enqueued.assign(engine.resolved_continuous_assigns.size(), 1);
    dirty_rcas.clear();
    dirty_rcas.reserve(engine.resolved_continuous_assigns.size());
    for (uint32_t i = 0; i < engine.resolved_continuous_assigns.size(); ++i) {
        dirty_rcas.push_back(i);
    }

    comb_proc_enqueued.assign(engine.processes.size(), 0);
    dirty_comb_procs.clear();
    for (uint32_t i = 0; i < engine.processes.size(); ++i) {
        if (engine.processes[i].proc && (engine.processes[i].proc->type == ProcessType::COMBINATIONAL ||
                                         engine.processes[i].proc->is_always_comb ||
                                         engine.processes[i].proc->is_always_latch)) {
            comb_proc_enqueued[i] = 1;
            dirty_comb_procs.push_back(i);
        }
    }
    comb_dirty = true;
    if (native_dispatch.is_active || llvm_dispatch.is_active) {
        init_scheduler();
    }

    for (auto& n : engine.netlist) {
        n.prev_value = n.value;
        n.modified = false;
        n.changed_in_current_sim_time = false;
    }

    for (auto& bp : engine.processes) {
        bp.fired_in_time_slot = false;
    }
    native_seq_fired_in_time_slot = false;
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

    uint64_t sim_step_count = 0;
    while (simulation_active && !time_wheel.empty()) {
        if (engine.max_cycles > 0 && sim_step_count >= engine.max_cycles) {
            simulation_active = false;
            break;
        }
        uint64_t slot_time = time_wheel.next_time();
        current_sim_time = slot_time;
        sim_step_count++;
        if (sim_step_count % 500 == 0) {
            std::cout << "[CAS PROGRESS] Time: " << current_sim_time << " ns (" << sim_step_count << " steps)\n" << std::flush;
        }

        for (auto& n : engine.netlist) {
            if (n.changed_in_current_sim_time) n.changed_in_current_sim_time = false;
        }
        transfer_due_delayed_updates(slot_time);

        for (auto& bp : engine.processes) {
            bp.fired_in_time_slot = false;
        }
        native_seq_fired_in_time_slot = false;
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
                    LogicVal old_v = engine.netlist[net_id].value;
                    LogicVal casted = LogicOps::cast_width(b_item.val, engine.netlist[net_id].width, engine.netlist[net_id].is_signed);
                    engine.netlist[net_id].set(casted);
                    if (net_id < engine.alias_members.size()) {
                        for (size_t other_id : engine.alias_members[net_id]) {
                            if (other_id != net_id && other_id < engine.netlist.size()) {
                                engine.netlist[other_id].set(casted);
                            }
                        }
                    }
                    if (engine.netlist[net_id].value != old_v) {
                        notify_net_changed(net_id);
                    }
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
            if (n.modified) n.commit_time_slot();
        }
    }
    AsyncTraceBuffer::instance().stop();
    std::cout << "[PROFILE] slice_ms=" << t_slice_ms << " rca_ms=" << t_rca_ms << " proc_ms=" << t_proc_ms << std::endl;
}
