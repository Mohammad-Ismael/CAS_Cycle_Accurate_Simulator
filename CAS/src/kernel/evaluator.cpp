#include "../../include/kernel/evaluator.hpp"
#include "../../include/engine.hpp"
#include "../../include/kernel/logic_ops.hpp"
#include "../../include/frontend/parser.hpp"
#include <iostream>
#include <iomanip>
#include <unordered_set>
#include <functional>
#include <queue>

Evaluator::Evaluator(Engine& eng) 
    : engine(eng), expr_eval(eng), current_bp(nullptr) {}

void Evaluator::set_current_process(BoundProcess* bp) {
    current_bp = bp;
    expr_eval.set_current_process(bp);
}

std::vector<size_t> Evaluator::get_connected_nets(size_t start_id) {
    std::vector<size_t> result;
    std::unordered_set<size_t> visited;
    std::queue<size_t> q;
    q.push(start_id);
    visited.insert(start_id);
    while (!q.empty()) {
        size_t curr = q.front();
        q.pop();
        result.push_back(curr);
        for (const auto& bind : engine.slice_bindings) {
            if (bind.src.net_id == curr && visited.find(bind.dst.net_id) == visited.end()) {
                visited.insert(bind.dst.net_id);
                q.push(bind.dst.net_id);
            }
            if (bind.dst.net_id == curr && visited.find(bind.src.net_id) == visited.end()) {
                visited.insert(bind.src.net_id);
                q.push(bind.src.net_id);
            }
        }
    }
    return result;
}

LogicVal Evaluator::read_slice(const NetSliceRef& ref) {
    size_t target_id = (ref.net_id < engine.canonical_net_map.size()) ? engine.canonical_net_map[ref.net_id] : ref.net_id;
    const auto& net = engine.netlist[target_id];
    const auto& net_val = net.value;
    if (net_val.is_string || net.type_name == "string") {
        return net_val;
    }
    return net_val.extract_slice(ref.offset, ref.width);
}

void Evaluator::write_slice(const NetSliceRef& ref, const LogicVal& val) {
    size_t target_id = (ref.net_id < engine.canonical_net_map.size()) ? engine.canonical_net_map[ref.net_id] : ref.net_id;
    auto& target_net = engine.netlist[target_id];
    if (target_net.has_procedural_assign) return;
    if (val.is_string || target_net.type_name == "string") {
        target_net.set(val);
        return;
    }
    LogicVal casted = LogicOps::cast_width(val, ref.width, target_net.is_signed);

    auto cur = target_net.value;
    cur.deposit_slice(ref.offset, ref.width, casted);
    target_net.set(cur);
    if (target_id < engine.alias_members.size()) {
        for (size_t other_id : engine.alias_members[target_id]) {
            if (other_id != target_id && other_id < engine.netlist.size()) {
                engine.netlist[other_id].set(cur);
            }
        }
    }
}

void Evaluator::write_variable(const std::string& var_name, const LogicVal& val, BoundProcess& bp) {
    LocalVar* local = nullptr;
    if (expr_eval.get_context().has_active_frame()) {
        local = expr_eval.get_context().lookup_local(var_name);
    }
    if (!local && !bp.call_stack.empty()) {
        for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
            local = it->local_frame.get_var(var_name);
            if (local) break;
        }
    }
    if (local) {
        if (local->type_name == "string" || val.is_string) {
            local->value = val;
            return;
        }
        local->value = LogicOps::cast_width(val, local->width, local->is_signed);
        return;
    }
    size_t nid = engine.get_net_id(bp.scope, var_name);
    if (nid != (size_t)-1) {
        size_t target_id = (nid < engine.canonical_net_map.size()) ? engine.canonical_net_map[nid] : nid;
        if (!engine.netlist[target_id].has_procedural_assign) {
            engine.netlist[target_id].set(val);
            if (target_id < engine.alias_members.size()) {
                for (size_t other_id : engine.alias_members[target_id]) {
                    if (other_id != target_id && other_id < engine.netlist.size()) {
                        engine.netlist[other_id].set(val);
                    }
                }
            }
        }
    }
}

LogicVal Evaluator::eval_expr(ExpressionNode* expr, const std::string& scope) {
    return expr_eval.eval(expr, scope);
}

LogicVal Evaluator::eval_gate(const GateInstance& g) {
    if (g.type == GateType::UDP) {
        auto it = Parser::global_udp_defs.find(g.udp_name);
        if (it == Parser::global_udp_defs.end()) {
            return LogicVal::make_x(1);
        }
        const auto& udp = it->second;
        std::vector<char> actual_inputs;
        for (auto* expr : g.in_exprs) {
            LogicVal v = eval_expr(expr, g.scope);
            char c = 'x';
            if ((v.bval & 1) != 0) {
                c = 'x';
            } else if ((v.aval & 1) != 0) {
                c = '1';
            } else {
                c = '0';
            }
            actual_inputs.push_back(c);
        }

        for (const auto& row : udp->table) {
            if (row.inputs.size() != actual_inputs.size()) continue;
            bool match = true;
            for (size_t i = 0; i < actual_inputs.size(); ++i) {
                char row_sym = row.inputs[i];
                char act_sym = actual_inputs[i];
                if (row_sym == '?') {
                    continue;
                }
                if (row_sym == 'b' || row_sym == 'B') {
                    if (act_sym == '0' || act_sym == '1') continue;
                    match = false;
                    break;
                }
                if (row_sym == act_sym) {
                    continue;
                }
                match = false;
                break;
            }
            if (match) {
                if (row.output == '1') return LogicVal(1, 1, 0);
                if (row.output == '0') return LogicVal(1, 0, 0);
                if (row.output == '-') {
                    return read_slice(g.out_target);
                }
                return LogicVal(1, 1, 1);
            }
        }
        return LogicVal(1, 1, 1);
    }

    if (g.type == GateType::BUFIF0 || g.type == GateType::BUFIF1 ||
        g.type == GateType::NOTIF0 || g.type == GateType::NOTIF1) {
        LogicVal data_val = (g.in_exprs.size() > 0) ? eval_expr(g.in_exprs[0], g.scope) : LogicVal::make_x(1);
        LogicVal ctrl_val = (g.in_exprs.size() > 1) ? eval_expr(g.in_exprs[1], g.scope) : LogicVal::make_x(1);
        bool if1 = (g.type == GateType::BUFIF1 || g.type == GateType::NOTIF1);
        bool inv = (g.type == GateType::NOTIF0 || g.type == GateType::NOTIF1);
        return LogicOps::eval_bufif(if1, inv, data_val, ctrl_val);
    }

    if (g.type == GateType::BUF) {
        return (g.in_exprs.size() > 0) ? eval_expr(g.in_exprs[0], g.scope) : LogicVal::make_x(1);
    }
    if (g.type == GateType::NOT) {
        LogicVal in0 = (g.in_exprs.size() > 0) ? eval_expr(g.in_exprs[0], g.scope) : LogicVal::make_x(1);
        return LogicOps::bitwise_not(in0);
    }

    if (g.in_exprs.empty()) return LogicVal::make_x(1);
    LogicVal res = eval_expr(g.in_exprs[0], g.scope);
    for (size_t i = 1; i < g.in_exprs.size(); ++i) {
        LogicVal next_in = eval_expr(g.in_exprs[i], g.scope);
        if (g.type == GateType::AND || g.type == GateType::NAND) res = LogicOps::bitwise_and(res, next_in);
        else if (g.type == GateType::OR || g.type == GateType::NOR) res = LogicOps::bitwise_or(res, next_in);
        else if (g.type == GateType::XOR || g.type == GateType::XNOR) res = LogicOps::bitwise_xor(res, next_in);
    }
    if (g.type == GateType::NAND || g.type == GateType::NOR || g.type == GateType::XNOR) {
        res = LogicOps::bitwise_not(res);
    }
    return res;
}

void Evaluator::check_monitor() {
    if (!engine.monitor_session.active) return;
    set_current_process(nullptr);
    expr_eval.set_current_process(nullptr);

    bool values_changed = false;
    std::vector<LogicVal> current_values;

    for (const auto& expr : engine.monitor_session.arg_exprs) {
        if (!expr) {
            current_values.push_back(LogicVal::make_x(1));
            continue;
        }
        current_values.push_back(eval_expr(expr.get(), engine.monitor_session.scope));
    }

    if (engine.monitor_session.prev_values.empty()) {
        values_changed = true;
    } else {
        // IEEE 1800-2023 §21.2.2: Only watched signals trigger $monitor, NOT $time
        for (size_t i = 0; i < current_values.size(); ++i) {
            if (i < engine.monitor_session.arg_exprs.size()) {
                if (auto id = dynamic_cast<IdentifierNode*>(engine.monitor_session.arg_exprs[i].get())) {
                    if (id->name == "$time") continue;
                }
            }
            if (current_values[i] != engine.monitor_session.prev_values[i]) {
                values_changed = true;
                break;
            }
        }
    }

    if (values_changed) {
        engine.monitor_session.prev_values = current_values;

        if (!engine.is_monitor_allowed()) return;

        if (!engine.monitor_session.src_file.empty() && engine.monitor_session.src_line > 0) {
            std::cout << "[CAS $monitor " << engine.monitor_session.src_file << ":" << engine.monitor_session.src_line << "] ";
        } else {
            std::cout << "[CAS $monitor] ";
        }

        if (engine.monitor_session.has_leading_time) {
            std::cout << engine.get_sim_time() << " ";
        }

        size_t arg_idx = 0;
        size_t fmt_len = engine.monitor_session.format_string.size();
        for (size_t i = 0; i < fmt_len; ++i) {
            if (engine.monitor_session.format_string[i] == '%' && i + 1 < fmt_len) {
                ++i;
                if (engine.monitor_session.format_string[i] == '%') {
                    std::cout << '%';
                    continue;
                }
                bool pad_zero = false;
                int width = 0;
                if (engine.monitor_session.format_string[i] == '0') {
                    pad_zero = true;
                    ++i;
                }
                while (i < fmt_len && std::isdigit((unsigned char)engine.monitor_session.format_string[i])) {
                    width = width * 10 + (engine.monitor_session.format_string[i] - '0');
                    ++i;
                }
                char spec = (i < fmt_len) ? engine.monitor_session.format_string[i] : ' ';

                if (arg_idx < current_values.size()) {
                    const auto& val = current_values[arg_idx++];
                    if (spec == 's') {
                        if (val.is_string) std::cout << val.str_val;
                        else std::cout << val.aval;
                    } else if (val.is_unknown() && (spec == 'd' || spec == 't')) {
                        std::cout << "x";
                    } else {
                        if (spec == 'd' || spec == 't') {
                            if (val.is_real) {
                                std::cout << (int64_t)val.real_val;
                            } else if (val.is_signed) {
                                std::cout << val.to_signed();
                            } else {
                                std::cout << val.aval;
                            }
                        } else if (spec == 'f') {
                            std::cout << (val.is_real ? val.real_val : (double)val.aval);
                        } else if (spec == 'b') {
                            std::cout << val.to_bin_string();
                        } else if (spec == 'h' || spec == 'x') {
                            std::string hx = val.to_hex_string(true);
                            if (pad_zero && width > 0) {
                                while (hx.size() < (size_t)width) hx = "0" + hx;
                            }
                            std::cout << hx;
                        } else {
                            std::cout << val.aval;
                        }
                    }
                }
            } else {
                std::cout << engine.monitor_session.format_string[i];
            }
        }
        std::cout << "\n";
    }
}

void Evaluator::execute_statement(StatementNode* stmt, BoundProcess& bp) {
    if (!stmt) return;
    set_current_process(&bp);

    if (expr_eval.get_context().has_active_frame() && expr_eval.get_context().current_frame()->returned) {
        return;
    }

    if (dynamic_cast<LetStatementNode*>(stmt)) {
        return;
    }

    if (auto ret_node = dynamic_cast<ReturnStatementNode*>(stmt)) {
        if (expr_eval.get_context().has_active_frame()) {
            if (ret_node->expr) {
                LogicVal ret_val = eval_expr(ret_node->expr.get(), bp.scope);
                ScopeFrame* frame = expr_eval.get_context().current_frame();
                if (frame) {
                    expr_eval.get_context().set_local_value(frame->func_return_name, ret_val);
                    frame->returned = true;
                }
            } else {
                ScopeFrame* frame = expr_eval.get_context().current_frame();
                if (frame) {
                    frame->returned = true;
                }
            }
        }
        return;
    }
    if (auto blk = dynamic_cast<BlockStatementNode*>(stmt)) {
        for (auto& s : blk->statements) {
            if (expr_eval.get_context().current_frame() && expr_eval.get_context().current_frame()->returned) return;
            execute_statement(s.get(), bp);
            if (expr_eval.get_context().current_frame() && expr_eval.get_context().current_frame()->returned) return;
            if (!engine.scheduler.is_active()) return;
        }
        return;
    }

    if (auto vdecl = dynamic_cast<VarDeclStatementNode*>(stmt)) {
        if (expr_eval.get_context().has_active_frame()) {
            expr_eval.get_context().declare_local(vdecl->var_name, vdecl->width, vdecl->is_signed, vdecl->type_name);
            LocalVar* lv = expr_eval.get_context().lookup_local(vdecl->var_name);
            if (lv) {
                if (vdecl->is_queue) {
                    lv->is_queue = true;
                    lv->queue_data = QueueObject(vdecl->var_name, vdecl->width, vdecl->is_signed);
                }
                if (vdecl->is_dynamic_array) {
                    lv->is_dynamic_array = true;
                    lv->dyn_array_data = DynamicArrayObject(vdecl->var_name, vdecl->width, vdecl->is_signed);
                }
                if (vdecl->is_associative_array) {
                    lv->is_associative_array = true;
                    lv->assoc_array_data = AssociativeArrayObject(vdecl->var_name, vdecl->width, vdecl->is_signed, vdecl->assoc_key_type, vdecl->assoc_key_width);
                }
                if (vdecl->is_unpacked_array) {
                    lv->is_unpacked_array = true;
                    lv->elem_width = vdecl->elem_width;
                    lv->array_size = vdecl->array_size;
                    lv->arr_start_idx = vdecl->arr_start_idx;
                    lv->arr_end_idx = vdecl->arr_end_idx;
                    lv->unpacked_elements.assign(vdecl->array_size, LogicVal::make_x(vdecl->elem_width));
                }
                if (vdecl->is_class_handle) {
                    lv->is_class_handle = true;
                    lv->class_type_name = vdecl->class_type_name;
                    lv->value = LogicVal(64, 0, 0);
                }
                if (!vdecl->unpacked_dimensions.empty()) {
                    for (const auto& udim : vdecl->unpacked_dimensions) {
                        int64_t b1 = 0, b2 = 0;
                        if (udim.left_expr) b1 = eval_expr(udim.left_expr.get(), bp.scope).to_signed();
                        if (udim.right_expr) b2 = eval_expr(udim.right_expr.get(), bp.scope).to_signed();
                        if (udim.is_single) {
                            b2 = b1 > 0 ? (b1 - 1) : 0;
                            b1 = 0;
                        }
                        ArrayDimInfo info;
                        info.left = b1;
                        info.right = b2;
                        info.is_unpacked = true;
                        lv->dimensions.push_back(info);
                    }
                    lv->unpacked_dims = (uint32_t)lv->dimensions.size();
                if (!lv->is_unpacked_array && !vdecl->is_dynamic_array && !vdecl->is_queue && !vdecl->is_associative_array) {
                    lv->is_unpacked_array = true;
                    lv->elem_width = vdecl->width;
                    uint32_t tot = 1;
                    for (const auto& d : lv->dimensions) {
                        if (d.is_unpacked) tot *= (uint32_t)d.size();
                    }
                    lv->array_size = tot;
                    lv->arr_start_idx = lv->dimensions.empty() ? 0 : lv->dimensions[0].left;
                    lv->arr_end_idx = lv->dimensions.empty() ? 0 : lv->dimensions[0].right;
                    lv->unpacked_elements.assign(tot, LogicVal::make_x(lv->elem_width));
                }
                    if (!lv->is_unpacked_array && !vdecl->is_dynamic_array && !vdecl->is_queue && !vdecl->is_associative_array) {
                        lv->is_unpacked_array = true;
                        lv->elem_width = vdecl->width;
                        uint32_t tot = 1;
                        for (const auto& d : lv->dimensions) {
                            if (d.is_unpacked) tot *= (uint32_t)d.size();
                        }
                        lv->array_size = tot;
                        lv->arr_start_idx = lv->dimensions.empty() ? 0 : lv->dimensions[0].left;
                        lv->arr_end_idx = lv->dimensions.empty() ? 0 : lv->dimensions[0].right;
                        lv->unpacked_elements.assign(tot, LogicVal::make_x(lv->elem_width));
                    }
                }
                if (!vdecl->packed_dimensions.empty()) {
                    for (const auto& pdim : vdecl->packed_dimensions) {
                        int64_t b1 = 0, b2 = 0;
                        if (pdim.left_expr) b1 = eval_expr(pdim.left_expr.get(), bp.scope).to_signed();
                        if (pdim.right_expr) b2 = eval_expr(pdim.right_expr.get(), bp.scope).to_signed();
                        ArrayDimInfo info;
                        info.left = b1;
                        info.right = b2;
                        info.is_unpacked = false;
                        lv->dimensions.push_back(info);
                    }
                    lv->is_vector_or_array = true;
                } else if (!vdecl->type_name.empty()) {
                    const VariableDimMetadata* tmeta = Parser::find_type_dim_metadata(vdecl->type_name);
                    if (tmeta) {
                        for (const auto& d : tmeta->dimensions) {
                            if (d.is_unpacked) {
                                lv->dimensions.insert(lv->dimensions.begin() + lv->unpacked_dims, d);
                                lv->unpacked_dims++;
                            } else {
                                lv->dimensions.push_back(d);
                            }
                        }
                        lv->is_vector_or_array = true;
                    }
                } else if (vdecl->width > 1 && vdecl->type_name.empty()) {
                    lv->dimensions.push_back({(int64_t)(vdecl->width - 1), 0, false, false, false});
                    lv->is_vector_or_array = true;
                }
                if (!lv->dimensions.empty()) lv->is_vector_or_array = true;
                if (vdecl->is_chandle || vdecl->type_name == "chandle") {
                    lv->is_chandle = true;
                    lv->type_name = "chandle";
                    lv->value = LogicVal(64, 0, 0);
                }
                if (vdecl->is_semaphore || vdecl->type_name == "semaphore") {
                    lv->is_semaphore = true;
                    lv->type_name = "semaphore";
                    lv->value = LogicVal(64, 0, 0);
                }
                if (vdecl->is_mailbox || vdecl->type_name == "mailbox") {
                    lv->is_mailbox = true;
                    lv->type_name = "mailbox";
                    lv->value = LogicVal(64, 0, 0);
                }
                if (vdecl->type_name == "string") {
                    lv->type_name = "string";
                    lv->value.is_string = true;
                    lv->value.str_val = "";
                }
                if (vdecl->init_expr) {
                    if (vdecl->is_dynamic_array) {
                        auto elems = expr_eval.eval_array_expr(vdecl->init_expr.get(), bp.scope);
                        for (auto& el : elems) {
                            el = LogicOps::cast_width(el, lv->dyn_array_data.elem_width, lv->dyn_array_data.is_signed);
                        }
                        lv->dyn_array_data.elements = std::move(elems);
                    } else if (vdecl->is_unpacked_array) {
                        LogicVal init_val = eval_expr(vdecl->init_expr.get(), bp.scope);
                        std::vector<LogicVal> elems;
                        if (init_val.is_unpacked) {
                            elems = init_val.unpacked_elements;
                        } else {
                            elems = expr_eval.eval_array_expr(vdecl->init_expr.get(), bp.scope);
                        }
                        if (!elems.empty() || dynamic_cast<UnpackedConcatNode*>(vdecl->init_expr.get()) || dynamic_cast<ConcatNode*>(vdecl->init_expr.get())) {
                            if (elems.size() != lv->unpacked_elements.size()) {
                                throw std::runtime_error("[CAS-DTINPCIL] Concatenation element count (got " +
                                    std::to_string(elems.size()) + ") does not match target array size (" +
                                    std::to_string(lv->unpacked_elements.size()) + ") per IEEE 1800-2023 section 10.10");
                            }
                            for (size_t i = 0; i < lv->unpacked_elements.size(); ++i) {
                                lv->unpacked_elements[i] = LogicOps::cast_width(elems[i], lv->elem_width, lv->is_signed);
                            }
                        } else {
                            for (size_t i = 0; i < lv->unpacked_elements.size(); ++i) {
                                uint32_t ew = lv->elem_width;
                                uint32_t shift = (uint32_t)((lv->unpacked_elements.size() - 1 - i) * ew);
                                uint64_t mask = (ew >= 64) ? ~0ULL : ((1ULL << ew) - 1);
                                uint64_t a = (init_val.aval >> shift) & mask;
                                uint64_t b = (init_val.bval >> shift) & mask;
                                lv->unpacked_elements[i] = LogicVal(ew, a, b, lv->is_signed);
                            }
                        }
                        lv->value = init_val;
                    } else {
                        LogicVal init_val = eval_expr(vdecl->init_expr.get(), bp.scope);
                        if (vdecl->type_name == "string" || init_val.is_string) {
                            lv->value = init_val;
                        } else {
                            lv->value = LogicOps::cast_width(init_val, lv->width, lv->is_signed);
                        }
                    }
                }
            }
            return;
        }
        if (!bp.call_stack.empty()) {
            bp.call_stack.back().local_frame.declare_var(vdecl->var_name, vdecl->width, vdecl->is_signed, vdecl->type_name, vdecl->is_queue, vdecl->is_dynamic_array, vdecl->is_associative_array, vdecl->assoc_key_type, vdecl->assoc_key_width);
            LocalVar* lv = bp.call_stack.back().local_frame.get_var(vdecl->var_name);
            if (lv && vdecl->is_class_handle) {
                lv->is_class_handle = true;
                lv->class_type_name = vdecl->class_type_name;
                lv->value = LogicVal(64, 0, 0);
            }
            if (lv && !vdecl->unpacked_dimensions.empty()) {
                for (const auto& udim : vdecl->unpacked_dimensions) {
                    int64_t b1 = 0, b2 = 0;
                    if (udim.left_expr) b1 = eval_expr(udim.left_expr.get(), bp.scope).to_signed();
                    if (udim.right_expr) b2 = eval_expr(udim.right_expr.get(), bp.scope).to_signed();
                    if (udim.is_single) {
                        b2 = b1 > 0 ? (b1 - 1) : 0;
                        b1 = 0;
                    }
                    ArrayDimInfo info;
                    info.left = b1;
                    info.right = b2;
                    info.is_unpacked = true;
                    lv->dimensions.push_back(info);
                }
                lv->unpacked_dims = (uint32_t)lv->dimensions.size();
                if (!lv->is_unpacked_array && !vdecl->is_dynamic_array && !vdecl->is_queue && !vdecl->is_associative_array) {
                    lv->is_unpacked_array = true;
                    lv->elem_width = vdecl->width;
                    uint32_t tot = 1;
                    for (const auto& d : lv->dimensions) {
                        if (d.is_unpacked) tot *= (uint32_t)d.size();
                    }
                    lv->array_size = tot;
                    lv->arr_start_idx = lv->dimensions.empty() ? 0 : lv->dimensions[0].left;
                    lv->arr_end_idx = lv->dimensions.empty() ? 0 : lv->dimensions[0].right;
                    lv->unpacked_elements.assign(tot, LogicVal::make_x(lv->elem_width));
                }
            }
            if (lv && !vdecl->packed_dimensions.empty()) {
                for (const auto& pdim : vdecl->packed_dimensions) {
                    int64_t b1 = 0, b2 = 0;
                    if (pdim.left_expr) b1 = eval_expr(pdim.left_expr.get(), bp.scope).to_signed();
                    if (pdim.right_expr) b2 = eval_expr(pdim.right_expr.get(), bp.scope).to_signed();
                    lv->dimensions.push_back({b1, b2, false, false, false});
                }
                lv->is_vector_or_array = true;
            } else if (lv && !vdecl->type_name.empty()) {
                const VariableDimMetadata* tmeta = Parser::find_type_dim_metadata(vdecl->type_name);
                if (tmeta) {
                    for (const auto& d : tmeta->dimensions) {
                        if (d.is_unpacked) {
                            lv->dimensions.insert(lv->dimensions.begin() + lv->unpacked_dims, d);
                            lv->unpacked_dims++;
                        } else {
                            lv->dimensions.push_back(d);
                        }
                    }
                    lv->is_vector_or_array = true;
                }
            } else if (lv && vdecl->width > 1 && vdecl->type_name.empty()) {
                lv->dimensions.push_back({(int64_t)(vdecl->width - 1), 0, false, false, false});
                lv->is_vector_or_array = true;
            }
            if (lv && !lv->dimensions.empty()) lv->is_vector_or_array = true;
            if (lv && (vdecl->is_chandle || vdecl->type_name == "chandle")) {
                lv->is_chandle = true;
                lv->type_name = "chandle";
                lv->value = LogicVal(64, 0, 0);
            }
            if (lv && (vdecl->is_semaphore || vdecl->type_name == "semaphore")) {
                lv->is_semaphore = true;
                lv->type_name = "semaphore";
                lv->value = LogicVal(64, 0, 0);
            }
            if (lv && (vdecl->is_mailbox || vdecl->type_name == "mailbox")) {
                lv->is_mailbox = true;
                lv->type_name = "mailbox";
                lv->value = LogicVal(64, 0, 0);
            }
            if (lv && vdecl->type_name == "string") {
                lv->type_name = "string";
                lv->value.is_string = true;
                lv->value.str_val = "";
            }
            if (lv && vdecl->is_unpacked_array) {
                lv->is_unpacked_array = true;
                lv->elem_width = vdecl->elem_width;
                lv->array_size = vdecl->array_size;
                lv->arr_start_idx = vdecl->arr_start_idx;
                lv->arr_end_idx = vdecl->arr_end_idx;
                lv->unpacked_elements.assign(vdecl->array_size, LogicVal::make_x(vdecl->elem_width));
            }
            if (vdecl->init_expr) {
                if (lv) {
                    if (vdecl->is_dynamic_array) {
                        auto elems = expr_eval.eval_array_expr(vdecl->init_expr.get(), bp.scope);
                        for (auto& el : elems) {
                            el = LogicOps::cast_width(el, lv->dyn_array_data.elem_width, lv->dyn_array_data.is_signed);
                        }
                        lv->dyn_array_data.elements = std::move(elems);
                    } else if (vdecl->is_unpacked_array) {
                        LogicVal init_val = eval_expr(vdecl->init_expr.get(), bp.scope);
                        std::vector<LogicVal> elems;
                        if (init_val.is_unpacked) {
                            elems = init_val.unpacked_elements;
                        } else {
                            elems = expr_eval.eval_array_expr(vdecl->init_expr.get(), bp.scope);
                        }
                        if (!elems.empty() || dynamic_cast<UnpackedConcatNode*>(vdecl->init_expr.get()) || dynamic_cast<ConcatNode*>(vdecl->init_expr.get())) {
                            if (elems.size() != lv->unpacked_elements.size()) {
                                throw std::runtime_error("[CAS-DTINPCIL] Concatenation element count (got " +
                                    std::to_string(elems.size()) + ") does not match target array size (" +
                                    std::to_string(lv->unpacked_elements.size()) + ") per IEEE 1800-2023 section 10.10");
                            }
                            for (size_t i = 0; i < lv->unpacked_elements.size(); ++i) {
                                lv->unpacked_elements[i] = LogicOps::cast_width(elems[i], lv->elem_width, lv->is_signed);
                            }
                        } else {
                            for (size_t i = 0; i < lv->unpacked_elements.size(); ++i) {
                                uint32_t ew = lv->elem_width;
                                uint32_t shift = (uint32_t)((lv->unpacked_elements.size() - 1 - i) * ew);
                                uint64_t mask = (ew >= 64) ? ~0ULL : ((1ULL << ew) - 1);
                                uint64_t a = (init_val.aval >> shift) & mask;
                                uint64_t b = (init_val.bval >> shift) & mask;
                                lv->unpacked_elements[i] = LogicVal(ew, a, b, lv->is_signed);
                            }
                        }
                        lv->value = init_val;
                    } else {
                        LogicVal init_val = eval_expr(vdecl->init_expr.get(), bp.scope);
                        if (vdecl->type_name == "string" || init_val.is_string) {
                            lv->value = init_val;
                        } else {
                            lv->value = LogicOps::cast_width(init_val, lv->width, lv->is_signed);
                        }
                    }
                }
            }
            return;
        }
        expr_eval.get_context().push_frame(bp.scope);
        expr_eval.get_context().declare_local(vdecl->var_name, vdecl->width, vdecl->is_signed, vdecl->type_name);
        LocalVar* lv = expr_eval.get_context().lookup_local(vdecl->var_name);
        if (lv) {
            if (vdecl->is_queue) {
                lv->is_queue = true;
                lv->queue_data = QueueObject(vdecl->var_name, vdecl->width, vdecl->is_signed);
            }
            if (vdecl->is_dynamic_array) {
                lv->is_dynamic_array = true;
                lv->dyn_array_data = DynamicArrayObject(vdecl->var_name, vdecl->width, vdecl->is_signed);
            }
            if (vdecl->is_associative_array) {
                lv->is_associative_array = true;
                lv->assoc_array_data = AssociativeArrayObject(vdecl->var_name, vdecl->width, vdecl->is_signed, vdecl->assoc_key_type, vdecl->assoc_key_width);
            }
            if (vdecl->is_unpacked_array) {
                lv->is_unpacked_array = true;
                lv->elem_width = vdecl->elem_width;
                lv->array_size = vdecl->array_size;
                lv->arr_start_idx = vdecl->arr_start_idx;
                lv->arr_end_idx = vdecl->arr_end_idx;
                lv->unpacked_elements.assign(vdecl->array_size, LogicVal::make_x(vdecl->elem_width));
            }
            if (vdecl->is_class_handle) {
                lv->is_class_handle = true;
                lv->class_type_name = vdecl->class_type_name;
                lv->value = LogicVal(64, 0, 0);
            }
            if (vdecl->is_chandle || vdecl->type_name == "chandle") {
                lv->is_chandle = true;
                lv->type_name = "chandle";
                lv->value = LogicVal(64, 0, 0);
            }
            if (vdecl->is_semaphore || vdecl->type_name == "semaphore") {
                lv->is_semaphore = true;
                lv->type_name = "semaphore";
                lv->value = LogicVal(64, 0, 0);
            }
            if (vdecl->is_mailbox || vdecl->type_name == "mailbox") {
                lv->is_mailbox = true;
                lv->type_name = "mailbox";
                lv->value = LogicVal(64, 0, 0);
            }
            if (vdecl->type_name == "string") {
                lv->type_name = "string";
                lv->value.is_string = true;
                lv->value.str_val = "";
            }
            if (!vdecl->unpacked_dimensions.empty()) {
                for (const auto& udim : vdecl->unpacked_dimensions) {
                    int64_t b1 = 0, b2 = 0;
                    if (udim.left_expr) b1 = eval_expr(udim.left_expr.get(), bp.scope).to_signed();
                    if (udim.right_expr) b2 = eval_expr(udim.right_expr.get(), bp.scope).to_signed();
                    if (udim.is_single) {
                        b2 = b1 > 0 ? (b1 - 1) : 0;
                        b1 = 0;
                    }
                    ArrayDimInfo info;
                    info.left = b1;
                    info.right = b2;
                    info.is_unpacked = true;
                    lv->dimensions.push_back(info);
                }
                lv->unpacked_dims = (uint32_t)lv->dimensions.size();
                if (!lv->is_unpacked_array && !vdecl->is_dynamic_array && !vdecl->is_queue && !vdecl->is_associative_array) {
                    lv->is_unpacked_array = true;
                    lv->elem_width = vdecl->width;
                    uint32_t tot = 1;
                    for (const auto& d : lv->dimensions) {
                        if (d.is_unpacked) tot *= (uint32_t)d.size();
                    }
                    lv->array_size = tot;
                    lv->arr_start_idx = lv->dimensions.empty() ? 0 : lv->dimensions[0].left;
                    lv->arr_end_idx = lv->dimensions.empty() ? 0 : lv->dimensions[0].right;
                    lv->unpacked_elements.assign(tot, LogicVal::make_x(lv->elem_width));
                }
            }
            if (!vdecl->packed_dimensions.empty()) {
                for (const auto& pdim : vdecl->packed_dimensions) {
                    int64_t b1 = 0, b2 = 0;
                    if (pdim.left_expr) b1 = eval_expr(pdim.left_expr.get(), bp.scope).to_signed();
                    if (pdim.right_expr) b2 = eval_expr(pdim.right_expr.get(), bp.scope).to_signed();
                    lv->dimensions.push_back({b1, b2, false, false, false});
                }
                lv->is_vector_or_array = true;
            } else if (!vdecl->type_name.empty()) {
                const VariableDimMetadata* tmeta = Parser::find_type_dim_metadata(vdecl->type_name);
                if (tmeta) {
                    for (const auto& d : tmeta->dimensions) {
                        if (d.is_unpacked) {
                            lv->dimensions.insert(lv->dimensions.begin() + lv->unpacked_dims, d);
                            lv->unpacked_dims++;
                        } else {
                            lv->dimensions.push_back(d);
                        }
                    }
                    lv->is_vector_or_array = true;
                }
            } else if (vdecl->width > 1 && vdecl->type_name.empty()) {
                lv->dimensions.push_back({(int64_t)(vdecl->width - 1), 0, false, false, false});
                lv->is_vector_or_array = true;
            }
            if (!lv->dimensions.empty()) lv->is_vector_or_array = true;
            if (vdecl->init_expr) {
                if (vdecl->is_dynamic_array) {
                    auto elems = expr_eval.eval_array_expr(vdecl->init_expr.get(), bp.scope);
                    for (auto& el : elems) {
                        el = LogicOps::cast_width(el, lv->dyn_array_data.elem_width, lv->dyn_array_data.is_signed);
                    }
                    lv->dyn_array_data.elements = std::move(elems);
                } else if (vdecl->is_unpacked_array) {
                    LogicVal init_val = eval_expr(vdecl->init_expr.get(), bp.scope);
                    std::vector<LogicVal> elems;
                    if (init_val.is_unpacked) {
                        elems = init_val.unpacked_elements;
                    } else {
                        elems = expr_eval.eval_array_expr(vdecl->init_expr.get(), bp.scope);
                    }
                    if (!elems.empty() || dynamic_cast<UnpackedConcatNode*>(vdecl->init_expr.get()) || dynamic_cast<ConcatNode*>(vdecl->init_expr.get())) {
                        if (elems.size() != lv->unpacked_elements.size()) {
                            throw std::runtime_error("[CAS-DTINPCIL] Concatenation element count (got " +
                                std::to_string(elems.size()) + ") does not match target array size (" +
                                std::to_string(lv->unpacked_elements.size()) + ") per IEEE 1800-2023 section 10.10");
                        }
                        for (size_t i = 0; i < lv->unpacked_elements.size(); ++i) {
                            lv->unpacked_elements[i] = LogicOps::cast_width(elems[i], lv->elem_width, lv->is_signed);
                        }
                    } else {
                        for (size_t i = 0; i < lv->unpacked_elements.size(); ++i) {
                            uint32_t ew = lv->elem_width;
                            uint32_t shift = (uint32_t)((lv->unpacked_elements.size() - 1 - i) * ew);
                            uint64_t mask = (ew >= 64) ? ~0ULL : ((1ULL << ew) - 1);
                            uint64_t a = (init_val.aval >> shift) & mask;
                            uint64_t b = (init_val.bval >> shift) & mask;
                            lv->unpacked_elements[i] = LogicVal(ew, a, b, lv->is_signed);
                        }
                    }
                    lv->value = init_val;
                } else {
                    LogicVal init_val = eval_expr(vdecl->init_expr.get(), bp.scope);
                    if (vdecl->type_name == "string" || init_val.is_string) {
                        lv->value = init_val;
                    } else {
                        lv->value = LogicOps::cast_width(init_val, lv->width, lv->is_signed);
                    }
                }
            }
        }
        return;
    }

    if (auto for_stmt = dynamic_cast<ForStatementNode*>(stmt)) {
        expr_eval.get_context().push_frame(bp.scope + ".for");
        if (for_stmt->init_stmt) execute_statement(for_stmt->init_stmt.get(), bp);
        uint64_t for_iters = 0;
        while (eval_expr(for_stmt->condition.get(), bp.scope).to_bool()) {
            for (auto& s : for_stmt->body) execute_statement(s.get(), bp);
            if (for_stmt->step_stmt) execute_statement(for_stmt->step_stmt.get(), bp);
            for_iters++;
            if (for_iters > 100000) {
                std::cout << "[ERROR FOR LOOP] for loop in " << bp.scope << " exceeded 100000 iters!\n" << std::flush;
                break;
            }
        }
        expr_eval.get_context().pop_frame();
        return;
    }

    if (auto fe_stmt = dynamic_cast<ForeachStatementNode*>(stmt)) {
        DynamicArrayObject* da = nullptr;
        if (expr_eval.get_context().has_active_frame()) {
            LocalVar* local = expr_eval.get_context().lookup_local(fe_stmt->array_name);
            if (local && local->is_dynamic_array) da = &local->dyn_array_data;
        }
        if (!da && !bp.call_stack.empty()) {
            for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                LocalVar* local = it->local_frame.get_var(fe_stmt->array_name);
                if (local && local->is_dynamic_array) {
                    da = &local->dyn_array_data;
                    break;
                }
            }
        }
        if (!da && expr_eval.get_current_this() && expr_eval.get_current_this()->has_dynamic_array(fe_stmt->array_name)) {
            da = expr_eval.get_current_this()->get_dynamic_array(fe_stmt->array_name);
        }
        if (!da) {
            da = engine.get_dynamic_array(fe_stmt->array_name, bp.scope);
        }
        size_t n = da ? da->size() : 0;
        if (expr_eval.get_context().has_active_frame()) {
            if (!expr_eval.get_context().lookup_local(fe_stmt->loop_var)) {
                expr_eval.get_context().declare_local(fe_stmt->loop_var, 32, true);
            }
        }
        for (size_t i = 0; i < n; ++i) {
            if (expr_eval.get_context().has_active_frame()) {
                expr_eval.get_context().set_local_value(fe_stmt->loop_var, LogicVal::make_known(32, (uint64_t)i));
            }
            for (auto& s : fe_stmt->body) {
                if (expr_eval.get_context().current_frame() && expr_eval.get_context().current_frame()->returned) break;
                execute_statement(s.get(), bp);
            }
            if (expr_eval.get_context().current_frame() && expr_eval.get_context().current_frame()->returned) break;
        }
        return;
    }

    if (auto while_stmt = dynamic_cast<WhileStatementNode*>(stmt)) {
        uint64_t while_iters = 0;
        while (eval_expr(while_stmt->condition.get(), bp.scope).to_bool()) {
            for (auto& s : while_stmt->body) execute_statement(s.get(), bp);
            while_iters++;
            if (while_iters > 100000) {
                std::cout << "[ERROR WHILE LOOP] while loop in " << bp.scope << " exceeded 100000 iters!\n" << std::flush;
                break;
            }
        }
        return;
    }

    if (auto rep_stmt = dynamic_cast<RepeatStatementNode*>(stmt)) {
        LogicVal count_val = eval_expr(rep_stmt->count_expr.get(), bp.scope);
        uint64_t count = count_val.is_unknown() ? 0 : count_val.aval;
        for (uint64_t i = 0; i < count; ++i) {
            for (auto& s : rep_stmt->body) execute_statement(s.get(), bp);
        }
        return;
    }

    if (auto fv_stmt = dynamic_cast<ForeverStatementNode*>(stmt)) {
        while (engine.scheduler.is_active() && !bp.completed && !bp.is_delayed && bp.waiting_event_id == (size_t)-1 && !bp.is_waiting_sync && !bp.is_waiting_cycle) {
            for (auto& s : fv_stmt->body) {
                execute_statement(s.get(), bp);
                if (!engine.scheduler.is_active() || bp.completed || bp.is_delayed || bp.waiting_event_id != (size_t)-1 || bp.is_waiting_sync || bp.is_waiting_cycle) break;
            }
        }
        return;
    }

    if (auto if_stmt = dynamic_cast<IfStatementNode*>(stmt)) {
        if (if_stmt->modifier == DecisionModifier::NONE) {
            LogicVal cond = eval_expr(if_stmt->condition.get(), bp.scope);
            if (cond.to_bool()) {
                for (auto& s : if_stmt->then_branch) {
                    if (expr_eval.get_context().current_frame() && expr_eval.get_context().current_frame()->returned) return;
                    execute_statement(s.get(), bp);
                    if (expr_eval.get_context().current_frame() && expr_eval.get_context().current_frame()->returned) return;
                }
            } else {
                for (auto& s : if_stmt->else_branch) {
                    if (expr_eval.get_context().current_frame() && expr_eval.get_context().current_frame()->returned) return;
                    execute_statement(s.get(), bp);
                    if (expr_eval.get_context().current_frame() && expr_eval.get_context().current_frame()->returned) return;
                }
            }
            return;
        }

        // Handle unique, unique0, priority if chain (§12.5)
        struct IfArm {
            ExpressionNode* cond;
            const std::vector<std::unique_ptr<StatementNode>>* body;
        };
        std::vector<IfArm> arms;
        arms.push_back({ if_stmt->condition.get(), &if_stmt->then_branch });

        const IfStatementNode* cur = if_stmt;
        const std::vector<std::unique_ptr<StatementNode>>* final_else = nullptr;

        while (cur->else_branch.size() == 1) {
            auto next_if = dynamic_cast<const IfStatementNode*>(cur->else_branch[0].get());
            if (next_if && next_if->modifier == DecisionModifier::NONE) {
                arms.push_back({ next_if->condition.get(), &next_if->then_branch });
                cur = next_if;
            } else {
                break;
            }
        }
        if (!cur->else_branch.empty()) {
            final_else = &cur->else_branch;
        }

        std::string loc_file = !if_stmt->src_file.empty() ? if_stmt->src_file : "unknown";
        size_t loc_line = if_stmt->src_line;

        if (if_stmt->modifier == DecisionModifier::PRIORITY) {
            int first_match = -1;
            bool any_cond_unknown = false;
            for (size_t i = 0; i < arms.size(); ++i) {
                LogicVal cv = eval_expr(arms[i].cond, bp.scope);
                if (cv.is_unknown()) any_cond_unknown = true;
                if (cv.to_bool()) {
                    first_match = (int)i;
                    break;
                }
            }
            if (first_match >= 0) {
                for (auto& s : *arms[first_match].body) {
                    execute_statement(s.get(), bp);
                    if (!engine.scheduler.is_active()) return;
                }
            } else if (final_else) {
                for (auto& s : *final_else) {
                    execute_statement(s.get(), bp);
                    if (!engine.scheduler.is_active()) return;
                }
            } else if (!any_cond_unknown) {
                std::cout << "\"" << loc_file << "\", " << loc_line << ": RT-VIOLATION: priority if/case condition failed to match\n";
            }
            return;
        }

        // UNIQUE or UNIQUE0
        int match_count = 0;
        int first_match = -1;
        bool any_cond_unknown = false;
        for (size_t i = 0; i < arms.size(); ++i) {
            LogicVal cv = eval_expr(arms[i].cond, bp.scope);
            if (cv.is_unknown()) any_cond_unknown = true;
            if (cv.to_bool()) {
                match_count++;
                if (first_match < 0) first_match = (int)i;
            }
        }

        if (!any_cond_unknown) {
            if (match_count > 1) {
                std::cout << "\"" << loc_file << "\", " << loc_line << ": RT-VIOLATION: unique if/case multiple conditions matched simultaneously\n";
            } else if (match_count == 0 && if_stmt->modifier == DecisionModifier::UNIQUE && !final_else) {
                std::cout << "\"" << loc_file << "\", " << loc_line << ": RT-VIOLATION: unique if/case condition failed to match\n";
            }
        }

        if (first_match >= 0) {
            for (auto& s : *arms[first_match].body) {
                execute_statement(s.get(), bp);
                if (!engine.scheduler.is_active()) return;
            }
        } else if (final_else) {
            for (auto& s : *final_else) {
                execute_statement(s.get(), bp);
                if (!engine.scheduler.is_active()) return;
            }
        }
        return;
    }

    if (auto case_stmt = dynamic_cast<CaseStatementNode*>(stmt)) {
        LogicVal sel_val = eval_expr(case_stmt->selector.get(), bp.scope);
        const CaseItem* default_item = nullptr;

        auto test_match = [&](ExpressionNode* expr) -> bool {
            if (!expr) return false;
            LogicVal item_val = eval_expr(expr, bp.scope);
            if (case_stmt->case_type == CaseType::CASEZ) {
                return LogicOps::casez_eq(sel_val, item_val).to_bool();
            } else if (case_stmt->case_type == CaseType::CASEX) {
                return LogicOps::casex_eq(sel_val, item_val).to_bool();
            } else {
                return LogicOps::case_eq(sel_val, item_val).to_bool();
            }
        };

        if (case_stmt->modifier == DecisionModifier::NONE) {
            bool branch_taken = false;
            for (const auto& item : case_stmt->items) {
                if (item.is_default) {
                    default_item = &item;
                    continue;
                }

                bool item_matched = false;
                if (!item.match_exprs.empty()) {
                    for (const auto& m_expr : item.match_exprs) {
                        if (test_match(m_expr.get())) {
                            item_matched = true;
                            break;
                        }
                    }
                } else if (item.match_expr) {
                    item_matched = test_match(item.match_expr.get());
                }

                if (item_matched) {
                    for (auto& s : item.statements) {
                        execute_statement(s.get(), bp);
                        if (!engine.scheduler.is_active()) return;
                    }
                    branch_taken = true;
                    break;
                }
            }
            if (!branch_taken && default_item) {
                for (auto& s : default_item->statements) {
                    execute_statement(s.get(), bp);
                    if (!engine.scheduler.is_active()) return;
                }
            }
            return;
        }

        // Modified case: PRIORITY, UNIQUE, UNIQUE0
        std::string loc_file = !case_stmt->src_file.empty() ? case_stmt->src_file : "unknown";
        size_t loc_line = case_stmt->src_line;

        const CaseItem* first_matched_item = nullptr;
        int match_count = 0;

        for (const auto& item : case_stmt->items) {
            if (item.is_default) {
                default_item = &item;
                continue;
            }

            bool item_matched = false;
            if (!item.match_exprs.empty()) {
                for (const auto& m_expr : item.match_exprs) {
                    if (test_match(m_expr.get())) {
                        item_matched = true;
                        break;
                    }
                }
            } else if (item.match_expr) {
                item_matched = test_match(item.match_expr.get());
            }

            if (item_matched) {
                match_count++;
                if (!first_matched_item) {
                    first_matched_item = &item;
                }
            }
        }

        if (!sel_val.is_unknown()) {
            if (case_stmt->modifier == DecisionModifier::PRIORITY) {
                if (match_count == 0 && !default_item) {
                    std::cout << "\"" << loc_file << "\", " << loc_line << ": RT-VIOLATION: priority if/case condition failed to match\n";
                }
            } else {
                // UNIQUE or UNIQUE0
                if (match_count > 1) {
                    std::cout << "\"" << loc_file << "\", " << loc_line << ": RT-VIOLATION: unique if/case multiple conditions matched simultaneously\n";
                } else if (match_count == 0 && case_stmt->modifier == DecisionModifier::UNIQUE && !default_item) {
                    std::cout << "\"" << loc_file << "\", " << loc_line << ": RT-VIOLATION: unique if/case condition failed to match\n";
                }
            }
        }

        if (first_matched_item) {
            for (auto& s : first_matched_item->statements) {
                execute_statement(s.get(), bp);
                if (!engine.scheduler.is_active()) return;
            }
        } else if (default_item) {
            for (auto& s : default_item->statements) {
                execute_statement(s.get(), bp);
                if (!engine.scheduler.is_active()) return;
            }
        }
        return;
    }

    if (auto sc = dynamic_cast<SysCastStatementNode*>(stmt)) {
        SysCastCallNode call(sc->dest_expr ? sc->dest_expr->clone() : nullptr,
                             sc->src_expr ? sc->src_expr->clone() : nullptr);
        LogicVal res = expr_eval.eval(&call, bp.scope);
        if (res.aval == 0) {
            throw std::runtime_error("[CAS-CAST-ERR] Runtime error: $cast failed (incompatible class types or invalid handle)");
        }
        return;
    }

    if (auto vc = dynamic_cast<VoidCastStatementNode*>(stmt)) {
        if (vc->expr) {
            eval_expr(vc->expr.get(), bp.scope);
        }
        return;
    }

    if (auto tcall = dynamic_cast<TaskCallNode*>(stmt)) {
        std::string tname = tcall->task_name;
        auto colons = tname.find("::");
        if (colons != std::string::npos && tcall->class_type_name.empty()) {
            tcall->class_type_name = tname.substr(0, colons);
            tcall->task_name = tname.substr(colons + 2);
        }

        if (!tcall->class_type_name.empty()) {
            const ClassNode* cls = engine.get_class_def(tcall->class_type_name);
            if (cls) {
                TaskNode* task_node = cls->get_task(tcall->task_name);
                FunctionNode* func_node = cls->get_function(tcall->task_name);
                if (func_node) {
                    std::vector<LogicVal> arg_vals;
                    for (auto& a : tcall->args) arg_vals.push_back(expr_eval.eval(a.get(), bp.scope));
                    LogicVal dummy;
                    expr_eval.eval_static_class_method(tcall->class_type_name, func_node, arg_vals, dummy);
                    return;
                }
                if (task_node) {
                    const ClassNode* prev_cd = expr_eval.get_current_class_def();
                    expr_eval.set_current_class_def(cls);
                    std::string frame_name = tcall->class_type_name + "::" + task_node->name + "__call";
                    expr_eval.get_context().push_frame(frame_name);
                    for (size_t i = 0; i < task_node->ports.size(); ++i) {
                        const auto& p = task_node->ports[i];
                        uint32_t w = 32;
                        if (p.msb_expr && p.lsb_expr) {
                            auto msb_n = dynamic_cast<NumberNode*>(p.msb_expr.get());
                            auto lsb_n = dynamic_cast<NumberNode*>(p.lsb_expr.get());
                            if (msb_n && lsb_n) {
                                w = (msb_n->aval >= lsb_n->aval) ? (uint32_t)(msb_n->aval - lsb_n->aval + 1) : (uint32_t)(lsb_n->aval - msb_n->aval + 1);
                            }
                        }
                        expr_eval.get_context().declare_local(p.name, w, p.is_signed);
                        if (i < tcall->args.size()) {
                            LogicVal arg_v = eval_expr(tcall->args[i].get(), bp.scope);
                            expr_eval.get_context().set_local_value(p.name, arg_v);
                        } else if (p.default_value) {
                            LogicVal def_v = eval_expr(p.default_value.get(), bp.scope);
                            expr_eval.get_context().set_local_value(p.name, def_v);
                        }
                    }
                    for (auto& s : task_node->get_body()) {
                        if (expr_eval.get_context().current_frame() && expr_eval.get_context().current_frame()->returned) break;
                        execute_statement(s.get(), bp);
                    }
                    expr_eval.get_context().pop_frame();
                    expr_eval.set_current_class_def(prev_cd);
                    return;
                }
            }
        }

        if (tcall->task_name == "$finish" || tcall->task_name == "$stop") {
            if (engine.is_finish_msg_allowed()) {
                std::cout << "[CAS $finish] Simulation Terminated at time " << engine.get_sim_time() << ".\n";
            }
            engine.scheduler.terminate();
            bp.completed = true;
            return;
        }

        if (tcall->task_name == "$readmemh" || tcall->task_name == "$readmemb") {
            if (tcall->args.size() < 2) return;
            bool is_hex = (tcall->task_name == "$readmemh");
            LogicVal fn_val = eval_expr(tcall->args[0].get(), bp.scope);
            std::string filename = fn_val.is_string ? fn_val.str_val : "";
            if (filename.empty()) return;

            std::string mem_name = "";
            if (auto id = dynamic_cast<IdentifierNode*>(tcall->args[1].get())) {
                mem_name = id->name;
            }

            MemoryArray* target_mem = nullptr;
            std::string cur_s = bp.scope;
            while (!cur_s.empty()) {
                std::string scoped_m = cur_s + "." + mem_name;
                auto it = engine.memory_map.find(scoped_m);
                if (it != engine.memory_map.end()) {
                    target_mem = &it->second;
                    break;
                }
                auto dot_pos = cur_s.rfind('.');
                if (dot_pos == std::string::npos) cur_s = "";
                else cur_s = cur_s.substr(0, dot_pos);
            }
            if (!target_mem) {
                auto it = engine.memory_map.find(mem_name);
                if (it != engine.memory_map.end()) {
                    target_mem = &it->second;
                }
            }
            if (!target_mem) {
                for (auto& kv : engine.memory_map) {
                    if (kv.first.size() >= mem_name.size() &&
                        kv.first.substr(kv.first.size() - mem_name.size()) == mem_name) {
                        target_mem = &kv.second;
                        break;
                    }
                }
            }

            if (!target_mem) return;

            std::ifstream ifs(filename);
            if (!ifs.is_open()) {
                for (const auto& sp : engine.source_search_paths) {
                    std::string alt = sp + "/" + filename;
                    ifs.open(alt);
                    if (ifs.is_open()) {
                        filename = alt;
                        break;
                    }
                }
            }
            if (!ifs.is_open()) return;

            int64_t start_addr = target_mem->start_idx;
            int64_t cur_addr = start_addr;
            std::string token;
            while (ifs >> token) {
                if (token.rfind("//", 0) == 0) {
                    std::string dummy;
                    std::getline(ifs, dummy);
                    continue;
                }
                if (token.rfind("/*", 0) == 0) {
                    while (token.find("*/") == std::string::npos && (ifs >> token)) {}
                    continue;
                }
                if (token.front() == '@') {
                    std::string addr_hex = token.substr(1);
                    cur_addr = std::stoull(addr_hex, nullptr, 16);
                    continue;
                }
                uint64_t aval = 0;
                uint64_t bval = 0;
                int bits_per_digit = is_hex ? 4 : 1;
                for (char ch : token) {
                    if (ch == '_') continue;
                    aval <<= bits_per_digit;
                    bval <<= bits_per_digit;
                    if (ch == 'x' || ch == 'X') {
                        aval |= (bits_per_digit == 4 ? 0xF : 1);
                        bval |= (bits_per_digit == 4 ? 0xF : 1);
                    } else if (ch == 'z' || ch == 'Z' || ch == '?') {
                        bval |= (bits_per_digit == 4 ? 0xF : 1);
                    } else {
                        int d = 0;
                        if (ch >= '0' && ch <= '9') d = ch - '0';
                        else if (ch >= 'a' && ch <= 'f') d = 10 + (ch - 'a');
                        else if (ch >= 'A' && ch <= 'F') d = 10 + (ch - 'A');
                        aval |= (d & ((1 << bits_per_digit) - 1));
                    }
                }
                LogicVal word_val(target_mem->word_width, aval, bval);
                target_mem->set_word(cur_addr++, word_val);
            }
            return;
        }

        if (tcall->task_name == "$fwrite" || tcall->task_name == "$fdisplay") {
            if (tcall->args.empty()) return;
            LogicVal fd_val = eval_expr(tcall->args[0].get(), bp.scope);
            int fd = (int)fd_val.aval;
            bool is_fdisp = (tcall->task_name == "$fdisplay");
            
            std::ostream* out_stream = nullptr;
            if (fd == 1) {
                out_stream = &std::cout;
            }
            auto it = engine.open_files.find(fd);
            if (it != engine.open_files.end() && it->second.stream) {
                out_stream = it->second.stream.get();
            }

            std::string fmt = "";
            size_t arg_start = 1;
            if (tcall->args.size() > 1) {
                LogicVal fmt_val = eval_expr(tcall->args[1].get(), bp.scope);
                if (fmt_val.is_string) {
                    fmt = fmt_val.str_val;
                    arg_start = 2;
                }
            }

            std::stringstream ss;
            size_t arg_idx = arg_start;
            size_t fmt_len = fmt.size();
            for (size_t i = 0; i < fmt_len; ++i) {
                if (fmt[i] == '%' && i + 1 < fmt_len) {
                    ++i;
                    if (fmt[i] == '%') {
                        ss << '%';
                        continue;
                    }
                    bool pad_zero = false;
                    int width = 0;
                    if (fmt[i] == '0') {
                        pad_zero = true;
                        ++i;
                    }
                    while (i < fmt_len && std::isdigit((unsigned char)fmt[i])) {
                        width = width * 10 + (fmt[i] - '0');
                        ++i;
                    }
                    char spec = (i < fmt_len) ? fmt[i] : ' ';
                    if (spec == 'm') {
                        ss << bp.scope;
                        continue;
                    }
                    if (arg_idx < tcall->args.size()) {
                        LogicVal val = eval_expr(tcall->args[arg_idx++].get(), bp.scope);
                        if (spec == 'c') {
                            char ch = static_cast<char>(val.aval & 0xFF);
                            ss << ch;
                        } else if (spec == 's') {
                            if (val.is_string) ss << val.str_val;
                            else ss << val.aval;
                        } else if (val.is_unknown() && (spec == 'd' || spec == 't')) {
                            ss << "x";
                        } else if (spec == 'd' || spec == 't') {
                            if (val.is_real) ss << (int64_t)val.real_val;
                            else if (val.is_signed) ss << val.to_signed();
                            else ss << val.aval;
                        } else if (spec == 'h' || spec == 'x') {
                            std::string hx = val.to_hex_string(true);
                            if (pad_zero && width > 0) {
                                while (hx.size() < (size_t)width) hx = "0" + hx;
                            }
                            ss << hx;
                        } else {
                            ss << val.aval;
                        }
                    }
                } else {
                    ss << fmt[i];
                }
            }
            while (arg_idx < tcall->args.size()) {
                LogicVal val = eval_expr(tcall->args[arg_idx++].get(), bp.scope);
                if (val.is_string) ss << val.str_val;
                else ss << val.aval;
                arg_idx++;
            }
            if (is_fdisp) ss << "\n";

            std::string out_str = ss.str();
            if (out_stream) {
                *out_stream << out_str;
            }
            if (out_stream != &std::cout) {
                std::cout << out_str;
            }
            return;
        }

        if (tcall->task_name == "$fflush") {
            if (!tcall->args.empty()) {
                LogicVal fd_val = eval_expr(tcall->args[0].get(), bp.scope);
                int fd = (int)fd_val.aval;
                auto it = engine.open_files.find(fd);
                if (it != engine.open_files.end() && it->second.stream) {
                    it->second.stream->flush();
                }
                std::cout.flush();
            } else {
                std::cout.flush();
            }
            return;
        }

        if (tcall->task_name == "$fclose") {
            if (!tcall->args.empty()) {
                LogicVal fd_val = eval_expr(tcall->args[0].get(), bp.scope);
                int fd = (int)fd_val.aval;
                auto it = engine.open_files.find(fd);
                if (it != engine.open_files.end()) {
                    if (it->second.stream) it->second.stream->close();
                    engine.open_files.erase(it);
                }
            }
            return;
        }

        auto dot = tname.rfind('.');
        if (dot != std::string::npos) {
            std::string var_name = tname.substr(0, dot);
            std::string method = tname.substr(dot + 1);

            if (method == "putc") {
                LocalVar* local_target = nullptr;
                size_t net_target_id = (size_t)-1;
                LogicVal str_val;
                bool found_target = false;

                if (expr_eval.get_context().has_active_frame()) {
                    local_target = expr_eval.get_context().lookup_local(var_name);
                    if (local_target) {
                        str_val = local_target->value;
                        found_target = true;
                    }
                }
                if (!found_target && !bp.call_stack.empty()) {
                    for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                        LocalVar* lv = it->local_frame.get_var(var_name);
                        if (lv) {
                            local_target = lv;
                            str_val = lv->value;
                            found_target = true;
                            break;
                        }
                    }
                }
                if (!found_target) {
                    size_t nid = engine.get_net_id(bp.scope, var_name);
                    if (nid != (size_t)-1) {
                        net_target_id = nid;
                        str_val = engine.netlist[nid].value;
                        found_target = true;
                    }
                }
                if (!found_target && expr_eval.get_current_this() && expr_eval.get_current_this()->has_property(var_name)) {
                    str_val = expr_eval.get_current_this()->get_property(var_name);
                    found_target = true;
                }
                if (found_target && tcall->args.size() >= 2) {
                    LogicVal iv = eval_expr(tcall->args[0].get(), bp.scope);
                    LogicVal cv = eval_expr(tcall->args[1].get(), bp.scope);
                    int idx = (int)iv.to_signed();
                    if (idx >= 0 && idx < (int)str_val.str_val.size()) {
                        str_val.str_val[idx] = (char)(cv.aval & 0xFF);
                        if (local_target) {
                            local_target->value = str_val;
                        } else if (net_target_id != (size_t)-1) {
                            engine.netlist[net_target_id].set(str_val);
                        } else if (expr_eval.get_current_this()) {
                            expr_eval.get_current_this()->set_property(var_name, str_val);
                        }
                    }
                }
                return;
            }
            if (method == "len" || method == "getc" || method == "toupper" || method == "tolower" ||
                method == "compare" || method == "icompare" || method == "substr" || method == "atoi" ||
                method == "atohex" || method == "atooct" || method == "atobin" || method == "atoreal") {
                return;
            }

            if (engine.is_semaphore(var_name, bp.scope)) {
                uint64_t handle_id = engine.get_semaphore_handle_id(var_name, bp.scope, &bp, &expr_eval.get_context());
                auto sobj = engine.get_semaphore(handle_id);
                if (sobj) {
                    int count = 1;
                    if (!tcall->args.empty()) {
                        LogicVal kval = expr_eval.eval(tcall->args[0].get(), bp.scope);
                        count = kval.is_unknown() ? 0 : (int)kval.aval;
                    }
                    if (method == "get") {
                        if (sobj->keys >= count && sobj->waiting_threads.empty()) {
                            sobj->keys -= count;
                        } else {
                            bp.is_waiting_sync = true;
                            WaitingThread wt;
                            wt.pid = bp.pid;
                            wt.keys_requested = count;
                            sobj->waiting_threads.push_back(wt);
                        }
                        return;
                    }
                    if (method == "put") {
                        sobj->keys += count;
                        while (!sobj->waiting_threads.empty()) {
                            auto front = sobj->waiting_threads.front();
                            if (sobj->keys >= front.keys_requested) {
                                sobj->keys -= front.keys_requested;
                                sobj->waiting_threads.pop_front();
                                size_t wpid = front.pid;
                                if (wpid < engine.processes.size()) {
                                    auto& wake_bp = engine.processes[wpid];
                                    wake_bp.is_waiting_sync = false;
                                    if (!wake_bp.call_stack.empty()) {
                                        wake_bp.call_stack.back().pc++;
                                    } else {
                                        wake_bp.pc++;
                                    }
                                    engine.scheduler.step_process(wpid);
                                }
                            } else {
                                break;
                            }
                        }
                        return;
                    }
                    if (method == "try_get") {
                        if (sobj->keys >= count) {
                            sobj->keys -= count;
                        }
                        return;
                    }
                }
                return;
            }

            if (engine.is_mailbox(var_name, bp.scope)) {
                uint64_t handle_id = engine.get_mailbox_handle_id(var_name, bp.scope, &bp, &expr_eval.get_context());
                auto mobj = engine.get_mailbox(handle_id);
                if (mobj) {
                    if (method == "put") {
                        LogicVal msg = expr_eval.eval(tcall->args[0].get(), bp.scope);
                        if (mobj->bound > 0 && mobj->queue.size() >= (size_t)mobj->bound) {
                            bp.is_waiting_sync = true;
                            WaitingThread wt;
                            wt.pid = bp.pid;
                            wt.val_to_put = msg;
                            wt.is_put = true;
                            mobj->waiting_put_threads.push_back(wt);
                        } else {
                            if (!mobj->waiting_get_threads.empty()) {
                                auto front = mobj->waiting_get_threads.front();
                                mobj->waiting_get_threads.pop_front();
                                size_t wpid = front.pid;
                                if (wpid < engine.processes.size()) {
                                    auto& wake_bp = engine.processes[wpid];
                                    write_variable(front.dest_var_name, msg, wake_bp);
                                    wake_bp.is_waiting_sync = false;
                                    if (!wake_bp.call_stack.empty()) {
                                        wake_bp.call_stack.back().pc++;
                                    } else {
                                        wake_bp.pc++;
                                    }
                                    engine.scheduler.step_process(wpid);
                                }
                            } else {
                                mobj->queue.push_back(msg);
                            }
                        }
                        return;
                    }
                    if (method == "get") {
                        std::string dest_var = "";
                        if (!tcall->args.empty()) {
                            if (auto id_node = dynamic_cast<IdentifierNode*>(tcall->args[0].get())) {
                                dest_var = id_node->name;
                            }
                        }
                        if (mobj->queue.empty()) {
                            bp.is_waiting_sync = true;
                            WaitingThread wt;
                            wt.pid = bp.pid;
                            wt.dest_var_name = dest_var;
                            wt.is_put = false;
                            mobj->waiting_get_threads.push_back(wt);
                        } else {
                            LogicVal msg = mobj->queue.front();
                            mobj->queue.pop_front();
                            if (!dest_var.empty()) {
                                write_variable(dest_var, msg, bp);
                            }
                            if (!mobj->waiting_put_threads.empty()) {
                                auto front = mobj->waiting_put_threads.front();
                                mobj->waiting_put_threads.pop_front();
                                mobj->queue.push_back(front.val_to_put);
                                size_t wpid = front.pid;
                                if (wpid < engine.processes.size()) {
                                    auto& wake_bp = engine.processes[wpid];
                                    wake_bp.is_waiting_sync = false;
                                    if (!wake_bp.call_stack.empty()) {
                                        wake_bp.call_stack.back().pc++;
                                    } else {
                                        wake_bp.pc++;
                                    }
                                    engine.scheduler.step_process(wpid);
                                }
                            }
                        }
                        return;
                    }
                    if (method == "peek") {
                        std::string dest_var = "";
                        if (!tcall->args.empty()) {
                            if (auto id_node = dynamic_cast<IdentifierNode*>(tcall->args[0].get())) {
                                dest_var = id_node->name;
                            }
                        }
                        if (mobj->queue.empty()) {
                            bp.is_waiting_sync = true;
                            WaitingThread wt;
                            wt.pid = bp.pid;
                            wt.dest_var_name = dest_var;
                            wt.is_put = false;
                            mobj->waiting_get_threads.push_back(wt);
                        } else {
                            LogicVal msg = mobj->queue.front();
                            if (!dest_var.empty()) {
                                write_variable(dest_var, msg, bp);
                            }
                        }
                        return;
                    }
                }
                return;
            }

            ClassObject* cl_obj = nullptr;
            if (var_name == "this" || var_name == "super") {
                cl_obj = expr_eval.get_current_this();
            } else {
                if (expr_eval.get_context().has_active_frame()) {
                    LocalVar* local = expr_eval.get_context().lookup_local(var_name);
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
                if (!cl_obj && var_name.find('.') != std::string::npos) {
                    size_t last_dot = var_name.rfind('.');
                    std::string base_path = var_name.substr(0, last_dot);
                    std::string prop = var_name.substr(last_dot + 1);
                    MemberAccessNode sub_man(base_path, prop);
                    LogicVal handle_val = expr_eval.eval(&sub_man, bp.scope);
                    if (!handle_val.is_unknown() && handle_val.aval != 0) {
                        cl_obj = engine.get_class_object(handle_val.aval).get();
                    }
                }
            }
            CovergroupInstance* cg_inst = nullptr;
            if (!cl_obj) {
                uint64_t handle_id = 0;
                if (expr_eval.get_context().has_active_frame()) {
                    LocalVar* local = expr_eval.get_context().lookup_local(var_name);
                    if (local && local->is_covergroup_handle) {
                        handle_id = local->value.aval;
                    }
                }
                if (handle_id == 0 && !bp.call_stack.empty()) {
                    for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                        LocalVar* local = it->local_frame.get_var(var_name);
                        if (local && local->is_covergroup_handle) {
                            handle_id = local->value.aval;
                            break;
                        }
                    }
                }
                if (handle_id == 0) {
                    size_t nid = engine.get_net_id(bp.scope, var_name);
                    if (nid != (size_t)-1 && engine.netlist[nid].is_covergroup_handle) {
                        handle_id = engine.netlist[nid].value.aval;
                    }
                }
                if (handle_id != 0) {
                    cg_inst = engine.get_covergroup_instance(handle_id).get();
                }
            }
            if (cg_inst) {
                if (method == "sample") {
                    cg_inst->sample([&](ExpressionNode* e, const std::string& sc) {
                        return expr_eval.eval(e, sc.empty() ? bp.scope : sc);
                    });
                    return;
                }
            }
            if (cl_obj) {
                if (method == "srandom") {
                    uint32_t seed = 0;
                    if (!tcall->args.empty()) {
                        seed = static_cast<uint32_t>(expr_eval.eval(tcall->args[0].get(), bp.scope).aval);
                    }
                    cl_obj->srandom(seed);
                    return;
                }
                if (method == "randomize") {
                    std::vector<std::unique_ptr<ExpressionNode>> dummy_inline;
                    engine.solver.solve(cl_obj, dummy_inline, bp.scope, [&](ExpressionNode* e) {
                        return expr_eval.eval(e, bp.scope);
                    });
                    return;
                }
                if (var_name == "super" && method == "new") {
                    FunctionNode* base_ctor = (cl_obj->class_def && cl_obj->class_def->base_class) ?
                                              cl_obj->class_def->base_class->get_constructor() : nullptr;
                    if (base_ctor) {
                        std::vector<LogicVal> arg_vals;
                        for (auto& a : tcall->args) {
                            arg_vals.push_back(expr_eval.eval(a.get(), bp.scope));
                        }
                        LogicVal dummy;
                        expr_eval.eval_class_method(cl_obj, base_ctor, arg_vals, dummy);
                    }
                    return;
                }

                TaskNode* task_node = nullptr;
                FunctionNode* func_node = nullptr;
                if (var_name == "super") {
                    if (cl_obj->class_def && cl_obj->class_def->base_class) {
                        task_node = cl_obj->class_def->base_class->find_task(method);
                        if (!task_node) func_node = cl_obj->class_def->base_class->find_function(method);
                    }
                } else {
                    std::string static_type = "";
                    if (var_name == "this") static_type = cl_obj->class_type_name;
                    else {
                        if (expr_eval.get_context().has_active_frame()) {
                            LocalVar* local = expr_eval.get_context().lookup_local(var_name);
                            if (local && local->is_class_handle) static_type = local->class_type_name;
                        }
                        if (static_type.empty() && !bp.call_stack.empty()) {
                            for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                                LocalVar* local = it->local_frame.get_var(var_name);
                                if (local && local->is_class_handle) { static_type = local->class_type_name; break; }
                            }
                        }
                        if (static_type.empty()) {
                            size_t nid = engine.get_net_id(bp.scope, var_name);
                            if (nid != (size_t)-1 && engine.netlist[nid].is_class_handle) {
                                static_type = engine.get_class_type(var_name, bp.scope);
                            }
                        }
                    }
                    const ClassNode* static_def = !static_type.empty() ? engine.get_class_def(static_type) : cl_obj->class_def;
                    bool is_virt = false;
                    if (static_def) is_virt = static_def->is_method_virtual(method);
                    else if (cl_obj->class_def) is_virt = cl_obj->class_def->is_method_virtual(method);

                    if (is_virt && cl_obj->class_def) {
                        task_node = cl_obj->class_def->dispatch_task(method);
                        if (!task_node) func_node = cl_obj->class_def->dispatch_function(method);
                    } else if (static_def) {
                        task_node = static_def->find_task(method);
                        if (!task_node) func_node = static_def->find_function(method);
                    } else if (cl_obj->class_def) {
                        task_node = cl_obj->class_def->find_task(method);
                        if (!task_node) func_node = cl_obj->class_def->find_function(method);
                    }
                }

                if (task_node) {
                    ClassObject* prev_this = expr_eval.get_current_this();
                    expr_eval.set_current_this(cl_obj);
                    std::string frame_name = cl_obj->class_type_name + "::" + task_node->name + "__call";
                    expr_eval.get_context().push_frame(frame_name);
                    std::vector<std::pair<std::string, std::string>> output_bindings;
                    for (size_t i = 0; i < task_node->ports.size(); ++i) {
                        const auto& p = task_node->ports[i];
                        uint32_t w = 32;
                        if (p.msb_expr && p.lsb_expr) {
                            auto msb_n = dynamic_cast<NumberNode*>(p.msb_expr.get());
                            auto lsb_n = dynamic_cast<NumberNode*>(p.lsb_expr.get());
                            if (msb_n && lsb_n) {
                                w = (msb_n->aval >= lsb_n->aval) ? (uint32_t)(msb_n->aval - lsb_n->aval + 1) : (uint32_t)(lsb_n->aval - msb_n->aval + 1);
                            }
                        }
                        expr_eval.get_context().declare_local(p.name, w, p.is_signed);
                        if (p.type_name == "string") {
                            LocalVar* lv = expr_eval.get_context().lookup_local(p.name);
                            if (lv) lv->value = LogicVal::make_string("");
                        }
                        if (i < tcall->args.size()) {
                            LogicVal arg_v = eval_expr(tcall->args[i].get(), bp.scope);
                            expr_eval.get_context().set_local_value(p.name, arg_v);
                        } else if (p.default_value) {
                            LogicVal def_v = eval_expr(p.default_value.get(), bp.scope);
                            expr_eval.get_context().set_local_value(p.name, def_v);
                        }
                        if (p.direction == "output" || p.direction == "inout") {
                            if (i < tcall->args.size() && tcall->args[i]) {
                                if (auto id = dynamic_cast<IdentifierNode*>(tcall->args[i].get())) {
                                    output_bindings.push_back({p.name, id->name});
                                }
                            }
                        }
                    }
                    for (auto& s : task_node->get_body()) {
                        if (expr_eval.get_context().current_frame() && expr_eval.get_context().current_frame()->returned) break;
                        execute_statement(s.get(), bp);
                    }
                    std::vector<std::pair<std::string, LogicVal>> output_vals;
                    for (const auto& binding : output_bindings) {
                        LocalVar* lv = expr_eval.get_context().current_frame() ? expr_eval.get_context().current_frame()->get_var(binding.first) : nullptr;
                        if (lv) {
                            output_vals.push_back({binding.second, lv->value});
                        }
                    }
                    expr_eval.get_context().pop_frame();
                    expr_eval.set_current_this(prev_this);
                    for (const auto& out : output_vals) {
                        bool written = false;
                        if (expr_eval.get_context().has_active_frame()) {
                            LocalVar* caller_local = expr_eval.get_context().lookup_local(out.first);
                            if (caller_local) {
                                caller_local->value = LogicOps::cast_width(out.second, caller_local->width, caller_local->is_signed);
                                written = true;
                            }
                        }
                        if (!written && !bp.call_stack.empty()) {
                            for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                                LocalVar* local = it->local_frame.get_var(out.first);
                                if (local) {
                                    local->value = LogicOps::cast_width(out.second, local->width, local->is_signed);
                                    written = true;
                                    break;
                                }
                            }
                        }
                        if (!written) {
                            std::string scoped_target = bp.scope.empty() ? out.first : (bp.scope + "." + out.first);
                            size_t net_id = (size_t)-1;
                            if (engine.net_map.find(scoped_target) != engine.net_map.end()) net_id = engine.net_map.at(scoped_target);
                            else if (engine.net_map.find(out.first) != engine.net_map.end()) net_id = engine.net_map.at(out.first);
                            if (net_id != (size_t)-1) {
                                auto& net = engine.netlist[net_id];
                                net.set(LogicOps::cast_width(out.second, net.width, net.is_signed));
                            }
                        }
                    }
                    return;
                }
                if (func_node) {
                    ClassObject* prev_this = expr_eval.get_current_this();
                    expr_eval.set_current_this(cl_obj);
                    std::string frame_name = cl_obj->class_type_name + "::" + func_node->name + "__call";
                    expr_eval.get_context().push_frame(frame_name);
                    if (expr_eval.get_context().current_frame()) {
                        expr_eval.get_context().current_frame()->func_return_name = func_node->name;
                    }
                    expr_eval.get_context().declare_local(func_node->name, func_node->return_width, func_node->return_signed);

                    std::vector<std::pair<std::string, std::string>> output_bindings;
                    for (size_t i = 0; i < func_node->inputs.size(); ++i) {
                        const auto& in_p = func_node->inputs[i];
                        uint32_t w = 32;
                        if (in_p.msb_expr && in_p.lsb_expr) {
                            auto msb_n = dynamic_cast<NumberNode*>(in_p.msb_expr.get());
                            auto lsb_n = dynamic_cast<NumberNode*>(in_p.lsb_expr.get());
                            if (msb_n && lsb_n) {
                                w = (msb_n->aval >= lsb_n->aval) ? (uint32_t)(msb_n->aval - lsb_n->aval + 1) : (uint32_t)(lsb_n->aval - msb_n->aval + 1);
                            }
                        }
                        expr_eval.get_context().declare_local(in_p.name, w, in_p.is_signed);
                        if (in_p.type_name == "string") {
                            LocalVar* lv = expr_eval.get_context().lookup_local(in_p.name);
                            if (lv) lv->value = LogicVal::make_string("");
                        }
                        if (i < tcall->args.size()) {
                            LogicVal arg_v = eval_expr(tcall->args[i].get(), bp.scope);
                            expr_eval.get_context().set_local_value(in_p.name, arg_v);
                        } else if (in_p.default_value) {
                            LogicVal def_v = eval_expr(in_p.default_value.get(), bp.scope);
                            expr_eval.get_context().set_local_value(in_p.name, def_v);
                        }
                        if (in_p.direction == "output" || in_p.direction == "inout") {
                            if (i < tcall->args.size() && tcall->args[i]) {
                                if (auto id = dynamic_cast<IdentifierNode*>(tcall->args[i].get())) {
                                    output_bindings.push_back({in_p.name, id->name});
                                }
                            }
                        }
                    }

                    BoundProcess* prev_bp = expr_eval.get_current_process();
                    BoundProcess call_bp{frame_name, nullptr, 0, false};
                    expr_eval.set_current_process(&call_bp);
                    for (auto& s : func_node->get_body()) {
                        if (expr_eval.get_context().current_frame() && expr_eval.get_context().current_frame()->returned) break;
                        execute_statement(s.get(), call_bp);
                    }
                    expr_eval.set_current_process(prev_bp);

                    std::vector<std::pair<std::string, LogicVal>> output_vals;
                    for (const auto& binding : output_bindings) {
                        LocalVar* lv = expr_eval.get_context().current_frame() ? expr_eval.get_context().current_frame()->get_var(binding.first) : nullptr;
                        if (lv) {
                            output_vals.push_back({binding.second, lv->value});
                        }
                    }
                    expr_eval.get_context().pop_frame();
                    expr_eval.set_current_this(prev_this);

                    for (const auto& out : output_vals) {
                        bool written = false;
                        if (expr_eval.get_context().has_active_frame()) {
                            LocalVar* caller_local = expr_eval.get_context().lookup_local(out.first);
                            if (caller_local) {
                                caller_local->value = LogicOps::cast_width(out.second, caller_local->width, caller_local->is_signed);
                                written = true;
                            }
                        }
                        if (!written && !bp.call_stack.empty()) {
                            for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                                LocalVar* local = it->local_frame.get_var(out.first);
                                if (local) {
                                    local->value = LogicOps::cast_width(out.second, local->width, local->is_signed);
                                    written = true;
                                    break;
                                }
                            }
                        }
                        if (!written) {
                            std::string scoped_target = bp.scope.empty() ? out.first : (bp.scope + "." + out.first);
                            size_t net_id = (size_t)-1;
                            if (engine.net_map.find(scoped_target) != engine.net_map.end()) net_id = engine.net_map.at(scoped_target);
                            else if (engine.net_map.find(out.first) != engine.net_map.end()) net_id = engine.net_map.at(out.first);
                            if (net_id != (size_t)-1) {
                                auto& net = engine.netlist[net_id];
                                net.set(LogicOps::cast_width(out.second, net.width, net.is_signed));
                            }
                        }
                    }
                    return;
                }
            }
            QueueObject* q = nullptr;
            if (expr_eval.get_context().has_active_frame()) {
                LocalVar* local = expr_eval.get_context().lookup_local(var_name);
                if (local && local->is_queue) q = &local->queue_data;
            }
            if (!q && !bp.call_stack.empty()) {
                for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                    LocalVar* local = it->local_frame.get_var(var_name);
                    if (local && local->is_queue) {
                        q = &local->queue_data;
                        break;
                    }
                }
            }
            if (!q) {
                q = engine.get_queue(var_name, bp.scope);
            }
            if (q) {
                if (method == "push_back") {
                    if (!tcall->args.empty()) {
                        LogicVal val = eval_expr(tcall->args[0].get(), bp.scope);
                        q->push_back(val);
                    }
                    return;
                } else if (method == "push_front") {
                    if (!tcall->args.empty()) {
                        LogicVal val = eval_expr(tcall->args[0].get(), bp.scope);
                        q->push_front(val);
                    }
                    return;
                } else if (method == "pop_front") {
                    q->pop_front();
                    return;
                } else if (method == "pop_back") {
                    q->pop_back();
                    return;
                } else if (method == "delete") {
                    if (tcall->args.empty()) {
                        q->delete_all();
                    } else {
                        LogicVal idx_v = eval_expr(tcall->args[0].get(), bp.scope);
                        q->delete_at((int64_t)idx_v.aval);
                    }
                    return;
                }
            }
            DynamicArrayObject* da = nullptr;
            if (expr_eval.get_context().has_active_frame()) {
                LocalVar* local = expr_eval.get_context().lookup_local(var_name);
                if (local && local->is_dynamic_array) da = &local->dyn_array_data;
            }
            if (!da && !bp.call_stack.empty()) {
                for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                    LocalVar* local = it->local_frame.get_var(var_name);
                    if (local && local->is_dynamic_array) {
                        da = &local->dyn_array_data;
                        break;
                    }
                }
            }
            if (!da && expr_eval.get_current_this() && expr_eval.get_current_this()->has_dynamic_array(var_name)) {
                da = expr_eval.get_current_this()->get_dynamic_array(var_name);
            }
            if (!da) {
                da = engine.get_dynamic_array(var_name, bp.scope);
            }
            if (da) {
                if (method == "delete") {
                    da->delete_all();
                    return;
                }
            }
            AssociativeArrayObject* assoc = nullptr;
            if (expr_eval.get_context().has_active_frame()) {
                LocalVar* local = expr_eval.get_context().lookup_local(var_name);
                if (local && local->is_associative_array) assoc = &local->assoc_array_data;
            }
            if (!assoc && !bp.call_stack.empty()) {
                for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                    LocalVar* local = it->local_frame.get_var(var_name);
                    if (local && local->is_associative_array) {
                        assoc = &local->assoc_array_data;
                        break;
                    }
                }
            }
            if (!assoc) {
                assoc = engine.get_assoc_array(var_name, bp.scope);
            }
            if (assoc) {
                auto write_tcall_arg = [&](ExpressionNode* arg_expr, uint64_t k_val) {
                    if (auto id = dynamic_cast<IdentifierNode*>(arg_expr)) {
                        bool written = false;
                        if (expr_eval.get_context().has_active_frame()) {
                            LocalVar* lv = expr_eval.get_context().lookup_local(id->name);
                            if (lv) {
                                lv->value = LogicOps::cast_width(LogicVal(64, k_val, 0), lv->width, lv->is_signed);
                                written = true;
                            }
                        }
                        if (!written && !bp.call_stack.empty()) {
                            for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                                LocalVar* lv = it->local_frame.get_var(id->name);
                                if (lv) {
                                    lv->value = LogicOps::cast_width(LogicVal(64, k_val, 0), lv->width, lv->is_signed);
                                    written = true;
                                    break;
                                }
                            }
                        }
                        if (!written) {
                            size_t net_id = engine.get_net_id(bp.scope, id->name);
                            if (net_id != (size_t)-1) {
                                LogicVal casted = LogicOps::cast_width(LogicVal(64, k_val, 0), engine.netlist[net_id].width, engine.netlist[net_id].is_signed);
                                engine.netlist[net_id].set(casted);
                            }
                        }
                    }
                };

                if (method == "delete") {
                    if (tcall->args.empty()) {
                        assoc->delete_all();
                    } else {
                        LogicVal k = eval_expr(tcall->args[0].get(), bp.scope);
                        if (assoc->is_string_key) {
                            assoc->delete_key(k.str_val);
                        } else {
                            uint64_t key = k.aval;
                            if (assoc->key_width < 64 && assoc->key_width > 0) {
                                key &= ((1ULL << assoc->key_width) - 1);
                            }
                            assoc->delete_key(key);
                        }
                    }
                    return;
                } else if (method == "first" || method == "last") {
                    uint64_t out_k = 0;
                    bool ok = (method == "first") ? assoc->first(out_k) : assoc->last(out_k);
                    if (ok && !tcall->args.empty()) {
                        write_tcall_arg(tcall->args[0].get(), out_k);
                    }
                    return;
                } else if (method == "next" || method == "prev") {
                    uint64_t in_out_k = 0;
                    if (!tcall->args.empty()) {
                        LogicVal k_val = eval_expr(tcall->args[0].get(), bp.scope);
                        in_out_k = k_val.aval;
                        if (assoc->key_width < 64 && assoc->key_width > 0) {
                            in_out_k &= ((1ULL << assoc->key_width) - 1);
                        }
                    }
                    bool ok = (method == "next") ? assoc->next(in_out_k) : assoc->prev(in_out_k);
                    if (ok && !tcall->args.empty()) {
                        write_tcall_arg(tcall->args[0].get(), in_out_k);
                    }
                    return;
                }
            }
        }

        if (tname == "srandom") {
            ClassObject* cl_obj = expr_eval.get_current_this();
            if (cl_obj) {
                uint32_t seed = 0;
                if (!tcall->args.empty()) {
                    seed = static_cast<uint32_t>(expr_eval.eval(tcall->args[0].get(), bp.scope).aval);
                }
                cl_obj->srandom(seed);
                return;
            }
        }

        std::string scoped_tname = bp.scope.empty() ? tcall->task_name : (bp.scope + "." + tcall->task_name);
        DpiImportNode* dpi_node = nullptr;
        if (engine.dpi_import_map.find(scoped_tname) != engine.dpi_import_map.end()) {
            dpi_node = engine.dpi_import_map.at(scoped_tname);
        } else if (engine.dpi_import_map.find(tcall->task_name) != engine.dpi_import_map.end()) {
            dpi_node = engine.dpi_import_map.at(tcall->task_name);
        }
        if (dpi_node) {
            expr_eval.eval_dpi_call(dpi_node, tcall->args, bp.scope);
            return;
        }

        ClassObject* active_cl_obj = (!bp.call_stack.empty() && bp.call_stack.back().this_obj) ?
                                     bp.call_stack.back().this_obj : expr_eval.get_current_this();
        if (active_cl_obj && active_cl_obj->class_def) {
            FunctionNode* fn = active_cl_obj->class_def->dispatch_function(tcall->task_name);
            if (fn) {
                std::vector<LogicVal> arg_vals;
                for (auto& a : tcall->args) {
                    arg_vals.push_back(expr_eval.eval(a.get(), bp.scope));
                }
                LogicVal dummy_res;
                expr_eval.eval_class_method(active_cl_obj, fn, arg_vals, dummy_res);
                return;
            }
        }

        FunctionNode* standalone_fn = nullptr;
        if (engine.function_map.find(scoped_tname) != engine.function_map.end()) {
            standalone_fn = engine.function_map.at(scoped_tname);
        } else if (engine.function_map.find(tcall->task_name) != engine.function_map.end()) {
            standalone_fn = engine.function_map.at(tcall->task_name);
        }
        if (standalone_fn) {
            std::vector<LogicVal> arg_vals;
            for (auto& a : tcall->args) {
                arg_vals.push_back(expr_eval.eval(a.get(), bp.scope));
            }
            LogicVal dummy_res;
            expr_eval.eval_function(standalone_fn->name, arg_vals, dummy_res);
            return;
        }

        TaskNode* target_task = nullptr;
        if (engine.task_map.find(scoped_tname) != engine.task_map.end()) {
            target_task = engine.task_map.at(scoped_tname);
        } else if (engine.task_map.find(tcall->task_name) != engine.task_map.end()) {
            target_task = engine.task_map.at(tcall->task_name);
        }
        if (target_task) {
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

            std::string frame_name = (bp.scope.empty() ? "" : (bp.scope + ".")) + target_task->name + "__call";
            expr_eval.get_context().push_frame(frame_name);

            std::vector<std::pair<std::string, std::string>> output_bindings;

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
                expr_eval.get_context().declare_local(p.name, w, p.is_signed);

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
                        LogicVal arg_val = eval_expr(val_expr, bp.scope);
                        expr_eval.get_context().set_local_value(p.name, LogicOps::cast_width(arg_val, w, p.is_signed));
                    }
                }

                if (p.direction == "output" || p.direction == "inout") {
                    if (bindings[i].actual_expr) {
                        if (auto id = dynamic_cast<IdentifierNode*>(bindings[i].actual_expr)) {
                            output_bindings.push_back({p.name, id->name});
                        }
                    }
                }
            }

            for (auto& s : target_task->body) {
                execute_statement(s.get(), bp);
                if (!engine.scheduler.is_active()) break;
            }

            // Save output values from task frame
            std::vector<std::pair<std::string, LogicVal>> output_vals;
            for (const auto& binding : output_bindings) {
                LocalVar* lv = expr_eval.get_context().current_frame() ? expr_eval.get_context().current_frame()->get_var(binding.first) : nullptr;
                if (lv) {
                    output_vals.push_back({binding.second, lv->value});
                }
            }

            // Pop task frame
            expr_eval.get_context().pop_frame();

            // Write back output values to caller context
            for (const auto& out : output_vals) {
                bool written = false;
                if (expr_eval.get_context().has_active_frame()) {
                    LocalVar* caller_local = expr_eval.get_context().lookup_local(out.first);
                    if (caller_local) {
                        caller_local->value = LogicOps::cast_width(out.second, caller_local->width, caller_local->is_signed);
                        written = true;
                    }
                }
                if (!written && !bp.call_stack.empty()) {
                    for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                        LocalVar* local = it->local_frame.get_var(out.first);
                        if (local) {
                            local->value = LogicOps::cast_width(out.second, local->width, local->is_signed);
                            written = true;
                            break;
                        }
                    }
                }
                if (!written) {
                    std::string scoped_target = bp.scope.empty() ? out.first : (bp.scope + "." + out.first);
                    size_t net_id = (size_t)-1;
                    if (engine.net_map.find(scoped_target) != engine.net_map.end()) net_id = engine.net_map.at(scoped_target);
                    else if (engine.net_map.find(out.first) != engine.net_map.end()) net_id = engine.net_map.at(out.first);
                    if (net_id != (size_t)-1) {
                        auto& net = engine.netlist[net_id];
                        net.set(LogicOps::cast_width(out.second, net.width, net.is_signed));
                    }
                }
            }
            return;
        }
        return;
    }

    if (auto rstmt = dynamic_cast<RandomizeStatementNode*>(stmt)) {
        if (rstmt->call) {
            eval_expr(rstmt->call.get(), bp.scope);
        }
        return;
    }

    if (auto delay = dynamic_cast<DelayStatementNode*>(stmt)) {
        return;
    }

    if (auto sfmt = dynamic_cast<SFormatStatementNode*>(stmt)) {
        if (!sfmt->target_expr || !sfmt->format_expr) {
            throw std::runtime_error("[CAS-DTINPCIL] $sformat requires at least 2 arguments per IEEE 1800-2023 section 20.14");
        }
        LogicVal fmt_val = eval_expr(sfmt->format_expr.get(), bp.scope);
        if (!fmt_val.is_string) {
            throw std::runtime_error("[CAS-DTINPCIL] Format argument in $sformat must evaluate to a string type per IEEE 1800-2023 section 20.14");
        }
        std::vector<LogicVal> arg_vals;
        for (const auto& a : sfmt->arg_exprs) {
            arg_vals.push_back(eval_expr(a.get(), bp.scope));
        }
        std::string formatted = ExprEvaluator::format_systemverilog_string(fmt_val.str_val, arg_vals, engine.get_sim_time());
        LogicVal res = LogicVal::make_string(formatted);

        if (auto id = dynamic_cast<IdentifierNode*>(sfmt->target_expr.get())) {
            write_variable(id->name, res, bp);
        } else if (auto ma = dynamic_cast<MemberAccessNode*>(sfmt->target_expr.get())) {
            if (expr_eval.get_current_this() && expr_eval.get_current_this()->has_property(ma->member_name)) {
                expr_eval.get_current_this()->set_property(ma->member_name, res);
            } else {
                write_variable(ma->var_name + "." + ma->member_name, res, bp);
            }
        }
        return;
    }

    if (auto disp = dynamic_cast<DisplayStatementNode*>(stmt)) {
        if (disp->is_monitor) {
            std::function<void(ExpressionNode*)> check_mon_expr = [&](ExpressionNode* expr) {
                if (!expr) return;
                if (auto id = dynamic_cast<IdentifierNode*>(expr)) {
                    if (engine.is_covergroup_handle(id->name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: covergroup handle '" + id->name + "' used directly in $monitor per IEEE 1800-2023 section 19 & 21.2.3");
                    }
                    if (engine.is_class_handle(id->name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: class handle '" + id->name + "' used directly in $monitor per IEEE 1800-2023 section 8.4 & 21.2.3");
                    }
                    if (engine.is_queue(id->name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: queue '" + id->name + "' used directly in $monitor per IEEE 1800-2023 section 7.10.4 & 21.2.3");
                    }
                    if (engine.is_dynamic_array(id->name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: dynamic array '" + id->name + "' used directly in $monitor per IEEE 1800-2023 section 7.5 & 21.2.3");
                    }
                    if (engine.is_assoc_array(id->name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: associative array '" + id->name + "' used directly in $monitor per IEEE 1800-2023 section 7.8 & 21.2.3");
                    }
                    if (engine.is_property(id->name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: property '" + id->name + "' used directly in $monitor per IEEE 1800-2023 section 16 & 21.2.3");
                    }
                    if (engine.is_sequence(id->name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: sequence '" + id->name + "' used directly in $monitor per IEEE 1800-2023 section 16 & 21.2.3");
                    }
                    if (engine.is_assertion(id->name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: assertion '" + id->name + "' used directly in $monitor per IEEE 1800-2023 section 16 & 21.2.3");
                    }
                    if (engine.is_chandle(id->name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: chandle '" + id->name + "' used directly in $monitor per IEEE 1800-2023 section 35.5.4 & 21.2.3");
                    }
                    if (engine.is_semaphore(id->name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: semaphore handle '" + id->name + "' used in $monitor per IEEE 1800-2023 section 15.3 & 21.2.3");
                    }
                    if (engine.is_mailbox(id->name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: mailbox handle '" + id->name + "' used in $monitor per IEEE 1800-2023 section 15.4 & 21.2.3");
                    }
                    std::string s_id = bp.scope.empty() ? id->name : (bp.scope + "." + id->name);
                    if (engine.is_open_array(id->name, bp.scope) ||
                        engine.memory_map.find(s_id) != engine.memory_map.end() ||
                        engine.memory_map.find(id->name) != engine.memory_map.end()) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: open array handle or unpacked array '" + id->name + "' used directly in $monitor per IEEE 1800-2023 section 35.5.6.1 & 21.2.3");
                    }
                    if (engine.is_sva_local_var(id->name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: sequence or property local variable '" + id->name + "' used directly in $monitor per IEEE 1800-2023 section 16.10 & 21.2.3");
                    }
                } else if (auto idx = dynamic_cast<IndexSelectNode*>(expr)) {
                    if (engine.is_sva_local_var(idx->name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: sequence or property local variable '" + idx->name + "[]' used in $monitor per IEEE 1800-2023 section 16.10 & 21.2.3");
                    }
                    if (engine.is_queue(idx->name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: queue index select '" + idx->name + "[]' used in $monitor per IEEE 1800-2023 section 7.10.4 & 21.2.3");
                    }
                    if (engine.is_dynamic_array(idx->name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: dynamic array index select '" + idx->name + "[]' used in $monitor per IEEE 1800-2023 section 7.5 & 21.2.3");
                    }
                    if (engine.is_assoc_array(idx->name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: associative array index select '" + idx->name + "[]' used in $monitor per IEEE 1800-2023 section 7.8 & 21.2.3");
                    }
                    check_mon_expr(idx->msb_expr.get());
                    check_mon_expr(idx->lsb_expr.get());
                } else if (auto ma = dynamic_cast<MemberAccessNode*>(expr)) {
                    if (engine.is_covergroup_handle(ma->var_name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: covergroup item '" + ma->var_name + "." + ma->member_name + "' used in $monitor per IEEE 1800-2023 section 19 & 21.2.3");
                    }
                    if (engine.is_class_handle(ma->var_name, bp.scope) || ma->var_name == "this" || ma->var_name == "super") {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: class property '" + ma->var_name + "." + ma->member_name + "' used in $monitor per IEEE 1800-2023 section 8.4 & 21.2.3");
                    }
                } else if (dynamic_cast<SysCastCallNode*>(expr)) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: '$cast' used directly in $monitor per IEEE 1800-2023 section 8.16 & 21.2.3");
                } else if (dynamic_cast<NullNode*>(expr)) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: 'null' literal used directly in $monitor per IEEE 1800-2023 section 8.4 & 21.2.3");
                } else if (dynamic_cast<ClassNewNode*>(expr)) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: class constructor 'new()' used in $monitor per IEEE 1800-2023 section 8.7 & 21.2.3");
                } else if (dynamic_cast<ThisNode*>(expr)) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: 'this' used directly in $monitor per IEEE 1800-2023 section 8.11 & 21.2.3");
                } else if (dynamic_cast<RandomizeCallNode*>(expr)) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: 'randomize()' used directly in $monitor per IEEE 1800-2023 section 18 & 21.2.3");
                } else if (auto smc = dynamic_cast<StaticMethodCallNode*>(expr)) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: static class method '" + smc->class_type_name + "::" + smc->method_name + "()' used directly in $monitor per IEEE 1800-2023 section 8.25 & 21.2.3");
                } else if (auto mc = dynamic_cast<MethodCallNode*>(expr)) {
                    if (engine.is_covergroup_handle(mc->var_name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: covergroup method '" + mc->var_name + "." + mc->method_name + "()' used in $monitor per IEEE 1800-2023 section 19 & 21.2.3");
                    }
                    if (engine.is_class_handle(mc->var_name, bp.scope) || mc->var_name == "this") {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: class method '" + mc->var_name + "." + mc->method_name + "()' used in $monitor per IEEE 1800-2023 section 8.5 & 21.2.3");
                    }
                    if (engine.is_queue(mc->var_name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: queue method '" + mc->var_name + "." + mc->method_name + "()' used in $monitor per IEEE 1800-2023 section 7.10.4 & 21.2.3");
                    }
                    if (engine.is_dynamic_array(mc->var_name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: dynamic array method '" + mc->var_name + "." + mc->method_name + "()' used in $monitor per IEEE 1800-2023 section 7.5 & 21.2.3");
                    }
                    if (engine.is_assoc_array(mc->var_name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: associative array method '" + mc->var_name + "." + mc->method_name + "()' used in $monitor per IEEE 1800-2023 section 7.8 & 21.2.3");
                    }
                    if (engine.is_semaphore(mc->var_name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: semaphore method '" + mc->var_name + "." + mc->method_name + "()' used in $monitor per IEEE 1800-2023 section 15.3 & 21.2.3");
                    }
                    if (engine.is_mailbox(mc->var_name, bp.scope)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: mailbox method '" + mc->var_name + "." + mc->method_name + "()' used in $monitor per IEEE 1800-2023 section 15.4 & 21.2.3");
                    }
                    for (auto& a : mc->args) check_mon_expr(a.get());
                } else if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
                    if (bin->op == "==" || bin->op == "!=") {
                        auto is_null = [](ExpressionNode* e) {
                            return dynamic_cast<NullNode*>(e) != nullptr;
                        };
                        auto is_chandle_expr = [&](ExpressionNode* e) {
                            if (auto id = dynamic_cast<IdentifierNode*>(e)) {
                                return engine.is_chandle(id->name, bp.scope) ||
                                       engine.is_semaphore(id->name, bp.scope) ||
                                       engine.is_mailbox(id->name, bp.scope);
                            }
                            return false;
                        };
                        if ((is_chandle_expr(bin->lhs.get()) && is_null(bin->rhs.get())) ||
                            (is_null(bin->lhs.get()) && is_chandle_expr(bin->rhs.get()))) {
                            return;
                        }
                    }
                    check_mon_expr(bin->lhs.get());
                    check_mon_expr(bin->rhs.get());
                } else if (auto un = dynamic_cast<UnaryOpNode*>(expr)) {
                    check_mon_expr(un->operand.get());
                } else if (auto ter = dynamic_cast<TernaryOpNode*>(expr)) {
                    check_mon_expr(ter->condition.get());
                    check_mon_expr(ter->true_expr.get());
                    check_mon_expr(ter->false_expr.get());
                } else if (auto sf = dynamic_cast<SysSampledFuncNode*>(expr)) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: SVA sampled value function '" + sf->func_name + "' used directly in $monitor per IEEE 1800-2023 section 16.9.3 & 21.2.3");
                } else if (dynamic_cast<SequenceRepetitionNode*>(expr)) {
                    throw std::runtime_error("[CAS-DTINPCIL] SVA sequence repetition operator used directly in $monitor per IEEE 1800-2023 section 16.9.2 & 21.2.3");
                } else if (dynamic_cast<SequenceConcatNode*>(expr)) {
                    throw std::runtime_error("[CAS-DTINPCIL] SVA sequence concatenation used directly in $monitor per IEEE 1800-2023 section 16.9.2 & 21.2.3");
                } else if (dynamic_cast<SequenceMatchItemNode*>(expr)) {
                    throw std::runtime_error("[CAS-DTINPCIL] SVA sequence match item used directly in $monitor per IEEE 1800-2023 section 16.10, 16.11 & 21.2.3");
                } else if (auto fc = dynamic_cast<FunctionCallNode*>(expr)) {
                    if (engine.dpi_import_map.find(fc->func_name) != engine.dpi_import_map.end()) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: DPI-C foreign function '" + fc->func_name + "' used directly in $monitor per IEEE 1800-2023 section 35 & 21.2.3");
                    }
                    if (engine.dpi_export_map.find(fc->func_name) != engine.dpi_export_map.end() ||
                        engine.dpi_export_map.find(bp.scope + "." + fc->func_name) != engine.dpi_export_map.end()) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: DPI-C exported function '" + fc->func_name + "' used directly in $monitor per IEEE 1800-2023 section 35 & 21.2.3");
                    }
                    for (auto& a : fc->args) check_mon_expr(a.get());
                }
            };
            for (const auto& arg : disp->arg_exprs) {
                check_mon_expr(arg.get());
            }
            engine.monitor_session.active = true;
            engine.monitor_session.scope = bp.scope;
            engine.monitor_session.format_string = disp->format_string;
            engine.monitor_session.args.clear();
            engine.monitor_session.arg_exprs.clear();
            for (auto& arg : disp->arg_exprs) {
                if (auto id = dynamic_cast<IdentifierNode*>(arg.get())) {
                    engine.monitor_session.args.push_back(id->name);
                }
                engine.monitor_session.arg_exprs.push_back(arg ? arg->clone() : nullptr);
            }
            engine.monitor_session.has_leading_time = disp->has_leading_time;
            engine.monitor_session.src_file = disp->src_file;
            engine.monitor_session.src_line = disp->src_line;
            engine.monitor_session.prev_values.clear();
        } else {
            if (!engine.is_display_allowed()) return;

            if (!disp->src_file.empty() && disp->src_line > 0) {
                std::cout << "[CAS $display " << disp->src_file << ":" << disp->src_line << "] ";
            } else {
                std::cout << "[CAS $display] ";
            }

            if (disp->has_leading_time) {
                std::cout << engine.get_sim_time() << " ";
            }
            size_t arg_idx = 0;
            size_t fmt_len = disp->format_string.size();
            for (size_t i = 0; i < fmt_len; ++i) {
                if (disp->format_string[i] == '%' && i + 1 < fmt_len) {
                    ++i;
                    if (disp->format_string[i] == '%') {
                        std::cout << '%';
                        continue;
                    }
                    bool pad_zero = false;
                    int width = 0;
                    if (disp->format_string[i] == '0') {
                        pad_zero = true;
                        ++i;
                    }
                    while (i < fmt_len && std::isdigit((unsigned char)disp->format_string[i])) {
                        width = width * 10 + (disp->format_string[i] - '0');
                        ++i;
                    }
                    char spec = (i < fmt_len) ? disp->format_string[i] : ' ';
                    if (spec == 'm') {
                        std::cout << bp.scope;
                        continue;
                    }

                    if (arg_idx < disp->arg_exprs.size()) {
                        LogicVal val = eval_expr(disp->arg_exprs[arg_idx++].get(), bp.scope);
                        if (spec == 'c') {
                            char ch = static_cast<char>(val.aval & 0xFF);
                            std::cout << ch;
                        } else if (spec == 's') {
                            if (val.is_string) std::cout << val.str_val;
                            else std::cout << val.aval;
                        } else if (val.is_unknown() && (spec == 'd' || spec == 't')) {
                            std::cout << "x";
                        } else if (spec == 'd' || spec == 't') {
                            if (val.is_real) {
                                std::cout << (int64_t)val.real_val;
                            } else if (val.is_signed) {
                                std::cout << val.to_signed();
                            } else {
                                std::cout << val.aval;
                            }
                        } else if (spec == 'f') {
                            std::cout << (val.is_real ? val.real_val : (double)val.aval);
                        } else if (spec == 'b') {
                            std::cout << val.to_bin_string();
                        } else if (spec == 'h' || spec == 'x') {
                            std::string hx = val.to_hex_string(true);
                            if (pad_zero && width > 0) {
                                while (hx.size() < (size_t)width) hx = "0" + hx;
                            }
                            std::cout << hx;
                        } else {
                            std::cout << val.aval;
                        }
                    }
                } else {
                    std::cout << disp->format_string[i];
                }
            }
            std::cout << "\n";
        }
        return;
    }

    if (auto sev = dynamic_cast<SeverityTaskStatementNode*>(stmt)) {
        std::string prefix = "";
        switch (sev->level) {
            case SeverityLevel::INFO: prefix = "Info"; break;
            case SeverityLevel::WARNING: prefix = "Warning"; break;
            case SeverityLevel::ERROR: prefix = "Error"; break;
            case SeverityLevel::FATAL: prefix = "Fatal"; break;
        }

        std::string fname = sev->src_file;
        size_t last_slash = fname.find_last_of("/\\");
        if (last_slash != std::string::npos) fname = fname.substr(last_slash + 1);

        std::string scope_str = bp.scope.empty() ? "$root" : bp.scope;
        std::string prec_unit = Lexer::get_time_precision_unit();
        if (prec_unit.empty()) prec_unit = "ps";

        // Emit location header matching Synopsys VCS line-for-line:
        // Prefix: "<filename>", <line>: <scope_path>: at time <time> <unit>
        std::cout << prefix << ": \"" << fname << "\", " << sev->src_line
                  << ": " << scope_str << ": at time "
                  << engine.get_sim_time() << " " << prec_unit << "\n";

        // Format and emit message line
        if (!sev->format_string.empty()) {
            size_t arg_idx = 0;
            size_t fmt_len = sev->format_string.size();
            for (size_t i = 0; i < fmt_len; ++i) {
                if (sev->format_string[i] == '%' && i + 1 < fmt_len) {
                    ++i;
                    if (sev->format_string[i] == '%') {
                        std::cout << '%';
                        continue;
                    }
                    bool pad_zero = false;
                    int width = 0;
                    if (sev->format_string[i] == '0') {
                        pad_zero = true;
                        ++i;
                    }
                    while (i < fmt_len && std::isdigit((unsigned char)sev->format_string[i])) {
                        width = width * 10 + (sev->format_string[i] - '0');
                        ++i;
                    }
                    char spec = (i < fmt_len) ? sev->format_string[i] : ' ';

                    if (arg_idx < sev->arg_exprs.size()) {
                        LogicVal val = eval_expr(sev->arg_exprs[arg_idx++].get(), bp.scope);
                        if (spec == 's') {
                            if (val.is_string) std::cout << val.str_val;
                            else std::cout << val.aval;
                        } else if (val.is_unknown() && (spec == 'd' || spec == 't')) {
                            std::cout << "x";
                        } else if (spec == 'd' || spec == 't') {
                            if (val.is_real) {
                                std::cout << (int64_t)val.real_val;
                            } else if (val.is_signed) {
                                std::cout << val.to_signed();
                            } else {
                                std::cout << val.aval;
                            }
                        } else if (spec == 'f') {
                            std::cout << (val.is_real ? val.real_val : (double)val.aval);
                        } else if (spec == 'b') {
                            std::cout << val.to_bin_string();
                        } else if (spec == 'h' || spec == 'x') {
                            std::string hx = val.to_hex_string(true);
                            if (pad_zero && width > 0) {
                                while (hx.size() < (size_t)width) hx = "0" + hx;
                            }
                            std::cout << hx;
                        } else {
                            std::cout << val.aval;
                        }
                    }
                } else {
                    std::cout << sev->format_string[i];
                }
            }
            while (arg_idx < sev->arg_exprs.size()) {
                std::cout << " ";
                LogicVal val = eval_expr(sev->arg_exprs[arg_idx++].get(), bp.scope);
                if (val.is_string) std::cout << val.str_val;
                else if (val.is_real) std::cout << val.real_val;
                else if (val.is_unknown()) std::cout << "x";
                else if (val.is_signed) std::cout << val.to_signed();
                else std::cout << val.aval;
            }
            std::cout << "\n";
        } else if (!sev->arg_exprs.empty()) {
            for (size_t a_i = 0; a_i < sev->arg_exprs.size(); ++a_i) {
                if (a_i > 0) std::cout << " ";
                LogicVal val = eval_expr(sev->arg_exprs[a_i].get(), bp.scope);
                if (val.is_string) std::cout << val.str_val;
                else if (val.is_real) std::cout << val.real_val;
                else if (val.is_unknown()) std::cout << "x";
                else if (val.is_signed) std::cout << val.to_signed();
                else std::cout << val.aval;
            }
            std::cout << "\n";
        }

        if (sev->level == SeverityLevel::FATAL) {
            int64_t f_num = 1;
            if (sev->finish_number) {
                LogicVal fn_val = eval_expr(sev->finish_number.get(), bp.scope);
                f_num = fn_val.to_signed();
            }
            if (f_num < 0 || f_num > 2) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: $fatal finish number must be 0, 1, or 2 (got " + std::to_string(f_num) + ") per IEEE 1800-2023 section 20.10");
            }
            if (f_num == 1 || f_num == 2) {
                if (engine.is_finish_msg_allowed()) {
                    std::cout << "[CAS $fatal " << fname << ":" << sev->src_line << "] Simulation Terminated at time " << engine.get_sim_time() << ".\n";
                }
            }
            engine.set_fatal_error(1);
            engine.scheduler.terminate();
            bp.completed = true;
            return;
        }
        return;
    }

    if (auto fin = dynamic_cast<FinishStatementNode*>(stmt)) {
        if (engine.is_finish_msg_allowed()) {
            if (!fin->src_file.empty() && fin->src_line > 0) {
                std::cout << "[CAS $finish " << fin->src_file << ":" << fin->src_line << "] Simulation Terminated at time " << engine.get_sim_time() << ".\n";
            } else {
                std::cout << "[CAS $finish] Simulation Terminated at time " << engine.get_sim_time() << ".\n";
            }
        }
        engine.scheduler.terminate();
        bp.completed = true;
        return;
    }

    if (auto ex = dynamic_cast<ExitStatementNode*>(stmt)) {
        bp.completed = true;
        return;
    }

    // IEEE 1800-2023 §10.6.1: Non-blocking Assignment (with optional intra-assignment delay)
    if (auto nassign = dynamic_cast<NonBlockingAssignNode*>(stmt)) {
        if (nassign->stream_lhs) {
            execute_streaming_assign(nassign->stream_lhs.get(), nassign->rhs.get(), bp, true);
            return;
        }

        std::string target_name = nassign->target;
        std::string base_var = target_name;
        std::string member_name = "";
        auto dot_pos = target_name.rfind('.');
        if (dot_pos != std::string::npos) {
            base_var = target_name.substr(0, dot_pos);
            member_name = target_name.substr(dot_pos + 1);
        }

        if (!member_name.empty()) {
            VirtualInterfaceHandle vif = engine.lookup_virtual_interface(base_var, bp.scope, expr_eval.get_current_this(), &bp);
            if (vif.is_valid_vif) {
                size_t target_net_id = engine.resolve_vif_net_id(vif, member_name);
                if (target_net_id != (size_t)-1) {
                    LogicVal rhs_val = eval_expr(nassign->rhs.get(), bp.scope);
                    LogicVal casted = LogicOps::cast_width(rhs_val, engine.netlist[target_net_id].width, engine.netlist[target_net_id].is_signed);
                    engine.scheduler.queue_nba(target_net_id, casted);
                    return;
                }
            }

            size_t cb_id = engine.get_clocking_block_id(base_var, bp.scope);
            if (cb_id != (size_t)-1) {
                auto& cb = engine.clocking_blocks[cb_id];
                auto sig_it = cb.signals.find(member_name);
                if (sig_it != cb.signals.end()) {
                    if (sig_it->second.direction == "input") {
                        throw std::runtime_error("[CAS-DTINPCIL] Attempted to drive input clocking block signal '" + member_name + "' per IEEE 1800-2023 section 14.16");
                    }
                    size_t target_net_id = sig_it->second.net_id;
                    if (target_net_id != (size_t)-1) {
                        LogicVal rhs_val = eval_expr(nassign->rhs.get(), bp.scope);
                        LogicVal casted = LogicOps::cast_width(rhs_val, engine.netlist[target_net_id].width, engine.netlist[target_net_id].is_signed);
                        uint64_t skew_ticks = sig_it->second.skew_ticks;
                        if (cb.fired_in_time_slot) {
                            if (skew_ticks == 0) {
                                engine.scheduler.queue_nba(target_net_id, casted);
                            } else {
                                uint64_t wake_time = engine.scheduler.get_sim_time() + skew_ticks;
                                engine.scheduler.queue_delayed_nba(wake_time, target_net_id, casted);
                            }
                        } else {
                            cb.pending_drives.push_back({target_net_id, casted, skew_ticks});
                        }
                        return;
                    }
                }
            }
        }

        LocalVar* local = nullptr;
        if (expr_eval.get_context().has_active_frame()) {
            local = expr_eval.get_context().lookup_local(base_var);
        }
        if (!local && !bp.call_stack.empty()) {
            for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                local = it->local_frame.get_var(base_var);
                if (local) break;
            }
        }
        if (local) {
            LogicVal rhs_val = eval_expr(nassign->rhs.get(), bp.scope);
            if (local->is_dynamic_array) {
                std::vector<LogicVal> elems;
                if (rhs_val.is_unpacked) {
                    elems = rhs_val.unpacked_elements;
                } else {
                    elems = expr_eval.eval_array_expr(nassign->rhs.get(), bp.scope);
                }
                uint32_t ew = local->dyn_array_data.elem_width;
                bool is_s = local->dyn_array_data.is_signed;
                for (auto& el : elems) el = LogicOps::cast_width(el, ew, is_s);
                local->dyn_array_data.elements = std::move(elems);
                return;
            }
            if (local->is_unpacked_array) {
                if (nassign->bit_idx) {
                    LogicVal idx_val = eval_expr(nassign->bit_idx.get(), bp.scope);
                    if (!idx_val.is_unknown()) {
                        int64_t idx = (int64_t)idx_val.aval;
                        int64_t rel_idx = idx - local->arr_start_idx;
                        if (rel_idx >= 0 && (size_t)rel_idx < local->unpacked_elements.size()) {
                            if (nassign->bit_idx2) {
                                LogicVal msb_val = eval_expr(nassign->bit_idx2.get(), bp.scope);
                                if (!msb_val.is_unknown()) {
                                    uint32_t msb = 0, lsb = 0, slice_w = 0;
                                    if (nassign->bit_lsb2) {
                                        LogicVal lsb_val = eval_expr(nassign->bit_lsb2.get(), bp.scope);
                                        if (nassign->is_plus_colon2) {
                                            lsb = msb_val.aval;
                                            slice_w = lsb_val.aval;
                                            msb = lsb + slice_w - 1;
                                        } else if (nassign->is_minus_colon2) {
                                            slice_w = lsb_val.aval;
                                            msb = msb_val.aval;
                                            lsb = (msb >= slice_w - 1) ? (msb - (slice_w - 1)) : 0;
                                        } else {
                                            msb = msb_val.aval;
                                            lsb = lsb_val.aval;
                                            if (msb < lsb) std::swap(msb, lsb);
                                            slice_w = msb - lsb + 1;
                                        }
                                    } else {
                                        lsb = msb_val.aval;
                                        slice_w = 1;
                                        msb = lsb;
                                    }
                                    local->unpacked_elements[rel_idx].deposit_slice(lsb, slice_w, rhs_val);
                                }
                            } else {
                                local->unpacked_elements[rel_idx] = LogicOps::cast_width(rhs_val, local->elem_width, local->is_signed);
                            }
                            std::string elem_vname = base_var + "[" + std::to_string(idx) + "]";
                            if (expr_eval.get_context().has_active_frame()) {
                                LocalVar* elem_lv = expr_eval.get_context().lookup_local(elem_vname);
                                if (elem_lv) elem_lv->value = rhs_val;
                            }
                            if (!bp.call_stack.empty()) {
                                for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                                    LocalVar* elem_lv = it->local_frame.get_var(elem_vname);
                                    if (elem_lv) { elem_lv->value = rhs_val; break; }
                                }
                            }
                        }
                    }
                    return;
                }
                std::vector<LogicVal> elems;
                if (rhs_val.is_unpacked) {
                    elems = rhs_val.unpacked_elements;
                } else {
                    elems = expr_eval.eval_array_expr(nassign->rhs.get(), bp.scope);
                }
                if (elems.size() != local->unpacked_elements.size()) {
                    throw std::runtime_error("[CAS-DTINPCIL] Concatenation element count (got " +
                        std::to_string(elems.size()) + ") does not match target array size (" +
                        std::to_string(local->unpacked_elements.size()) + ") per IEEE 1800-2023 section 10.10");
                }
                for (size_t i = 0; i < local->unpacked_elements.size(); ++i) {
                    local->unpacked_elements[i] = LogicOps::cast_width(elems[i], local->elem_width, local->is_signed);
                }
                local->value = rhs_val;
                return;
            }
            if (!member_name.empty()) {
                const PackedStructType* st = nullptr;
                if (!local->type_name.empty()) {
                    if (engine.struct_types.find(local->type_name) != engine.struct_types.end()) {
                        st = &engine.struct_types.at(local->type_name);
                    }
                }
                if (st) {
                    const StructMember* sm = st->get_member(member_name);
                    if (sm) {
                        local->value.deposit_slice(sm->offset, sm->width, rhs_val);
                        return;
                    }
                }
            }
            if (local->is_class_handle && rhs_val.aval != 0) {
                auto rhs_obj = engine.get_class_object(rhs_val.aval);
                if (rhs_obj && !rhs_obj->is_instance_of(local->class_type_name)) {
                    throw std::runtime_error("[CAS-DTINPCIL] Incompatible class assignment: '" + rhs_obj->class_type_name + "' is not a subtype of '" + local->class_type_name + "' per IEEE 1800-2023 section 8.26");
                }
            }
            if (local->type_name == "string" || rhs_val.is_string) {
                local->value = rhs_val;
            } else {
                local->value = LogicOps::cast_width(rhs_val, local->width, local->is_signed);
            }
            return;
        }

        DynamicArrayObject* da_nba = engine.get_dynamic_array(target_name, bp.scope);
        if (da_nba) {
            LogicVal rhs_val = eval_expr(nassign->rhs.get(), bp.scope);
            std::vector<LogicVal> elems;
            if (rhs_val.is_unpacked) {
                elems = rhs_val.unpacked_elements;
            } else {
                elems = expr_eval.eval_array_expr(nassign->rhs.get(), bp.scope);
            }
            uint32_t ew = da_nba->elem_width;
            bool is_s = da_nba->is_signed;
            for (auto& el : elems) el = LogicOps::cast_width(el, ew, is_s);
            da_nba->elements = std::move(elems);
            return;
        }

        std::string scoped_mem_nba = bp.scope.empty() ? target_name : (bp.scope + "." + target_name);
        if (engine.memory_map.find(scoped_mem_nba) == engine.memory_map.end()) {
            std::string cur_s = bp.scope;
            while (!cur_s.empty() && engine.memory_map.find(cur_s + "." + target_name) == engine.memory_map.end()) {
                auto dot = cur_s.rfind('.');
                if (dot == std::string::npos) cur_s = "";
                else cur_s = cur_s.substr(0, dot);
            }
            if (!cur_s.empty()) scoped_mem_nba = cur_s + "." + target_name;
            else if (engine.memory_map.find(target_name) != engine.memory_map.end()) {
                scoped_mem_nba = target_name;
            }
        }
        if (engine.memory_map.find(scoped_mem_nba) != engine.memory_map.end()) {
            auto& mem = engine.memory_map.at(scoped_mem_nba);
            if (nassign->bit_idx) {
                LogicVal idx_val = eval_expr(nassign->bit_idx.get(), bp.scope);
                if (!idx_val.is_unknown()) {
                    int64_t addr = (int64_t)idx_val.aval;
                    LogicVal rhs_val = eval_expr(nassign->rhs.get(), bp.scope);
                    if (nassign->bit_idx2) {
                        LogicVal msb_val = eval_expr(nassign->bit_idx2.get(), bp.scope);
                        if (!msb_val.is_unknown()) {
                            uint32_t msb = 0, lsb = 0, slice_w = 0;
                            if (nassign->bit_lsb2) {
                                LogicVal lsb_val = eval_expr(nassign->bit_lsb2.get(), bp.scope);
                                if (nassign->is_plus_colon2) {
                                    lsb = msb_val.aval;
                                    slice_w = lsb_val.aval;
                                    msb = lsb + slice_w - 1;
                                } else if (nassign->is_minus_colon2) {
                                    slice_w = lsb_val.aval;
                                    msb = msb_val.aval;
                                    lsb = (msb >= slice_w - 1) ? (msb - (slice_w - 1)) : 0;
                                } else {
                                    msb = msb_val.aval;
                                    lsb = lsb_val.aval;
                                    if (msb < lsb) std::swap(msb, lsb);
                                    slice_w = msb - lsb + 1;
                                }
                            } else {
                                lsb = msb_val.aval;
                                slice_w = 1;
                                msb = lsb;
                            }
                            if (nassign->intra_delay > 0) {
                                uint64_t wake_time = engine.get_sim_time() + nassign->intra_delay;
                                engine.scheduler.queue_delayed_nba_mem(wake_time, scoped_mem_nba, addr, rhs_val, lsb, slice_w, true);
                            } else {
                                engine.scheduler.queue_nba_mem(scoped_mem_nba, addr, rhs_val, lsb, slice_w, true);
                            }
                        }
                    } else if (nassign->bit_lsb) {
                        LogicVal msb_val = eval_expr(nassign->bit_idx.get(), bp.scope);
                        LogicVal lsb_val = eval_expr(nassign->bit_lsb.get(), bp.scope);
                        uint32_t msb = 0, lsb = 0, slice_w = 0;
                        if (nassign->is_plus_colon) {
                            lsb = msb_val.aval;
                            slice_w = lsb_val.aval;
                            msb = lsb + slice_w - 1;
                        } else if (nassign->is_minus_colon) {
                            slice_w = lsb_val.aval;
                            msb = msb_val.aval;
                            lsb = (msb >= slice_w - 1) ? (msb - (slice_w - 1)) : 0;
                        } else {
                            msb = msb_val.aval;
                            lsb = lsb_val.aval;
                            if (msb < lsb) std::swap(msb, lsb);
                            slice_w = msb - lsb + 1;
                        }
                        if (nassign->intra_delay > 0) {
                            uint64_t wake_time = engine.get_sim_time() + nassign->intra_delay;
                            engine.scheduler.queue_delayed_nba_mem(wake_time, scoped_mem_nba, addr, rhs_val, lsb, slice_w, true);
                        } else {
                            engine.scheduler.queue_nba_mem(scoped_mem_nba, addr, rhs_val, lsb, slice_w, true);
                        }
                    } else {
                        if (nassign->intra_delay > 0) {
                            uint64_t wake_time = engine.get_sim_time() + nassign->intra_delay;
                            engine.scheduler.queue_delayed_nba_mem(wake_time, scoped_mem_nba, addr, rhs_val);
                        } else {
                            engine.scheduler.queue_nba_mem(scoped_mem_nba, addr, rhs_val);
                        }
                    }
                }
                return;
            }
            LogicVal rhs_val = eval_expr(nassign->rhs.get(), bp.scope);
            std::vector<LogicVal> elems;
            if (rhs_val.is_unpacked) {
                elems = rhs_val.unpacked_elements;
            } else {
                elems = expr_eval.eval_array_expr(nassign->rhs.get(), bp.scope);
            }
            if (elems.size() != mem.depth) {
                throw std::runtime_error("[CAS-DTINPCIL] Concatenation element count (got " +
                    std::to_string(elems.size()) + ") does not match target array size (" +
                    std::to_string(mem.depth) + ") per IEEE 1800-2023 section 10.10");
            }
            for (size_t i = 0; i < mem.depth; ++i) {
                mem.set_word(i, LogicOps::cast_width(elems[i], mem.word_width, false));
            }
            return;
        }

        std::string scoped_full = bp.scope.empty() ? target_name : (bp.scope + "." + target_name);
        size_t net_id = engine.get_net_id(bp.scope, target_name);
        bool is_member = false;
        if (net_id == (size_t)-1 && !member_name.empty()) {
            std::string scoped_base = bp.scope.empty() ? base_var : (bp.scope + "." + base_var);
            if (engine.net_map.find(scoped_base) != engine.net_map.end()) {
                net_id = engine.net_map.at(scoped_base);
                is_member = true;
            } else if (engine.net_map.find(base_var) != engine.net_map.end()) {
                net_id = engine.net_map.at(base_var);
                is_member = true;
            }
        }

        if (net_id != (size_t)-1) {
            if (engine.netlist[net_id].has_procedural_assign) return;
            LogicVal rhs_val = eval_expr(nassign->rhs.get(), bp.scope);

            if (is_member) {
                const PackedStructType* st = nullptr;
                std::string type_name = engine.netlist[net_id].type_name;
                if (type_name.empty()) {
                    std::string scoped_base = bp.scope.empty() ? base_var : (bp.scope + "." + base_var);
                    if (engine.var_to_struct_type.find(scoped_base) != engine.var_to_struct_type.end()) {
                        type_name = engine.var_to_struct_type.at(scoped_base);
                    } else if (engine.var_to_struct_type.find(base_var) != engine.var_to_struct_type.end()) {
                        type_name = engine.var_to_struct_type.at(base_var);
                    }
                }
                std::string scoped_type = bp.scope.empty() ? type_name : (bp.scope + "." + type_name);
                if (engine.struct_types.find(scoped_type) != engine.struct_types.end()) {
                    st = &engine.struct_types.at(scoped_type);
                } else if (engine.struct_types.find(type_name) != engine.struct_types.end()) {
                    st = &engine.struct_types.at(type_name);
                }

                if (st) {
                    const StructMember* sm = st->get_member(member_name);
                    if (sm) {
                        LogicVal casted = LogicOps::cast_width(rhs_val, sm->width, sm->is_signed);
                        if (nassign->has_intra_event) {
                            size_t trig_net_id = engine.get_net_id(bp.scope, nassign->intra_signal);

                            if (trig_net_id != (size_t)-1) {
                                engine.scheduler.queue_intra_event_nba(trig_net_id, nassign->intra_edge, net_id, casted, sm->offset, sm->width, true);
                            }
                        } else if (nassign->intra_delay > 0) {
                            uint64_t wake_time = engine.get_sim_time() + nassign->intra_delay;
                            engine.scheduler.queue_delayed_nba(wake_time, net_id, casted, sm->offset, sm->width, true);
                        } else {
                            engine.scheduler.queue_nba(net_id, casted, sm->offset, sm->width, true);
                        }
                        return;
                    }
                }
            }

            if (nassign->bit_idx) {
                uint32_t d1_width = 1;
                std::string full_target_name = engine.netlist[net_id].name;
                auto it_vdm = engine.var_dimensions.find(full_target_name);
                if (it_vdm == engine.var_dimensions.end()) it_vdm = engine.var_dimensions.find(bp.scope.empty() ? nassign->target : (bp.scope + "." + nassign->target));
                if (it_vdm == engine.var_dimensions.end()) it_vdm = engine.var_dimensions.find(nassign->target);
                int64_t base_low = 0;
                if (it_vdm != engine.var_dimensions.end() && !it_vdm->second.dimensions.empty()) {
                    base_low = it_vdm->second.dimensions[0].low();
                    if (it_vdm->second.dimensions.size() > 1) {
                        d1_width = (uint32_t)it_vdm->second.dimensions[1].size();
                    }
                }
                LogicVal idx_v = eval_expr(nassign->bit_idx.get(), bp.scope);
                if (!idx_v.is_unknown()) {
                    uint32_t msb = 0, lsb = 0, slice_w = 0;
                    uint32_t elem_idx = idx_v.aval;
                    if (base_low > 0 && elem_idx >= (uint32_t)base_low) elem_idx -= (uint32_t)base_low;
                    if (nassign->bit_idx2) {
                        LogicVal idx_v2 = eval_expr(nassign->bit_idx2.get(), bp.scope);
                        if (!idx_v2.is_unknown()) {
                            if (nassign->bit_lsb2) {
                                LogicVal lsb_v2 = eval_expr(nassign->bit_lsb2.get(), bp.scope);
                                if (!lsb_v2.is_unknown()) {
                                    if (nassign->is_plus_colon2) {
                                        uint32_t b_base = idx_v2.aval;
                                        slice_w = lsb_v2.aval;
                                        lsb = elem_idx * d1_width + b_base;
                                    } else if (nassign->is_minus_colon2) {
                                        slice_w = lsb_v2.aval;
                                        uint32_t b_base = idx_v2.aval;
                                        uint32_t b_lsb = (b_base >= slice_w - 1) ? (b_base - (slice_w - 1)) : 0;
                                        lsb = elem_idx * d1_width + b_lsb;
                                    } else {
                                        uint32_t b_msb = idx_v2.aval;
                                        uint32_t b_lsb = lsb_v2.aval;
                                        if (b_msb < b_lsb) std::swap(b_msb, b_lsb);
                                        slice_w = b_msb - b_lsb + 1;
                                        lsb = elem_idx * d1_width + b_lsb;
                                    }
                                }
                            } else {
                                slice_w = 1;
                                lsb = elem_idx * d1_width + idx_v2.aval;
                            }
                        }
                    } else if (nassign->bit_lsb) {
                        LogicVal lsb_v = eval_expr(nassign->bit_lsb.get(), bp.scope);
                        if (!lsb_v.is_unknown()) {
                            if (nassign->is_plus_colon) {
                                lsb = idx_v.aval;
                                slice_w = lsb_v.aval;
                                msb = lsb + slice_w - 1;
                            } else if (nassign->is_minus_colon) {
                                slice_w = lsb_v.aval;
                                msb = idx_v.aval;
                                lsb = (msb >= slice_w - 1) ? (msb - (slice_w - 1)) : 0;
                            } else {
                                msb = idx_v.aval;
                                lsb = lsb_v.aval;
                                if (msb < lsb) std::swap(msb, lsb);
                                slice_w = msb - lsb + 1;
                            }
                            if (base_low > 0 && lsb >= (uint32_t)base_low) lsb -= (uint32_t)base_low;
                        }
                    } else {
                        if (d1_width > 1) {
                            lsb = elem_idx * d1_width;
                            slice_w = d1_width;
                            msb = lsb + slice_w - 1;
                        } else {
                            lsb = elem_idx;
                            slice_w = 1;
                            msb = lsb;
                        }
                    }
                    if (slice_w > 0) {
                        LogicVal casted_slice = LogicOps::cast_width(rhs_val, slice_w, false);
                        if (nassign->intra_delay > 0) {
                            uint64_t wake_time = engine.get_sim_time() + nassign->intra_delay;
                            engine.scheduler.queue_delayed_nba(wake_time, net_id, casted_slice, lsb, slice_w, true);
                        } else {
                            engine.scheduler.queue_nba(net_id, casted_slice, lsb, slice_w, true);
                        }
                        return;
                    }
                }
            }

            LogicVal casted = (engine.netlist[net_id].type_name == "string" || rhs_val.is_string) ? rhs_val : LogicOps::cast_width(rhs_val, engine.netlist[net_id].width, engine.netlist[net_id].is_signed);
            if (nassign->has_intra_event) {
                size_t trig_net_id = engine.get_net_id(bp.scope, nassign->intra_signal);

                if (trig_net_id != (size_t)-1) {
                    engine.scheduler.queue_intra_event_nba(trig_net_id, nassign->intra_edge, net_id, casted);
                }
            } else if (nassign->intra_delay > 0) {
                uint64_t wake_time = engine.get_sim_time() + nassign->intra_delay;
                engine.scheduler.queue_delayed_nba(wake_time, net_id, casted);
            } else {
                engine.scheduler.queue_nba(net_id, casted);
            }
        }
        return;
    }

    if (auto mem_assign = dynamic_cast<MemAssignNode*>(stmt)) {
        QueueObject* q = engine.get_queue(mem_assign->mem_name, bp.scope);
        if (q) {
            LogicVal addr_val = eval_expr(mem_assign->addr_expr.get(), bp.scope);
            LogicVal rhs_val = eval_expr(mem_assign->rhs.get(), bp.scope);
            if (!addr_val.is_unknown()) {
                int64_t idx = (int64_t)addr_val.aval;
                q->set_elem(idx, rhs_val);
            }
            return;
        }
        AssociativeArrayObject* assoc = nullptr;
        if (expr_eval.get_context().has_active_frame()) {
            LocalVar* local = expr_eval.get_context().lookup_local(mem_assign->mem_name);
            if (local && local->is_associative_array) assoc = &local->assoc_array_data;
        }
        if (!assoc && !bp.call_stack.empty()) {
            for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                LocalVar* local = it->local_frame.get_var(mem_assign->mem_name);
                if (local && local->is_associative_array) {
                    assoc = &local->assoc_array_data;
                    break;
                }
            }
        }
        if (!assoc) {
            assoc = engine.get_assoc_array(mem_assign->mem_name, bp.scope);
        }
        if (assoc) {
            LogicVal addr_val = eval_expr(mem_assign->addr_expr.get(), bp.scope);
            LogicVal rhs_val = eval_expr(mem_assign->rhs.get(), bp.scope);
            if (!addr_val.is_unknown()) {
                if (assoc->is_string_key) {
                    assoc->set(addr_val.str_val, rhs_val);
                } else {
                    uint64_t key = addr_val.aval;
                    if (assoc->key_width < 64 && assoc->key_width > 0) {
                        key &= ((1ULL << assoc->key_width) - 1);
                    }
                    assoc->set(key, rhs_val);
                }
            }
            return;
        }
        std::string scoped_mem = bp.scope.empty() ? mem_assign->mem_name : (bp.scope + "." + mem_assign->mem_name);
        if (engine.memory_map.find(scoped_mem) == engine.memory_map.end()) {
            std::string cur_s = bp.scope;
            while (!cur_s.empty() && engine.memory_map.find(cur_s + "." + mem_assign->mem_name) == engine.memory_map.end()) {
                auto dot = cur_s.rfind('.');
                if (dot == std::string::npos) cur_s = "";
                else cur_s = cur_s.substr(0, dot);
            }
            if (!cur_s.empty()) scoped_mem = cur_s + "." + mem_assign->mem_name;
            else if (engine.memory_map.find(mem_assign->mem_name) != engine.memory_map.end()) {
                scoped_mem = mem_assign->mem_name;
            }
        }
        LogicVal addr_val = eval_expr(mem_assign->addr_expr.get(), bp.scope);
        LogicVal rhs_val = eval_expr(mem_assign->rhs.get(), bp.scope);

        if (!addr_val.is_unknown()) {
            int64_t addr = (int64_t)addr_val.aval;
            if (engine.memory_map.find(scoped_mem) != engine.memory_map.end()) {
                auto& mem = engine.memory_map.at(scoped_mem);
                if (!mem_assign->member_name.empty()) {
                    std::string type_name = "";
                    if (engine.var_to_struct_type.find(scoped_mem) != engine.var_to_struct_type.end()) {
                        type_name = engine.var_to_struct_type.at(scoped_mem);
                    } else if (engine.var_to_struct_type.find(mem_assign->mem_name) != engine.var_to_struct_type.end()) {
                        type_name = engine.var_to_struct_type.at(mem_assign->mem_name);
                    }
                    const PackedStructType* st = nullptr;
                    std::string scoped_type = bp.scope.empty() ? type_name : (bp.scope + "." + type_name);
                    if (engine.struct_types.find(scoped_type) != engine.struct_types.end()) {
                        st = &engine.struct_types.at(scoped_type);
                    } else if (engine.struct_types.find(type_name) != engine.struct_types.end()) {
                        st = &engine.struct_types.at(type_name);
                    }
                    if (st) {
                        const StructMember* sm = st->get_member(mem_assign->member_name);
                        if (sm) {
                            LogicVal cur_word = mem.get_word(addr);
                            cur_word.deposit_slice(sm->offset, sm->width, rhs_val);
                            LogicVal new_word = cur_word;
                            if (mem_assign->is_non_blocking) {
                                if (mem_assign->intra_delay > 0) {
                                    uint64_t wake_time = engine.get_sim_time() + mem_assign->intra_delay;
                                    engine.scheduler.queue_delayed_nba_mem(wake_time, scoped_mem, addr, new_word);
                                } else {
                                    engine.scheduler.queue_nba_mem(scoped_mem, addr, new_word);
                                }
                            } else {
                                mem.set_word(addr, new_word);
                            }
                            return;
                        }
                    }
                }
                if (mem_assign->is_non_blocking) {
                    if (mem_assign->intra_delay > 0) {
                        uint64_t wake_time = engine.get_sim_time() + mem_assign->intra_delay;
                        engine.scheduler.queue_delayed_nba_mem(wake_time, scoped_mem, addr, rhs_val);
                    } else {
                        engine.scheduler.queue_nba_mem(scoped_mem, addr, rhs_val);
                    }
                } else {
                    mem.set_word(addr, rhs_val);
                }
            } else {
                size_t net_id = engine.get_net_id(bp.scope, mem_assign->mem_name);
                if (net_id != (size_t)-1) {
                    uint32_t d1_width = 1;
                    std::string full_target_name = engine.netlist[net_id].name;
                    auto it_vdm = engine.var_dimensions.find(full_target_name);
                    if (it_vdm == engine.var_dimensions.end()) it_vdm = engine.var_dimensions.find(bp.scope.empty() ? mem_assign->mem_name : (bp.scope + "." + mem_assign->mem_name));
                    if (it_vdm == engine.var_dimensions.end()) it_vdm = engine.var_dimensions.find(mem_assign->mem_name);
                    int64_t base_low = 0;
                    if (it_vdm != engine.var_dimensions.end() && !it_vdm->second.dimensions.empty()) {
                        base_low = it_vdm->second.dimensions[0].low();
                        if (it_vdm->second.dimensions.size() > 1) {
                            d1_width = (uint32_t)it_vdm->second.dimensions[1].size();
                        }
                    }
                    int64_t elem_idx = addr;
                    if (base_low > 0 && elem_idx >= base_low) elem_idx -= base_low;
                    uint32_t offset = (uint32_t)(elem_idx * d1_width);
                    uint32_t slice_w = d1_width;
                    LogicVal casted_slice = LogicOps::cast_width(rhs_val, slice_w, false);
                    if (mem_assign->is_non_blocking) {
                        if (mem_assign->intra_delay > 0) {
                            uint64_t wake_time = engine.get_sim_time() + mem_assign->intra_delay;
                            engine.scheduler.queue_delayed_nba(wake_time, net_id, casted_slice, offset, slice_w, true);
                        } else {
                            engine.scheduler.queue_nba(net_id, casted_slice, offset, slice_w, true);
                        }
                    } else {
                        auto cur = engine.netlist[net_id].value;
                        cur.deposit_slice(offset, slice_w, casted_slice);
                        engine.netlist[net_id].set(cur);
                    }
                }
            }
        }
        return;
    }

    // IEEE 1800-2023 §10.6.1: Blocking Assignment
    if (auto assign = dynamic_cast<BlockingAssignNode*>(stmt)) {
        if (assign->stream_lhs) {
            execute_streaming_assign(assign->stream_lhs.get(), assign->rhs.get(), bp, false);
            return;
        }

        if (assign->intra_delay > 0) {
            return;
        }

        LogicVal rhs_val = eval_expr(assign->rhs.get(), bp.scope);

        if (!assign->lhs_targets.empty()) {
            std::string target_name = assign->lhs_targets[0];
            auto colons = target_name.find("::");
            if (colons != std::string::npos) {
                std::string cls_type = target_name.substr(0, colons);
                std::string s_var = target_name.substr(colons + 2);
                engine.set_class_static_var(cls_type, s_var, rhs_val);
                return;
            }
            std::string base_var = target_name;
            std::string member_name = "";
            auto dot_pos = target_name.rfind('.');
            if (dot_pos != std::string::npos) {
                base_var = target_name.substr(0, dot_pos);
                member_name = target_name.substr(dot_pos + 1);
            }
            if (!member_name.empty()) {
                size_t cb_id = engine.get_clocking_block_id(base_var, bp.scope);
                if (cb_id != (size_t)-1) {
                    auto& cb = engine.clocking_blocks[cb_id];
                    auto sig_it = cb.signals.find(member_name);
                    if (sig_it != cb.signals.end()) {
                        if (sig_it->second.direction == "input") {
                            throw std::runtime_error("[CAS-DTINPCIL] Attempted to drive input clocking block signal '" + member_name + "' per IEEE 1800-2023 section 14.16");
                        }
                        size_t target_net_id = sig_it->second.net_id;
                        if (target_net_id != (size_t)-1) {
                            LogicVal casted = LogicOps::cast_width(rhs_val, engine.netlist[target_net_id].width, engine.netlist[target_net_id].is_signed);
                            uint64_t skew_ticks = sig_it->second.skew_ticks;
                            if (cb.fired_in_time_slot) {
                                if (skew_ticks == 0) {
                                    engine.scheduler.queue_nba(target_net_id, casted);
                                } else {
                                    uint64_t wake_time = engine.scheduler.get_sim_time() + skew_ticks;
                                    engine.scheduler.queue_delayed_nba(wake_time, target_net_id, casted);
                                }
                            } else {
                                cb.pending_drives.push_back({target_net_id, casted, skew_ticks});
                            }
                            return;
                        }
                    }
                }
            }
        }

        if (assign->lhs_targets.size() > 1) {
            int shift = 0;
            for (int i = (int)assign->lhs_targets.size() - 1; i >= 0; --i) {
                std::string target_name = assign->lhs_targets[i];
                LocalVar* local = nullptr;
                if (expr_eval.get_context().has_active_frame()) {
                    local = expr_eval.get_context().lookup_local(target_name);
                }
                if (!local && !bp.call_stack.empty()) {
                    for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                        local = it->local_frame.get_var(target_name);
                        if (local) break;
                    }
                }
                if (local) {
                    uint32_t width = local->width;
                    uint64_t mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
                    local->value = LogicVal(width, (rhs_val.aval >> shift) & mask, (rhs_val.bval >> shift) & mask);
                    shift += width;
                } else {
                    std::string scoped_target = bp.scope.empty() ? target_name : (bp.scope + "." + target_name);
                    size_t net_id = (size_t)-1;
                    if (engine.net_map.find(scoped_target) != engine.net_map.end()) net_id = engine.net_map.at(scoped_target);
                    else if (engine.net_map.find(target_name) != engine.net_map.end()) net_id = engine.net_map.at(target_name);
                    if (net_id != (size_t)-1) {
                        uint32_t width = engine.netlist[net_id].width;
                        if (!engine.netlist[net_id].has_procedural_assign) {
                            uint64_t mask = (width >= 64) ? ~0ULL : ((1ULL << width) - 1);
                            engine.netlist[net_id].set(LogicVal(width, (rhs_val.aval >> shift) & mask, (rhs_val.bval >> shift) & mask));
                        }
                        shift += width;
                    } else {
                        shift += 32;
                    }
                }
            }
        } else {
            std::string target_name = assign->lhs_targets[0];
            auto colons = target_name.find("::");
            if (colons != std::string::npos) {
                std::string cls_type = target_name.substr(0, colons);
                std::string s_var = target_name.substr(colons + 2);
                engine.set_class_static_var(cls_type, s_var, rhs_val);
                return;
            }
            std::string base_var = target_name;
            std::string member_name = "";
            auto dot_pos = target_name.rfind('.');
            if (dot_pos != std::string::npos) {
                base_var = target_name.substr(0, dot_pos);
                member_name = target_name.substr(dot_pos + 1);
            }

            LocalVar* local = nullptr;
            if (expr_eval.get_context().has_active_frame()) {
                if (assign->bit_idx) {
                    LogicVal idx_v = eval_expr(assign->bit_idx.get(), bp.scope);
                    if (!idx_v.is_unknown()) {
                        std::string elem_name = base_var + "[" + std::to_string(idx_v.aval) + "]";
                        local = expr_eval.get_context().lookup_local(elem_name);
                    }
                }
                if (!local) local = expr_eval.get_context().lookup_local(base_var);
            }
            if (!local && !bp.call_stack.empty()) {
                for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                    if (assign->bit_idx) {
                        LogicVal idx_v = eval_expr(assign->bit_idx.get(), bp.scope);
                        if (!idx_v.is_unknown()) {
                            std::string elem_name = base_var + "[" + std::to_string(idx_v.aval) + "]";
                            local = it->local_frame.get_var(elem_name);
                            if (local) break;
                        }
                    }
                    local = it->local_frame.get_var(base_var);
                    if (local) break;
                }
            }

            if (auto new_node = dynamic_cast<ClassNewNode*>(assign->rhs.get())) {
                bool is_sem = (local && local->is_semaphore);
                if (!is_sem) {
                    size_t nid = engine.get_net_id(bp.scope, target_name);
                    if (nid != (size_t)-1 && engine.netlist[nid].is_semaphore) is_sem = true;
                }
                if (!is_sem && engine.is_semaphore(target_name, bp.scope)) is_sem = true;
                if (!is_sem && new_node->class_type_name == "semaphore") is_sem = true;
                if (is_sem) {
                    int keys = 0;
                    if (!new_node->args.empty()) {
                        LogicVal kval = eval_expr(new_node->args[0].get(), bp.scope);
                        keys = kval.is_unknown() ? 0 : (int)kval.aval;
                    }
                    auto sobj = engine.allocate_semaphore(keys);
                    sobj->name = target_name;
                    LogicVal handle_val(64, sobj->handle_id, 0);
                    if (local) {
                        local->value = handle_val;
                    } else {
                        size_t nid = engine.get_net_id(bp.scope, target_name);
                        if (nid != (size_t)-1) {
                            engine.netlist[nid].set(handle_val);
                        }
                    }
                    return;
                }

                bool is_mbx = (local && local->is_mailbox);
                if (!is_mbx) {
                    size_t nid = engine.get_net_id(bp.scope, target_name);
                    if (nid != (size_t)-1 && engine.netlist[nid].is_mailbox) is_mbx = true;
                }
                if (!is_mbx && engine.is_mailbox(target_name, bp.scope)) is_mbx = true;
                if (!is_mbx && new_node->class_type_name == "mailbox") is_mbx = true;
                if (is_mbx) {
                    int bound = 0;
                    if (!new_node->args.empty()) {
                        LogicVal bval = eval_expr(new_node->args[0].get(), bp.scope);
                        bound = bval.is_unknown() ? 0 : (int)bval.aval;
                    }
                    auto mobj = engine.allocate_mailbox(bound);
                    mobj->name = target_name;
                    LogicVal handle_val(64, mobj->handle_id, 0);
                    if (local) {
                        local->value = handle_val;
                    } else {
                        size_t nid = engine.get_net_id(bp.scope, target_name);
                        if (nid != (size_t)-1) {
                            engine.netlist[nid].set(handle_val);
                        }
                    }
                    return;
                }

                std::string cg_type = "";
                if (local && local->is_covergroup_handle) {
                    cg_type = local->covergroup_type_name;
                } else {
                    size_t nid = engine.get_net_id(bp.scope, target_name);
                    if (nid != (size_t)-1 && engine.netlist[nid].is_covergroup_handle) {
                        cg_type = engine.get_covergroup_type(target_name, bp.scope);
                    }
                }
                if (!cg_type.empty()) {
                    auto inst = engine.allocate_covergroup_instance(cg_type, bp.scope, new_node->args, [&](ExpressionNode* e) {
                        return this->eval_expr(e, bp.scope);
                    });
                    LogicVal handle_val(64, inst->handle_id, 0);
                    if (local) {
                        local->value = handle_val;
                    } else {
                        size_t nid = engine.get_net_id(bp.scope, target_name);
                        if (nid != (size_t)-1) {
                            engine.netlist[nid].set(handle_val);
                        }
                    }
                    return;
                }
                std::string class_type = "";
                if (local && local->is_class_handle) {
                    class_type = local->class_type_name;
                } else {
                    size_t nid = engine.get_net_id(bp.scope, target_name);
                    if (nid != (size_t)-1 && engine.netlist[nid].is_class_handle) {
                        class_type = engine.get_class_type(target_name, bp.scope);
                    }
                }
                if (!class_type.empty()) {
                    auto obj = engine.allocate_class_object(class_type);
                    FunctionNode* ctor = obj->class_def ? obj->class_def->get_constructor() : nullptr;
                    if (ctor) {
                        std::vector<LogicVal> arg_vals;
                        for (auto& a : new_node->args) {
                            arg_vals.push_back(eval_expr(a.get(), bp.scope));
                        }
                        LogicVal dummy;
                        expr_eval.eval_class_method(obj.get(), ctor, arg_vals, dummy);
                    }
                    LogicVal handle_val(64, obj->handle_id, 0);
                    if (local) {
                        local->value = handle_val;
                    } else {
                        size_t nid = engine.get_net_id(bp.scope, target_name);
                        if (nid != (size_t)-1) {
                            engine.netlist[nid].set(handle_val);
                        }
                    }
                    return;
                }
            }

            if (!member_name.empty()) {
                VirtualInterfaceHandle vif = engine.lookup_virtual_interface(base_var, bp.scope, expr_eval.get_current_this(), &bp);
                if (vif.is_valid_vif) {
                    size_t target_net_id = engine.resolve_vif_net_id(vif, member_name);
                    if (target_net_id != (size_t)-1) {
                        LogicVal casted = LogicOps::cast_width(rhs_val, engine.netlist[target_net_id].width, engine.netlist[target_net_id].is_signed);
                        engine.netlist[target_net_id].set(casted);
                        return;
                    }
                }

                ClassObject* obj = nullptr;
                if (base_var == "this" || base_var == "super") {
                    obj = expr_eval.get_current_this();
                } else {
                    LocalVar* h_local = nullptr;
                    if (expr_eval.get_context().has_active_frame()) h_local = expr_eval.get_context().lookup_local(base_var);
                    if (!h_local && !bp.call_stack.empty()) {
                        for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                            h_local = it->local_frame.get_var(base_var);
                            if (h_local) break;
                        }
                    }
                    if (h_local && h_local->is_class_handle) {
                        obj = engine.get_class_object(h_local->value.aval).get();
                    } else {
                        size_t nid = engine.get_net_id(bp.scope, base_var);
                        if (nid != (size_t)-1 && engine.netlist[nid].is_class_handle) {
                            obj = engine.get_class_object(engine.netlist[nid].value.aval).get();
                        }
                    }
                }
                if (obj) {
                    if (obj->class_def && obj->class_def->has_static_member(member_name)) {
                        engine.set_class_static_var(obj->class_type_name, member_name, rhs_val);
                        return;
                    }
                    if (engine.is_virtual_interface_handle(rhs_val.aval)) {
                        obj->set_virtual_interface(member_name, engine.get_vif_handle(rhs_val.aval));
                    }
                    obj->set_property(member_name, rhs_val);
                    return;
                }
            }

            if (member_name.empty()) {
                if (local && (local->is_virtual_interface || (local->type_name.rfind("virtual", 0) == 0))) {
                    local->is_virtual_interface = true;
                    local->value = rhs_val;
                    local->vif_handle = rhs_val.aval;
                    return;
                }
                if (expr_eval.get_current_this() && expr_eval.get_current_this()->has_virtual_interface(base_var)) {
                    expr_eval.get_current_this()->set_property(base_var, rhs_val);
                    if (engine.is_virtual_interface_handle(rhs_val.aval)) {
                        expr_eval.get_current_this()->set_virtual_interface(base_var, engine.get_vif_handle(rhs_val.aval));
                    }
                    return;
                }
            }

            if (member_name.empty() && !local) {
                if (expr_eval.get_current_this() && expr_eval.get_current_this()->has_dynamic_array(base_var)) {
                    // fall through to dynamic array handling below
                } else if (expr_eval.get_current_this() && expr_eval.get_current_this()->has_property(base_var)) {
                    if (assign->bit_idx) {
                        // bit select handled below
                    } else {
                        expr_eval.get_current_this()->set_property(base_var, rhs_val);
                        return;
                    }
                }
                if (expr_eval.get_current_this() && expr_eval.get_current_this()->class_def && expr_eval.get_current_this()->class_def->has_static_member(base_var)) {
                    engine.set_class_static_var(expr_eval.get_current_this()->class_type_name, base_var, rhs_val);
                    return;
                }
                if (expr_eval.get_current_class_def() && expr_eval.get_current_class_def()->has_static_member(base_var)) {
                    engine.set_class_static_var(expr_eval.get_current_class_def()->name, base_var, rhs_val);
                    return;
                }
            }

            if (local) {
                if (!member_name.empty()) {
                    const PackedStructType* st = nullptr;
                    if (!local->type_name.empty()) {
                        if (engine.struct_types.find(local->type_name) != engine.struct_types.end()) {
                            st = &engine.struct_types.at(local->type_name);
                        }
                    }
                    if (st) {
                        const StructMember* sm = st->get_member(member_name);
                        if (sm) {
                            local->value.deposit_slice(sm->offset, sm->width, rhs_val);
                            return;
                        }
                    }
                }
                if (local->is_associative_array) {
                    if (assign->bit_idx) {
                        LogicVal idx_val = eval_expr(assign->bit_idx.get(), bp.scope);
                        if (!idx_val.is_unknown()) {
                            if (local->assoc_array_data.is_string_key) {
                                local->assoc_array_data.set(idx_val.str_val, rhs_val);
                            } else {
                                uint64_t key = idx_val.aval;
                                if (local->assoc_array_data.key_width < 64 && local->assoc_array_data.key_width > 0) {
                                    key &= ((1ULL << local->assoc_array_data.key_width) - 1);
                                }
                                local->assoc_array_data.set(key, rhs_val);
                            }
                        }
                        return;
                    } else if (auto src_id = dynamic_cast<IdentifierNode*>(assign->rhs.get())) {
                        AssociativeArrayObject* src_assoc = nullptr;
                        if (expr_eval.get_context().has_active_frame()) {
                            LocalVar* loc = expr_eval.get_context().lookup_local(src_id->name);
                            if (loc && loc->is_associative_array) src_assoc = &loc->assoc_array_data;
                        }
                        if (!src_assoc && !bp.call_stack.empty()) {
                            for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                                LocalVar* loc = it->local_frame.get_var(src_id->name);
                                if (loc && loc->is_associative_array) {
                                    src_assoc = &loc->assoc_array_data;
                                    break;
                                }
                            }
                        }
                        if (!src_assoc) src_assoc = engine.get_assoc_array(src_id->name, bp.scope);
                        if (src_assoc) {
                            local->assoc_array_data.elements = src_assoc->elements;
                            local->assoc_array_data.str_elements = src_assoc->str_elements;
                            return;
                        }
                    }
                }
                if (local->is_queue) {
                    if (assign->bit_idx) {
                        LogicVal idx_val = eval_expr(assign->bit_idx.get(), bp.scope);
                        if (!idx_val.is_unknown()) {
                            int64_t idx = (int64_t)idx_val.aval;
                            local->queue_data.set_elem(idx, rhs_val);
                        }
                        return;
                    } else {
                        auto elems = expr_eval.eval_array_expr(assign->rhs.get(), bp.scope);
                        local->queue_data.elements.assign(elems.begin(), elems.end());
                        return;
                    }
                }
                if (local->is_dynamic_array) {
                    if (assign->bit_idx) {
                        LogicVal idx_val = eval_expr(assign->bit_idx.get(), bp.scope);
                        if (!idx_val.is_unknown()) {
                            int64_t idx = (int64_t)idx_val.aval;
                            local->dyn_array_data.set_elem(idx, rhs_val);
                        }
                        return;
                    } else if (auto new_arr = dynamic_cast<NewArrayNode*>(assign->rhs.get())) {
                        LogicVal sz_val = eval_expr(new_arr->size_expr.get(), bp.scope);
                        int64_t sz = sz_val.is_unknown() ? 0 : (int64_t)sz_val.aval;
                        if (sz < 0) sz = 0;
                        if (new_arr->copy_source_expr) {
                            DynamicArrayObject* src_da = nullptr;
                            if (auto src_id = dynamic_cast<IdentifierNode*>(new_arr->copy_source_expr.get())) {
                                if (expr_eval.get_context().has_active_frame()) {
                                    LocalVar* loc = expr_eval.get_context().lookup_local(src_id->name);
                                    if (loc && loc->is_dynamic_array) src_da = &loc->dyn_array_data;
                                }
                                if (!src_da && !bp.call_stack.empty()) {
                                    for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                                        LocalVar* loc = it->local_frame.get_var(src_id->name);
                                        if (loc && loc->is_dynamic_array) {
                                            src_da = &loc->dyn_array_data;
                                            break;
                                        }
                                    }
                                }
                                if (!src_da) {
                                    src_da = engine.get_dynamic_array(src_id->name, bp.scope);
                                }
                            }
                            if (src_da) {
                                local->dyn_array_data.resize_and_copy((size_t)sz, *src_da);
                            } else {
                                local->dyn_array_data.allocate((size_t)sz);
                            }
                        } else {
                            local->dyn_array_data.allocate((size_t)sz);
                        }
                        return;
                    } else if (auto src_id = dynamic_cast<IdentifierNode*>(assign->rhs.get())) {
                        DynamicArrayObject* src_da = nullptr;
                        if (expr_eval.get_context().has_active_frame()) {
                            LocalVar* loc = expr_eval.get_context().lookup_local(src_id->name);
                            if (loc && loc->is_dynamic_array) src_da = &loc->dyn_array_data;
                        }
                        if (!src_da && !bp.call_stack.empty()) {
                            for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                                LocalVar* loc = it->local_frame.get_var(src_id->name);
                                if (loc && loc->is_dynamic_array) {
                                    src_da = &loc->dyn_array_data;
                                    break;
                                }
                            }
                        }
                        if (!src_da) {
                            src_da = engine.get_dynamic_array(src_id->name, bp.scope);
                        }
                        if (src_da) {
                            local->dyn_array_data.elements = src_da->elements;
                            return;
                        }
                    } else {
                        std::vector<LogicVal> elems;
                        if (rhs_val.is_unpacked) {
                            elems = rhs_val.unpacked_elements;
                        } else {
                            elems = expr_eval.eval_array_expr(assign->rhs.get(), bp.scope);
                        }
                        if (!elems.empty() || dynamic_cast<MethodCallNode*>(assign->rhs.get()) || dynamic_cast<IndexSelectNode*>(assign->rhs.get()) || dynamic_cast<IdentifierNode*>(assign->rhs.get()) || dynamic_cast<UnpackedConcatNode*>(assign->rhs.get()) || dynamic_cast<ConcatNode*>(assign->rhs.get()) || dynamic_cast<ReplicateNode*>(assign->rhs.get())) {
                            uint32_t ew = local->dyn_array_data.elem_width;
                            bool is_s = local->dyn_array_data.is_signed;
                            for (auto& el : elems) {
                                el = LogicOps::cast_width(el, ew, is_s);
                            }
                            local->dyn_array_data.elements = std::move(elems);
                            return;
                        }
                    }
                } else if (local->is_unpacked_array) {
                    if (assign->bit_idx) {
                        LogicVal idx_val = eval_expr(assign->bit_idx.get(), bp.scope);
                        if (!idx_val.is_unknown()) {
                            int64_t idx = (int64_t)idx_val.aval;
                            int64_t rel_idx = idx - local->arr_start_idx;
                            if (rel_idx >= 0 && (size_t)rel_idx < local->unpacked_elements.size()) {
                                if (assign->bit_idx2) {
                                    LogicVal msb_val = eval_expr(assign->bit_idx2.get(), bp.scope);
                                    if (!msb_val.is_unknown()) {
                                        uint32_t msb = 0, lsb = 0, slice_w = 0;
                                        if (assign->bit_lsb2) {
                                            LogicVal lsb_val = eval_expr(assign->bit_lsb2.get(), bp.scope);
                                            if (assign->is_plus_colon2) {
                                                lsb = msb_val.aval;
                                                slice_w = lsb_val.aval;
                                                msb = lsb + slice_w - 1;
                                            } else if (assign->is_minus_colon2) {
                                                slice_w = lsb_val.aval;
                                                msb = msb_val.aval;
                                                lsb = (msb >= slice_w - 1) ? (msb - (slice_w - 1)) : 0;
                                            } else {
                                                msb = msb_val.aval;
                                                lsb = lsb_val.aval;
                                                if (msb < lsb) std::swap(msb, lsb);
                                                slice_w = msb - lsb + 1;
                                            }
                                        } else {
                                            lsb = msb_val.aval;
                                            slice_w = 1;
                                            msb = lsb;
                                        }
                                        local->unpacked_elements[rel_idx].deposit_slice(lsb, slice_w, rhs_val);
                                    }
                                } else {
                                    local->unpacked_elements[rel_idx] = LogicOps::cast_width(rhs_val, local->elem_width, local->is_signed);
                                }
                            }
                            std::string elem_vname = base_var + "[" + std::to_string(idx) + "]";
                            if (expr_eval.get_context().has_active_frame()) {
                                LocalVar* elem_lv = expr_eval.get_context().lookup_local(elem_vname);
                                if (elem_lv) elem_lv->value = rhs_val;
                            }
                            if (!bp.call_stack.empty()) {
                                for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                                    LocalVar* elem_lv = it->local_frame.get_var(elem_vname);
                                    if (elem_lv) { elem_lv->value = rhs_val; break; }
                                }
                            }
                        }
                    } else {
                        std::vector<LogicVal> elems;
                        if (rhs_val.is_unpacked) {
                            elems = rhs_val.unpacked_elements;
                        } else {
                            elems = expr_eval.eval_array_expr(assign->rhs.get(), bp.scope);
                        }
                        if (!elems.empty() || dynamic_cast<UnpackedConcatNode*>(assign->rhs.get()) || dynamic_cast<ConcatNode*>(assign->rhs.get()) || dynamic_cast<ReplicateNode*>(assign->rhs.get())) {
                            if (elems.size() != local->unpacked_elements.size()) {
                                throw std::runtime_error("[CAS-DTINPCIL] Concatenation element count (got " +
                                    std::to_string(elems.size()) + ") does not match target array size (" +
                                    std::to_string(local->unpacked_elements.size()) + ") per IEEE 1800-2023 section 10.10");
                            }
                            for (size_t i = 0; i < local->unpacked_elements.size(); ++i) {
                                local->unpacked_elements[i] = LogicOps::cast_width(elems[i], local->elem_width, local->is_signed);
                            }
                        } else {
                            for (size_t i = 0; i < local->unpacked_elements.size(); ++i) {
                                uint32_t ew = local->elem_width;
                                uint32_t shift = (uint32_t)((local->unpacked_elements.size() - 1 - i) * ew);
                                uint64_t mask = (ew >= 64) ? ~0ULL : ((1ULL << ew) - 1);
                                uint64_t a = (rhs_val.aval >> shift) & mask;
                                uint64_t b = (rhs_val.bval >> shift) & mask;
                                local->unpacked_elements[i] = LogicVal(ew, a, b, local->is_signed);
                            }
                        }
                        local->value = rhs_val;
                    }
                    return;
                } else if (assign->bit_idx && !local->is_class_handle) {
                    if (assign->bit_lsb) {
                        LogicVal msb_val = eval_expr(assign->bit_idx.get(), bp.scope);
                        LogicVal lsb_val = eval_expr(assign->bit_lsb.get(), bp.scope);
                        if (!msb_val.is_unknown() && !lsb_val.is_unknown()) {
                            uint32_t msb = 0, lsb = 0, slice_w = 0;
                            if (assign->is_plus_colon) {
                                lsb = msb_val.aval;
                                slice_w = lsb_val.aval;
                                msb = lsb + slice_w - 1;
                            } else if (assign->is_minus_colon) {
                                slice_w = lsb_val.aval;
                                msb = msb_val.aval;
                                lsb = (msb >= slice_w - 1) ? (msb - (slice_w - 1)) : 0;
                            } else {
                                msb = msb_val.aval;
                                lsb = lsb_val.aval;
                                if (msb < lsb) std::swap(msb, lsb);
                                slice_w = msb - lsb + 1;
                            }
                            local->value.deposit_slice(lsb, slice_w, rhs_val);
                        }
                    } else {
                        LogicVal idx_val = eval_expr(assign->bit_idx.get(), bp.scope);
                        if (!idx_val.is_unknown()) {
                            local->value.deposit_slice(idx_val.aval, 1, rhs_val);
                        }
                    }
                } else {
                    if (local->is_class_handle && rhs_val.aval != 0) {
                        auto rhs_obj = engine.get_class_object(rhs_val.aval);
                        if (rhs_obj && !rhs_obj->is_instance_of(local->class_type_name)) {
                            throw std::runtime_error("[CAS-DTINPCIL] Incompatible class assignment: '" + rhs_obj->class_type_name + "' is not a subtype of '" + local->class_type_name + "' per IEEE 1800-2023 section 8.26");
                        }
                    }
                    if (local->type_name == "string" || rhs_val.is_string) {
                local->value = rhs_val;
            } else {
                local->value = LogicOps::cast_width(rhs_val, local->width, local->is_signed);
            }
                }
            } else {
                std::string scoped_full = bp.scope.empty() ? target_name : (bp.scope + "." + target_name);
                QueueObject* q = nullptr;
                if (expr_eval.get_context().has_active_frame()) {
                    LocalVar* local = expr_eval.get_context().lookup_local(target_name);
                    if (local && local->is_queue) q = &local->queue_data;
                }
                if (!q && !bp.call_stack.empty()) {
                    for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                        LocalVar* local = it->local_frame.get_var(target_name);
                        if (local && local->is_queue) {
                            q = &local->queue_data;
                            break;
                        }
                    }
                }
                if (!q) {
                    q = engine.get_queue(target_name, bp.scope);
                }
                if (q) {
                    if (assign->bit_idx) {
                        LogicVal idx_val = eval_expr(assign->bit_idx.get(), bp.scope);
                        if (!idx_val.is_unknown()) {
                            int64_t idx = (int64_t)idx_val.aval;
                            q->set_elem(idx, rhs_val);
                        }
                        return;
                    } else {
                        auto elems = expr_eval.eval_array_expr(assign->rhs.get(), bp.scope);
                        q->elements.assign(elems.begin(), elems.end());
                        return;
                    }
                }
                DynamicArrayObject* da = nullptr;
                if (expr_eval.get_context().has_active_frame()) {
                    LocalVar* local = expr_eval.get_context().lookup_local(target_name);
                    if (local && local->is_dynamic_array) da = &local->dyn_array_data;
                }
                if (!da && !bp.call_stack.empty()) {
                    for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                        LocalVar* local = it->local_frame.get_var(target_name);
                        if (local && local->is_dynamic_array) {
                            da = &local->dyn_array_data;
                            break;
                        }
                    }
                }
                if (!da && expr_eval.get_current_this() && expr_eval.get_current_this()->has_dynamic_array(target_name)) {
                    da = expr_eval.get_current_this()->get_dynamic_array(target_name);
                }
                if (!da) {
                    da = engine.get_dynamic_array(target_name, bp.scope);
                }
                if (da) {
                    if (assign->bit_idx) {
                        LogicVal idx_val = eval_expr(assign->bit_idx.get(), bp.scope);
                        if (!idx_val.is_unknown()) {
                            int64_t idx = (int64_t)idx_val.aval;
                            da->set_elem(idx, rhs_val);
                        }
                        return;
                    } else if (auto new_arr = dynamic_cast<NewArrayNode*>(assign->rhs.get())) {
                        LogicVal sz_val = eval_expr(new_arr->size_expr.get(), bp.scope);
                        int64_t sz = sz_val.is_unknown() ? 0 : (int64_t)sz_val.aval;
                        if (sz < 0) sz = 0;
                        if (new_arr->copy_source_expr) {
                            DynamicArrayObject* src_da = nullptr;
                            if (auto src_id = dynamic_cast<IdentifierNode*>(new_arr->copy_source_expr.get())) {
                                if (expr_eval.get_context().has_active_frame()) {
                                    LocalVar* loc = expr_eval.get_context().lookup_local(src_id->name);
                                    if (loc && loc->is_dynamic_array) src_da = &loc->dyn_array_data;
                                }
                                if (!src_da && !bp.call_stack.empty()) {
                                    for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                                        LocalVar* loc = it->local_frame.get_var(src_id->name);
                                        if (loc && loc->is_dynamic_array) {
                                            src_da = &loc->dyn_array_data;
                                            break;
                                        }
                                    }
                                }
                                if (!src_da) {
                                    src_da = engine.get_dynamic_array(src_id->name, bp.scope);
                                }
                            }
                            if (src_da) {
                                da->resize_and_copy((size_t)sz, *src_da);
                            } else {
                                da->allocate((size_t)sz);
                            }
                        } else {
                            da->allocate((size_t)sz);
                        }
                        return;
                    } else if (auto src_id = dynamic_cast<IdentifierNode*>(assign->rhs.get())) {
                        DynamicArrayObject* src_da = nullptr;
                        if (expr_eval.get_context().has_active_frame()) {
                            LocalVar* loc = expr_eval.get_context().lookup_local(src_id->name);
                            if (loc && loc->is_dynamic_array) src_da = &loc->dyn_array_data;
                        }
                        if (!src_da && !bp.call_stack.empty()) {
                            for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                                LocalVar* loc = it->local_frame.get_var(src_id->name);
                                if (loc && loc->is_dynamic_array) {
                                    src_da = &loc->dyn_array_data;
                                    break;
                                }
                            }
                        }
                        if (!src_da) {
                            src_da = engine.get_dynamic_array(src_id->name, bp.scope);
                        }
                        if (src_da) {
                            da->elements = src_da->elements;
                            return;
                        }
                    } else {
                        std::vector<LogicVal> elems;
                        if (rhs_val.is_unpacked) {
                            elems = rhs_val.unpacked_elements;
                        } else {
                            elems = expr_eval.eval_array_expr(assign->rhs.get(), bp.scope);
                        }
                        if (!elems.empty() || dynamic_cast<MethodCallNode*>(assign->rhs.get()) || dynamic_cast<IndexSelectNode*>(assign->rhs.get()) || dynamic_cast<IdentifierNode*>(assign->rhs.get()) || dynamic_cast<UnpackedConcatNode*>(assign->rhs.get()) || dynamic_cast<ConcatNode*>(assign->rhs.get()) || dynamic_cast<ReplicateNode*>(assign->rhs.get())) {
                            uint32_t ew = da->elem_width;
                            bool is_s = da->is_signed;
                            for (auto& el : elems) {
                                el = LogicOps::cast_width(el, ew, is_s);
                            }
                            da->elements = std::move(elems);
                            return;
                        }
                    }
                }
                AssociativeArrayObject* assoc = nullptr;
                if (expr_eval.get_context().has_active_frame()) {
                    LocalVar* local = expr_eval.get_context().lookup_local(target_name);
                    if (local && local->is_associative_array) assoc = &local->assoc_array_data;
                }
                if (!assoc && !bp.call_stack.empty()) {
                    for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                        LocalVar* local = it->local_frame.get_var(target_name);
                        if (local && local->is_associative_array) {
                            assoc = &local->assoc_array_data;
                            break;
                        }
                    }
                }
                if (!assoc) {
                    assoc = engine.get_assoc_array(target_name, bp.scope);
                }
                if (assoc) {
                    if (assign->bit_idx) {
                        LogicVal idx_val = eval_expr(assign->bit_idx.get(), bp.scope);
                        if (!idx_val.is_unknown()) {
                            if (assoc->is_string_key) {
                                assoc->set(idx_val.str_val, rhs_val);
                            } else {
                                uint64_t key = idx_val.aval;
                                if (assoc->key_width < 64 && assoc->key_width > 0) {
                                    key &= ((1ULL << assoc->key_width) - 1);
                                }
                                assoc->set(key, rhs_val);
                            }
                        }
                        return;
                    } else if (auto src_id = dynamic_cast<IdentifierNode*>(assign->rhs.get())) {
                        AssociativeArrayObject* src_assoc = nullptr;
                        if (expr_eval.get_context().has_active_frame()) {
                            LocalVar* loc = expr_eval.get_context().lookup_local(src_id->name);
                            if (loc && loc->is_associative_array) src_assoc = &loc->assoc_array_data;
                        }
                        if (!src_assoc && !bp.call_stack.empty()) {
                            for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                                LocalVar* loc = it->local_frame.get_var(src_id->name);
                                if (loc && loc->is_associative_array) {
                                    src_assoc = &loc->assoc_array_data;
                                    break;
                                }
                            }
                        }
                        if (!src_assoc) src_assoc = engine.get_assoc_array(src_id->name, bp.scope);
                        if (src_assoc) {
                            assoc->elements = src_assoc->elements;
                            assoc->str_elements = src_assoc->str_elements;
                            return;
                        }
                    }
                }
                std::string scoped_mem = scoped_full;
                if (engine.memory_map.find(scoped_mem) == engine.memory_map.end()) {
                    std::string cur_s = bp.scope;
                    while (!cur_s.empty() && engine.memory_map.find(cur_s + "." + target_name) == engine.memory_map.end()) {
                        auto dot = cur_s.rfind('.');
                        if (dot == std::string::npos) cur_s = "";
                        else cur_s = cur_s.substr(0, dot);
                    }
                    if (!cur_s.empty()) scoped_mem = cur_s + "." + target_name;
                    else if (engine.memory_map.find(target_name) != engine.memory_map.end()) {
                        scoped_mem = target_name;
                    }
                }
                if (engine.memory_map.find(scoped_mem) != engine.memory_map.end()) {
                    auto& mem = engine.memory_map.at(scoped_mem);
                    if (assign->bit_idx) {
                        LogicVal idx_val = eval_expr(assign->bit_idx.get(), bp.scope);
                        if (!idx_val.is_unknown()) {
                            int64_t addr = (int64_t)idx_val.aval;
                            if (assign->bit_idx2) {
                                LogicVal word_val = mem.get_word(addr);
                                LogicVal msb_val = eval_expr(assign->bit_idx2.get(), bp.scope);
                                if (!msb_val.is_unknown()) {
                                    uint32_t msb = 0, lsb = 0, slice_w = 0;
                                    if (assign->bit_lsb2) {
                                        LogicVal lsb_val = eval_expr(assign->bit_lsb2.get(), bp.scope);
                                        if (assign->is_plus_colon2) {
                                            lsb = msb_val.aval;
                                            slice_w = lsb_val.aval;
                                            msb = lsb + slice_w - 1;
                                        } else if (assign->is_minus_colon2) {
                                            slice_w = lsb_val.aval;
                                            msb = msb_val.aval;
                                            lsb = (msb >= slice_w - 1) ? (msb - (slice_w - 1)) : 0;
                                        } else {
                                            msb = msb_val.aval;
                                            lsb = lsb_val.aval;
                                            if (msb < lsb) std::swap(msb, lsb);
                                            slice_w = msb - lsb + 1;
                                        }
                                    } else {
                                        lsb = msb_val.aval;
                                        slice_w = 1;
                                        msb = lsb;
                                    }
                                    word_val.deposit_slice(lsb, slice_w, rhs_val);
                                    mem.set_word(addr, word_val);
                                }
                            } else if (assign->bit_lsb) {
                                LogicVal word_val = mem.get_word(addr);
                                LogicVal msb_val = eval_expr(assign->bit_idx.get(), bp.scope);
                                LogicVal lsb_val = eval_expr(assign->bit_lsb.get(), bp.scope);
                                uint32_t msb = 0, lsb = 0, slice_w = 0;
                                if (assign->is_plus_colon) {
                                    lsb = msb_val.aval;
                                    slice_w = lsb_val.aval;
                                    msb = lsb + slice_w - 1;
                                } else if (assign->is_minus_colon) {
                                    slice_w = lsb_val.aval;
                                    msb = msb_val.aval;
                                    lsb = (msb >= slice_w - 1) ? (msb - (slice_w - 1)) : 0;
                                } else {
                                    msb = msb_val.aval;
                                    lsb = lsb_val.aval;
                                    if (msb < lsb) std::swap(msb, lsb);
                                    slice_w = msb - lsb + 1;
                                }
                                word_val.deposit_slice(lsb, slice_w, rhs_val);
                                mem.set_word(addr, word_val);
                            } else {
                                mem.set_word(addr, rhs_val);
                            }
                        }
                        return;
                    } else {
                        std::vector<LogicVal> elems;
                        if (rhs_val.is_unpacked) {
                            elems = rhs_val.unpacked_elements;
                        } else {
                            elems = expr_eval.eval_array_expr(assign->rhs.get(), bp.scope);
                        }
                        if (!elems.empty() || dynamic_cast<UnpackedConcatNode*>(assign->rhs.get()) || dynamic_cast<ConcatNode*>(assign->rhs.get()) || dynamic_cast<ReplicateNode*>(assign->rhs.get())) {
                            if (elems.size() != mem.depth) {
                                throw std::runtime_error("[CAS-DTINPCIL] Concatenation element count (got " +
                                    std::to_string(elems.size()) + ") does not match target array size (" +
                                    std::to_string(mem.depth) + ") per IEEE 1800-2023 section 10.10");
                            }
                            for (size_t i = 0; i < mem.depth; ++i) {
                                mem.set_word(i, LogicOps::cast_width(elems[i], mem.word_width, false));
                            }
                        } else {
                            uint32_t ew = mem.word_width;
                            for (size_t i = 0; i < mem.depth; ++i) {
                                uint32_t shift = (uint32_t)((mem.depth - 1 - i) * ew);
                                uint64_t mask = (ew >= 64) ? ~0ULL : ((1ULL << ew) - 1);
                                uint64_t a = (rhs_val.aval >> shift) & mask;
                                uint64_t b = (rhs_val.bval >> shift) & mask;
                                mem.set_word(i, LogicVal(ew, a, b));
                            }
                        }
                    }
                    return;
                }
                size_t net_id = engine.get_net_id(bp.scope, target_name);
                bool is_member = false;
                if (net_id == (size_t)-1 && !member_name.empty()) {
                    std::string scoped_base = bp.scope.empty() ? base_var : (bp.scope + "." + base_var);
                    if (engine.net_map.find(scoped_base) != engine.net_map.end()) {
                        net_id = engine.net_map.at(scoped_base);
                        is_member = true;
                    } else if (engine.net_map.find(base_var) != engine.net_map.end()) {
                        net_id = engine.net_map.at(base_var);
                        is_member = true;
                    }
                }

                if (net_id != (size_t)-1) {
                    if (engine.netlist[net_id].has_procedural_assign) return;
                    if (is_member) {
                        const PackedStructType* st = nullptr;
                        std::string type_name = engine.netlist[net_id].type_name;
                        if (type_name.empty()) {
                            std::string scoped_base = bp.scope.empty() ? base_var : (bp.scope + "." + base_var);
                            if (engine.var_to_struct_type.find(scoped_base) != engine.var_to_struct_type.end()) {
                                type_name = engine.var_to_struct_type.at(scoped_base);
                            } else if (engine.var_to_struct_type.find(base_var) != engine.var_to_struct_type.end()) {
                                type_name = engine.var_to_struct_type.at(base_var);
                            }
                        }
                        std::string scoped_type = bp.scope.empty() ? type_name : (bp.scope + "." + type_name);
                        if (engine.struct_types.find(scoped_type) != engine.struct_types.end()) {
                            st = &engine.struct_types.at(scoped_type);
                        } else if (engine.struct_types.find(type_name) != engine.struct_types.end()) {
                            st = &engine.struct_types.at(type_name);
                        }

                        if (st) {
                            const StructMember* sm = st->get_member(member_name);
                            if (sm) {
                                auto cur = engine.netlist[net_id].value;
                                cur.deposit_slice(sm->offset, sm->width, rhs_val);
                                engine.netlist[net_id].set(cur);
                                return;
                            }
                        }
                    }
                    if (assign->bit_idx) {
                        uint32_t d1_width = 1;
                        std::string full_target_name = engine.netlist[net_id].name;
                        auto it_vdm = engine.var_dimensions.find(full_target_name);
                        if (it_vdm == engine.var_dimensions.end()) it_vdm = engine.var_dimensions.find(bp.scope.empty() ? target_name : (bp.scope + "." + target_name));
                        if (it_vdm == engine.var_dimensions.end()) it_vdm = engine.var_dimensions.find(target_name);
                        int64_t base_low = 0;
                        if (it_vdm != engine.var_dimensions.end() && !it_vdm->second.dimensions.empty()) {
                            base_low = it_vdm->second.dimensions[0].low();
                            if (it_vdm->second.dimensions.size() > 1) {
                                d1_width = (uint32_t)it_vdm->second.dimensions[1].size();
                            }
                        }
                        if (assign->bit_idx2) {
                            LogicVal idx_v = eval_expr(assign->bit_idx.get(), bp.scope);
                            LogicVal idx_v2 = eval_expr(assign->bit_idx2.get(), bp.scope);
                            if (!idx_v.is_unknown() && !idx_v2.is_unknown()) {
                                uint32_t elem_idx = idx_v.aval;
                                if (base_low > 0 && elem_idx >= (uint32_t)base_low) elem_idx -= (uint32_t)base_low;
                                uint32_t lsb = 0, slice_w = 1;
                                if (assign->bit_lsb2) {
                                    LogicVal lsb_v2 = eval_expr(assign->bit_lsb2.get(), bp.scope);
                                    if (!lsb_v2.is_unknown()) {
                                        if (assign->is_plus_colon2) {
                                            uint32_t b_base = idx_v2.aval;
                                            slice_w = lsb_v2.aval;
                                            lsb = elem_idx * d1_width + b_base;
                                        } else if (assign->is_minus_colon2) {
                                            slice_w = lsb_v2.aval;
                                            uint32_t b_base = idx_v2.aval;
                                            uint32_t b_lsb = (b_base >= slice_w - 1) ? (b_base - (slice_w - 1)) : 0;
                                            lsb = elem_idx * d1_width + b_lsb;
                                        } else {
                                            uint32_t b_msb = idx_v2.aval;
                                            uint32_t b_lsb = lsb_v2.aval;
                                            if (b_msb < b_lsb) std::swap(b_msb, b_lsb);
                                            slice_w = b_msb - b_lsb + 1;
                                            lsb = elem_idx * d1_width + b_lsb;
                                        }
                                    }
                                } else {
                                    slice_w = 1;
                                    lsb = elem_idx * d1_width + idx_v2.aval;
                                }
                                auto cur = engine.netlist[net_id].value;
                                cur.deposit_slice(lsb, slice_w, rhs_val);
                                engine.netlist[net_id].set(cur);
                                return;
                            }
                        } else if (assign->bit_lsb) {
                            LogicVal msb_val = eval_expr(assign->bit_idx.get(), bp.scope);
                            LogicVal lsb_val = eval_expr(assign->bit_lsb.get(), bp.scope);
                            if (!msb_val.is_unknown() && !lsb_val.is_unknown()) {
                                uint32_t msb = 0, lsb = 0, slice_w = 0;
                                if (assign->is_plus_colon) {
                                    lsb = msb_val.aval;
                                    slice_w = lsb_val.aval;
                                    msb = lsb + slice_w - 1;
                                } else if (assign->is_minus_colon) {
                                    slice_w = lsb_val.aval;
                                    msb = msb_val.aval;
                                    lsb = (msb >= slice_w - 1) ? (msb - (slice_w - 1)) : 0;
                                } else {
                                    msb = msb_val.aval;
                                    lsb = lsb_val.aval;
                                    if (msb < lsb) std::swap(msb, lsb);
                                    slice_w = msb - lsb + 1;
                                }
                                if (base_low > 0 && lsb >= (uint32_t)base_low) lsb -= (uint32_t)base_low;
                                size_t target_id = (net_id < engine.canonical_net_map.size()) ? engine.canonical_net_map[net_id] : net_id;
                                auto cur = engine.netlist[target_id].value;
                                cur.deposit_slice(lsb, slice_w, rhs_val);
                                engine.netlist[target_id].set(cur);
                                if (target_id < engine.alias_members.size()) {
                                    for (size_t other_id : engine.alias_members[target_id]) {
                                        if (other_id != target_id && other_id < engine.netlist.size()) {
                                            engine.netlist[other_id].set(cur);
                                        }
                                    }
                                }
                                return;
                            }
                        } else {
                            LogicVal idx_val = eval_expr(assign->bit_idx.get(), bp.scope);
                            if (!idx_val.is_unknown()) {
                                uint32_t elem_idx = idx_val.aval;
                                if (base_low > 0 && elem_idx >= (uint32_t)base_low) elem_idx -= (uint32_t)base_low;
                                uint32_t lsb = 0, slice_w = 1;
                                if (d1_width > 1) {
                                    lsb = elem_idx * d1_width;
                                    slice_w = d1_width;
                                } else {
                                    lsb = elem_idx;
                                    slice_w = 1;
                                }
                                size_t target_id = (net_id < engine.canonical_net_map.size()) ? engine.canonical_net_map[net_id] : net_id;
                                auto cur = engine.netlist[target_id].value;
                                cur.deposit_slice(lsb, slice_w, rhs_val);
                                engine.netlist[target_id].set(cur);
                                if (target_id < engine.alias_members.size()) {
                                    for (size_t other_id : engine.alias_members[target_id]) {
                                        if (other_id != target_id && other_id < engine.netlist.size()) {
                                            engine.netlist[other_id].set(cur);
                                        }
                                    }
                                }
                                return;
                            }
                        }
                    } else {
                        if (engine.netlist[net_id].is_class_handle && rhs_val.aval != 0) {
                            auto rhs_obj = engine.get_class_object(rhs_val.aval);
                            std::string net_type = engine.get_class_type(target_name, bp.scope);
                            if (rhs_obj && !net_type.empty() && !rhs_obj->is_instance_of(net_type)) {
                                throw std::runtime_error("[CAS-DTINPCIL] Incompatible class assignment: '" + rhs_obj->class_type_name + "' is not a subtype of '" + net_type + "' per IEEE 1800-2023 section 8.26");
                            }
                        }
                        size_t target_id = (net_id < engine.canonical_net_map.size()) ? engine.canonical_net_map[net_id] : net_id;
                        if (!engine.netlist[target_id].has_procedural_assign) {
                            LogicVal casted = (engine.netlist[target_id].type_name == "string" || rhs_val.is_string) ? rhs_val : LogicOps::cast_width(rhs_val, engine.netlist[target_id].width, engine.netlist[target_id].is_signed);
                            engine.netlist[target_id].set(casted);
                            if (target_id < engine.alias_members.size()) {
                                for (size_t other_id : engine.alias_members[target_id]) {
                                    if (other_id != target_id && other_id < engine.netlist.size()) {
                                        engine.netlist[other_id].set(casted);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        return;
    }

    // IEEE 1800-2023 §10.4.2: Procedural Continuous Assignment
    if (auto passign = dynamic_cast<ProceduralAssignNode*>(stmt)) {
        size_t net_id = (size_t)-1;
        std::string scoped_target = bp.scope.empty() ? passign->target : (bp.scope + "." + passign->target);
        if (engine.net_map.find(scoped_target) != engine.net_map.end()) {
            net_id = engine.net_map.at(scoped_target);
        } else if (engine.net_map.find(passign->target) != engine.net_map.end()) {
            net_id = engine.net_map.at(passign->target);
        } else {
            for (const auto& kv : engine.net_map) {
                if (kv.first.size() > passign->target.size() &&
                    kv.first.rfind("." + passign->target) == kv.first.size() - (passign->target.size() + 1)) {
                    net_id = kv.second;
                    break;
                }
            }
        }

        if (net_id != (size_t)-1) {
            LogicVal rhs_val = eval_expr(passign->rhs.get(), bp.scope);
            auto connected = get_connected_nets(net_id);
            for (size_t cid : connected) {
                auto& net = engine.netlist[cid];
                net.has_procedural_assign = true;
                net.procedural_assign_rhs = passign->rhs.get();
                net.procedural_assign_scope = bp.scope;
                LogicVal casted = LogicOps::cast_width(rhs_val, net.width, net.is_signed);
                net.set(casted);
            }
        }
        return;
    }

    if (auto pdeassign = dynamic_cast<ProceduralDeassignNode*>(stmt)) {
        size_t net_id = (size_t)-1;
        std::string scoped_target = bp.scope.empty() ? pdeassign->target : (bp.scope + "." + pdeassign->target);
        if (engine.net_map.find(scoped_target) != engine.net_map.end()) {
            net_id = engine.net_map.at(scoped_target);
        } else if (engine.net_map.find(pdeassign->target) != engine.net_map.end()) {
            net_id = engine.net_map.at(pdeassign->target);
        } else {
            for (const auto& kv : engine.net_map) {
                if (kv.first.size() > pdeassign->target.size() &&
                    kv.first.rfind("." + pdeassign->target) == kv.first.size() - (pdeassign->target.size() + 1)) {
                    net_id = kv.second;
                    break;
                }
            }
        }

        if (net_id != (size_t)-1) {
            auto connected = get_connected_nets(net_id);
            for (size_t cid : connected) {
                auto& net = engine.netlist[cid];
                net.has_procedural_assign = false;
                net.procedural_assign_rhs = nullptr;
                net.procedural_assign_scope = "";
            }
        }
        return;
    }
}

void Evaluator::execute_streaming_assign(StreamingConcatNode* slhs, ExpressionNode* rhs, BoundProcess& bp, bool is_nba) {
    if (!slhs || !rhs) return;

    int slice_sz = 1;
    if (slhs->slice_size) {
        LogicVal sz_v = eval_expr(slhs->slice_size.get(), bp.scope);
        slice_sz = (int)sz_v.to_signed();
        if (slice_sz <= 0) {
            throw std::runtime_error("[CAS-DTINPCIL] Invalid streaming slice size <= 0 (" + std::to_string(slice_sz) + ") per IEEE 1800-2023 section 11.4.14");
        }
    }

    std::vector<std::pair<bool, bool>> rhs_bits;
    expr_eval.get_stream_bits(rhs, bp.scope, rhs_bits);

    std::vector<std::pair<bool, bool>> stream_bits;
    if (slhs->direction == StreamDirection::LEFT_TO_RIGHT) {
        stream_bits = std::move(rhs_bits);
    } else {
        // RIGHT_TO_LEFT (<<)
        int N = (int)rhs_bits.size();
        std::vector<std::vector<std::pair<bool, bool>>> slices;
        int cur = N;
        while (cur > 0) {
            int start = std::max(0, cur - slice_sz);
            std::vector<std::pair<bool, bool>> sl(rhs_bits.begin() + start, rhs_bits.begin() + cur);
            slices.push_back(sl);
            cur = start;
        }
        for (const auto& sl : slices) {
            stream_bits.insert(stream_bits.end(), sl.begin(), sl.end());
        }
    }

    size_t bit_cursor = 0;
    for (const auto& item_expr : slhs->stream_items) {
        if (!item_expr) continue;

        if (auto id = dynamic_cast<IdentifierNode*>(item_expr.get())) {
            std::string tname = id->name;
            LocalVar* local = nullptr;
            if (expr_eval.get_context().has_active_frame()) {
                local = expr_eval.get_context().lookup_local(tname);
            }
            if (!local && !bp.call_stack.empty()) {
                for (auto it = bp.call_stack.rbegin(); it != bp.call_stack.rend(); ++it) {
                    local = it->local_frame.get_var(tname);
                    if (local) break;
                }
            }

            if (local && local->is_queue) {
                uint32_t elem_w = local->width;
                if (elem_w == 0) elem_w = 8;
                size_t num_elems = (stream_bits.size() > bit_cursor) ? ((stream_bits.size() - bit_cursor) / elem_w) : 0;
                local->queue_data.elements.clear();
                for (size_t e = 0; e < num_elems; ++e) {
                    uint64_t a = 0, b = 0;
                    for (size_t bi = 0; bi < elem_w; ++bi) {
                        if (bit_cursor < stream_bits.size()) {
                            if (stream_bits[bit_cursor].first) a |= (1ULL << (elem_w - 1 - bi));
                            if (stream_bits[bit_cursor].second) b |= (1ULL << (elem_w - 1 - bi));
                            bit_cursor++;
                        }
                    }
                    local->queue_data.elements.push_back(LogicVal(elem_w, a, b, local->is_signed));
                }
                continue;
            }

            if (local && local->is_dynamic_array) {
                uint32_t elem_w = local->width;
                if (elem_w == 0) elem_w = 8;
                size_t num_elems = (stream_bits.size() > bit_cursor) ? ((stream_bits.size() - bit_cursor) / elem_w) : 0;
                local->dyn_array_data.elements.clear();
                for (size_t e = 0; e < num_elems; ++e) {
                    uint64_t a = 0, b = 0;
                    for (size_t bi = 0; bi < elem_w; ++bi) {
                        if (bit_cursor < stream_bits.size()) {
                            if (stream_bits[bit_cursor].first) a |= (1ULL << (elem_w - 1 - bi));
                            if (stream_bits[bit_cursor].second) b |= (1ULL << (elem_w - 1 - bi));
                            bit_cursor++;
                        }
                    }
                    local->dyn_array_data.elements.push_back(LogicVal(elem_w, a, b, local->is_signed));
                }
                continue;
            }

            QueueObject* q = engine.get_queue(tname, bp.scope);
            if (q) {
                uint32_t elem_w = q->elem_width;
                if (elem_w == 0) elem_w = 8;
                size_t num_elems = (stream_bits.size() > bit_cursor) ? ((stream_bits.size() - bit_cursor) / elem_w) : 0;
                q->elements.clear();
                for (size_t e = 0; e < num_elems; ++e) {
                    uint64_t a = 0, b = 0;
                    for (size_t bi = 0; bi < elem_w; ++bi) {
                        if (bit_cursor < stream_bits.size()) {
                            if (stream_bits[bit_cursor].first) a |= (1ULL << (elem_w - 1 - bi));
                            if (stream_bits[bit_cursor].second) b |= (1ULL << (elem_w - 1 - bi));
                            bit_cursor++;
                        }
                    }
                    q->elements.push_back(LogicVal(elem_w, a, b, q->is_signed));
                }
                continue;
            }

            DynamicArrayObject* da = engine.get_dynamic_array(tname, bp.scope);
            if (da) {
                uint32_t elem_w = da->elem_width;
                if (elem_w == 0) elem_w = 8;
                size_t num_elems = (stream_bits.size() > bit_cursor) ? ((stream_bits.size() - bit_cursor) / elem_w) : 0;
                da->elements.clear();
                for (size_t e = 0; e < num_elems; ++e) {
                    uint64_t a = 0, b = 0;
                    for (size_t bi = 0; bi < elem_w; ++bi) {
                        if (bit_cursor < stream_bits.size()) {
                            if (stream_bits[bit_cursor].first) a |= (1ULL << (elem_w - 1 - bi));
                            if (stream_bits[bit_cursor].second) b |= (1ULL << (elem_w - 1 - bi));
                            bit_cursor++;
                        }
                    }
                    da->elements.push_back(LogicVal(elem_w, a, b, da->is_signed));
                }
                continue;
            }

            std::string scoped_tname = bp.scope.empty() ? tname : (bp.scope + "." + tname);
            auto it_mem = engine.memory_map.find(scoped_tname);
            if (it_mem == engine.memory_map.end()) it_mem = engine.memory_map.find(tname);
            if (it_mem != engine.memory_map.end()) {
                auto& mem = it_mem->second;
                for (size_t i = 0; i < mem.words.size(); ++i) {
                    if (bit_cursor >= stream_bits.size()) break;
                    uint32_t ew = mem.word_width;
                    uint64_t a = 0, b = 0;
                    for (size_t bi = 0; bi < ew; ++bi) {
                        if (bit_cursor < stream_bits.size()) {
                            if (stream_bits[bit_cursor].first) a |= (1ULL << (ew - 1 - bi));
                            if (stream_bits[bit_cursor].second) b |= (1ULL << (ew - 1 - bi));
                            bit_cursor++;
                        }
                    }
                    if (i < mem.words.size()) mem.words[i] = LogicVal(ew, a, b);
                }
                continue;
            }

            if (local) {
                uint32_t w = local->width;
                uint64_t a = 0, b = 0;
                for (size_t bi = 0; bi < w; ++bi) {
                    if (bit_cursor < stream_bits.size()) {
                        if (stream_bits[bit_cursor].first) a |= (1ULL << (w - 1 - bi));
                        if (stream_bits[bit_cursor].second) b |= (1ULL << (w - 1 - bi));
                        bit_cursor++;
                    }
                }
                local->value = LogicVal(w, a, b, local->is_signed);
                continue;
            }

            size_t net_id = engine.get_net_id(bp.scope, tname);
            if (net_id != (size_t)-1) {
                uint32_t w = engine.netlist[net_id].width;
                uint64_t a = 0, b = 0;
                for (size_t bi = 0; bi < w; ++bi) {
                    if (bit_cursor < stream_bits.size()) {
                        if (stream_bits[bit_cursor].first) a |= (1ULL << (w - 1 - bi));
                        if (stream_bits[bit_cursor].second) b |= (1ULL << (w - 1 - bi));
                        bit_cursor++;
                    }
                }
                LogicVal val(w, a, b, engine.netlist[net_id].is_signed);
                if (is_nba) {
                    engine.scheduler.queue_nba(net_id, val);
                } else {
                    engine.netlist[net_id].set(val);
                }
                continue;
            }
        }

        if (auto idx_node = dynamic_cast<IndexSelectNode*>(item_expr.get())) {
            std::string tname = idx_node->name;
            LogicVal idx_val = eval_expr(idx_node->msb_expr.get(), bp.scope);
            int64_t idx = (int64_t)idx_val.to_signed();
            std::string scoped_tname = bp.scope.empty() ? tname : (bp.scope + "." + tname);
            auto it_mem = engine.memory_map.find(scoped_tname);
            if (it_mem == engine.memory_map.end()) it_mem = engine.memory_map.find(tname);
            if (it_mem != engine.memory_map.end()) {
                auto& mem = it_mem->second;
                uint32_t ew = mem.word_width;
                uint64_t a = 0, b = 0;
                for (size_t bi = 0; bi < ew; ++bi) {
                    if (bit_cursor < stream_bits.size()) {
                        if (stream_bits[bit_cursor].first) a |= (1ULL << (ew - 1 - bi));
                        if (stream_bits[bit_cursor].second) b |= (1ULL << (ew - 1 - bi));
                        bit_cursor++;
                    }
                }
                mem.set_word(idx, LogicVal(ew, a, b));
                continue;
            }
        }
    }
}
