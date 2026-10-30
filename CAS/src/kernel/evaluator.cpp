#include "../../include/kernel/evaluator.hpp"
#include "../../include/engine.hpp"
#include "../../include/kernel/logic_ops.hpp"
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
    const auto& net = engine.netlist[ref.net_id];
    const auto& net_val = net.value;
    uint64_t mask = (ref.width >= 64) ? ~0ULL : ((1ULL << ref.width) - 1);
    uint64_t a = (net_val.aval >> ref.offset) & mask;
    uint64_t b = (net_val.bval >> ref.offset) & mask;
    return LogicVal(ref.width, a, b);
}

void Evaluator::write_slice(const NetSliceRef& ref, const LogicVal& val) {
    auto& target_net = engine.netlist[ref.net_id];
    if (target_net.has_procedural_assign) return;
    LogicVal casted = LogicOps::cast_width(val, ref.width, target_net.is_signed);

    auto cur = target_net.value;
    uint64_t mask = (ref.width >= 64) ? ~0ULL : ((1ULL << ref.width) - 1);
    uint64_t clear_mask = ~(mask << ref.offset);

    uint64_t new_a = (cur.aval & clear_mask) | ((casted.aval & mask) << ref.offset);
    uint64_t new_b = (cur.bval & clear_mask) | ((casted.bval & mask) << ref.offset);
    target_net.set(LogicVal(cur.width, new_a, new_b));
}

LogicVal Evaluator::eval_expr(ExpressionNode* expr, const std::string& scope) {
    return expr_eval.eval(expr, scope);
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
                char spec = engine.monitor_session.format_string[++i];
                if (spec == '%') {
                    std::cout << '%';
                    continue;
                }
                bool unpadded = false;
                if (spec == '0' && i + 1 < fmt_len) {
                    char next_spec = engine.monitor_session.format_string[i + 1];
                    if (next_spec == 't' || next_spec == 'h' || next_spec == 'd' || next_spec == 'b' || next_spec == 's' || next_spec == 'f') {
                        spec = next_spec;
                        unpadded = true;
                        ++i;
                    }
                }

                if (arg_idx < current_values.size()) {
                    const auto& val = current_values[arg_idx++];
                    if (spec == 's') {
                        if (val.is_string) std::cout << val.str_val;
                        else std::cout << val.aval;
                    } else if (val.is_unknown() && (spec == 'd' || spec == 't')) {
                        std::cout << "x";
                    } else {
                        if (spec == 'd' || spec == 't') std::cout << (val.is_real ? (int64_t)val.real_val : val.aval);
                        else if (spec == 'f') std::cout << (val.is_real ? val.real_val : (double)val.aval);
                        else if (spec == 'b') std::cout << val.to_bin_string();
                        else if (spec == 'h') std::cout << val.to_hex_string(unpadded);
                        else std::cout << val.aval;
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

    if (auto ret_node = dynamic_cast<ReturnStatementNode*>(stmt)) {
        if (expr_eval.get_context().has_active_frame()) {
            ScopeFrame* frame = expr_eval.get_context().current_frame();
            if (frame) {
                if (ret_node->expr) {
                    LogicVal ret_val = eval_expr(ret_node->expr.get(), bp.scope);
                    expr_eval.get_context().set_local_value(frame->func_return_name, ret_val);
                }
                frame->returned = true;
            }
        }
        return;
    }
    if (auto blk = dynamic_cast<BlockStatementNode*>(stmt)) {
        for (auto& s : blk->statements) {
            execute_statement(s.get(), bp);
            if (!engine.scheduler.is_active()) return;
        }
        return;
    }

    if (auto vdecl = dynamic_cast<VarDeclStatementNode*>(stmt)) {
        if (expr_eval.get_context().has_active_frame()) {
            expr_eval.get_context().declare_local(vdecl->var_name, vdecl->width, vdecl->is_signed, vdecl->type_name);
            if (vdecl->is_queue) {
                LocalVar* lv = expr_eval.get_context().lookup_local(vdecl->var_name);
                if (lv) {
                    lv->is_queue = true;
                    lv->queue_data = QueueObject(vdecl->var_name, vdecl->width, vdecl->is_signed);
                }
            }
            if (vdecl->is_dynamic_array) {
                LocalVar* lv = expr_eval.get_context().lookup_local(vdecl->var_name);
                if (lv) {
                    lv->is_dynamic_array = true;
                    lv->dyn_array_data = DynamicArrayObject(vdecl->var_name, vdecl->width, vdecl->is_signed);
                }
            }
            if (vdecl->is_associative_array) {
                LocalVar* lv = expr_eval.get_context().lookup_local(vdecl->var_name);
                if (lv) {
                    lv->is_associative_array = true;
                    lv->assoc_array_data = AssociativeArrayObject(vdecl->var_name, vdecl->width, vdecl->is_signed, vdecl->assoc_key_type, vdecl->assoc_key_width);
                }
            }
            if (vdecl->is_class_handle) {
                LocalVar* lv = expr_eval.get_context().lookup_local(vdecl->var_name);
                if (lv) {
                    lv->is_class_handle = true;
                    lv->class_type_name = vdecl->class_type_name;
                    lv->value = LogicVal(64, 0, 0);
                }
            }
            return;
        }
        if (!bp.call_stack.empty()) {
            bp.call_stack.back().local_frame.declare_var(vdecl->var_name, vdecl->width, vdecl->is_signed, vdecl->type_name, vdecl->is_queue, vdecl->is_dynamic_array, vdecl->is_associative_array, vdecl->assoc_key_type, vdecl->assoc_key_width);
            return;
        }
        expr_eval.get_context().push_frame(bp.scope);
        expr_eval.get_context().declare_local(vdecl->var_name, vdecl->width, vdecl->is_signed, vdecl->type_name);
        if (vdecl->is_queue) {
            LocalVar* lv = expr_eval.get_context().lookup_local(vdecl->var_name);
            if (lv) {
                lv->is_queue = true;
                lv->queue_data = QueueObject(vdecl->var_name, vdecl->width, vdecl->is_signed);
            }
        }
        if (vdecl->is_dynamic_array) {
            LocalVar* lv = expr_eval.get_context().lookup_local(vdecl->var_name);
            if (lv) {
                lv->is_dynamic_array = true;
                lv->dyn_array_data = DynamicArrayObject(vdecl->var_name, vdecl->width, vdecl->is_signed);
            }
        }
        if (vdecl->is_associative_array) {
            LocalVar* lv = expr_eval.get_context().lookup_local(vdecl->var_name);
            if (lv) {
                lv->is_associative_array = true;
                lv->assoc_array_data = AssociativeArrayObject(vdecl->var_name, vdecl->width, vdecl->is_signed, vdecl->assoc_key_type, vdecl->assoc_key_width);
            }
        }
        return;
    }

    if (auto for_stmt = dynamic_cast<ForStatementNode*>(stmt)) {
        if (for_stmt->init_stmt) execute_statement(for_stmt->init_stmt.get(), bp);
        while (eval_expr(for_stmt->condition.get(), bp.scope).to_bool()) {
            for (auto& s : for_stmt->body) execute_statement(s.get(), bp);
            if (for_stmt->step_stmt) execute_statement(for_stmt->step_stmt.get(), bp);
        }
        return;
    }

    if (auto while_stmt = dynamic_cast<WhileStatementNode*>(stmt)) {
        while (eval_expr(while_stmt->condition.get(), bp.scope).to_bool()) {
            for (auto& s : while_stmt->body) execute_statement(s.get(), bp);
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

    if (auto if_stmt = dynamic_cast<IfStatementNode*>(stmt)) {
        LogicVal cond = eval_expr(if_stmt->condition.get(), bp.scope);
        if (cond.to_bool()) {
            for (auto& s : if_stmt->then_branch) execute_statement(s.get(), bp);
        } else {
            for (auto& s : if_stmt->else_branch) execute_statement(s.get(), bp);
        }
        return;
    }

    if (auto case_stmt = dynamic_cast<CaseStatementNode*>(stmt)) {
        LogicVal sel_val = eval_expr(case_stmt->selector.get(), bp.scope);
        bool branch_taken = false;
        const CaseItem* default_item = nullptr;

        for (const auto& item : case_stmt->items) {
            if (item.is_default) {
                default_item = &item;
                continue;
            }

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

    if (auto sc = dynamic_cast<SysCastStatementNode*>(stmt)) {
        SysCastCallNode call(sc->dest_expr ? sc->dest_expr->clone() : nullptr,
                             sc->src_expr ? sc->src_expr->clone() : nullptr);
        LogicVal res = expr_eval.eval(&call, bp.scope);
        if (res.aval == 0) {
            throw std::runtime_error("[CAS-CAST-ERR] Runtime error: $cast failed (incompatible class types or invalid handle)");
        }
        return;
    }

    if (auto tcall = dynamic_cast<TaskCallNode*>(stmt)) {
        std::string tname = tcall->task_name;
        auto dot = tname.rfind('.');
        if (dot != std::string::npos) {
            std::string var_name = tname.substr(0, dot);
            std::string method = tname.substr(dot + 1);

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
                    for (auto& s : task_node->body) {
                        if (expr_eval.get_context().current_frame() && expr_eval.get_context().current_frame()->returned) break;
                        execute_statement(s.get(), bp);
                    }
                    expr_eval.get_context().pop_frame();
                    expr_eval.set_current_this(prev_this);
                    return;
                }
                if (func_node) {
                    std::vector<LogicVal> arg_vals;
                    for (auto& a : tcall->args) {
                        arg_vals.push_back(expr_eval.eval(a.get(), bp.scope));
                    }
                    LogicVal dummy;
                    expr_eval.eval_class_method(cl_obj, func_node, arg_vals, dummy);
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

        TaskNode* target_task = nullptr;
        if (engine.task_map.find(scoped_tname) != engine.task_map.end()) {
            target_task = engine.task_map.at(scoped_tname);
        } else if (engine.task_map.find(tcall->task_name) != engine.task_map.end()) {
            target_task = engine.task_map.at(tcall->task_name);
        }
        if (target_task) {
            for (auto& s : target_task->body) {
                execute_statement(s.get(), bp);
                if (!engine.scheduler.is_active()) return;
            }
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
                } else if (auto idx = dynamic_cast<IndexSelectNode*>(expr)) {
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
                    for (auto& a : mc->args) check_mon_expr(a.get());
                } else if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
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
                } else if (auto fc = dynamic_cast<FunctionCallNode*>(expr)) {
                    if (engine.dpi_import_map.find(fc->func_name) != engine.dpi_import_map.end()) {
                        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: DPI-C foreign function '" + fc->func_name + "' used directly in $monitor per IEEE 1800-2023 section 35 & 21.2.3");
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
                    char spec = disp->format_string[++i];
                    if (spec == '%') {
                        std::cout << '%';
                        continue;
                    }
                    bool unpadded = false;
                    if (spec == '0' && i + 1 < fmt_len) {
                        char next_spec = disp->format_string[i + 1];
                        if (next_spec == 't' || next_spec == 'h' || next_spec == 'd' || next_spec == 'b' || next_spec == 's' || next_spec == 'f') {
                            spec = next_spec;
                            unpadded = true;
                            ++i;
                        }
                    }

                    if (arg_idx < disp->arg_exprs.size()) {
                        LogicVal val = eval_expr(disp->arg_exprs[arg_idx++].get(), bp.scope);
                        if (spec == 's') {
                            if (val.is_string) std::cout << val.str_val;
                            else std::cout << val.aval;
                        } else if (val.is_unknown() && (spec == 'd' || spec == 't')) {
                            std::cout << "x";
                        } else {
                            if (spec == 'd' || spec == 't') std::cout << (val.is_real ? (int64_t)val.real_val : val.aval);
                        else if (spec == 'f') std::cout << (val.is_real ? val.real_val : (double)val.aval);
                            else if (spec == 'b') std::cout << val.to_bin_string();
                            else if (spec == 'h') std::cout << val.to_hex_string(unpadded);
                            else std::cout << val.aval;
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

    // IEEE 1800-2023 §10.6.1: Non-blocking Assignment (with optional intra-assignment delay)
    if (auto nassign = dynamic_cast<NonBlockingAssignNode*>(stmt)) {
        std::string target_name = nassign->target;
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
                    size_t target_net_id = sig_it->second.net_id;
                    if (target_net_id != (size_t)-1) {
                        LogicVal rhs_val = eval_expr(nassign->rhs.get(), bp.scope);
                        LogicVal casted = LogicOps::cast_width(rhs_val, engine.netlist[target_net_id].width, engine.netlist[target_net_id].is_signed);
                        if (cb.fired_in_time_slot) {
                            engine.scheduler.queue_nba(target_net_id, casted);
                        } else {
                            cb.pending_drives.push_back({target_net_id, casted});
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
                        uint32_t w = sm->width;
                        uint32_t off = sm->offset;
                        uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
                        uint64_t full_mask = mask << off;
                        uint64_t new_a = (local->value.aval & ~full_mask) | ((rhs_val.aval & mask) << off);
                        uint64_t new_b = (local->value.bval & ~full_mask) | ((rhs_val.bval & mask) << off);
                        local->value = LogicVal(local->width, new_a, new_b);
                        return;
                    }
                }
            }
            local->value = LogicOps::cast_width(rhs_val, local->width, local->is_signed);
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

            LogicVal casted = LogicOps::cast_width(rhs_val, engine.netlist[net_id].width, engine.netlist[net_id].is_signed);
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
            if (engine.memory_map.find(mem_assign->mem_name) != engine.memory_map.end()) {
                scoped_mem = mem_assign->mem_name;
            }
        }
        LogicVal addr_val = eval_expr(mem_assign->addr_expr.get(), bp.scope);
        LogicVal rhs_val = eval_expr(mem_assign->rhs.get(), bp.scope);

        if (!addr_val.is_unknown()) {
            int64_t addr = (int64_t)addr_val.aval;
            if (mem_assign->is_non_blocking) {
                if (mem_assign->intra_delay > 0) {
                    uint64_t wake_time = engine.get_sim_time() + mem_assign->intra_delay;
                    engine.scheduler.queue_delayed_nba_mem(wake_time, scoped_mem, addr, rhs_val);
                } else {
                    engine.scheduler.queue_nba_mem(scoped_mem, addr, rhs_val);
                }
            } else {
                if (engine.memory_map.find(scoped_mem) != engine.memory_map.end()) {
                    auto& mem = engine.memory_map.at(scoped_mem);
                    mem.set_word(addr, rhs_val);
                }
            }
        }
        return;
    }

    // IEEE 1800-2023 §10.6.1: Blocking Assignment
    if (auto assign = dynamic_cast<BlockingAssignNode*>(stmt)) {
        if (assign->intra_delay > 0) {
            return;
        }

        LogicVal rhs_val = eval_expr(assign->rhs.get(), bp.scope);

        if (!assign->lhs_targets.empty()) {
            std::string target_name = assign->lhs_targets[0];
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
                        size_t target_net_id = sig_it->second.net_id;
                        if (target_net_id != (size_t)-1) {
                            LogicVal casted = LogicOps::cast_width(rhs_val, engine.netlist[target_net_id].width, engine.netlist[target_net_id].is_signed);
                            if (cb.fired_in_time_slot) {
                                engine.scheduler.queue_nba(target_net_id, casted);
                            } else {
                                cb.pending_drives.push_back({target_net_id, casted});
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
            std::string base_var = target_name;
            std::string member_name = "";
            auto dot_pos = target_name.rfind('.');
            if (dot_pos != std::string::npos) {
                base_var = target_name.substr(0, dot_pos);
                member_name = target_name.substr(dot_pos + 1);
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

            if (auto new_node = dynamic_cast<ClassNewNode*>(assign->rhs.get())) {
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
                    auto inst = engine.allocate_covergroup_instance(cg_type, bp.scope);
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
                    obj->set_property(member_name, rhs_val);
                    return;
                }
            }

            if (member_name.empty() && !local) {
                if (expr_eval.get_current_this() && expr_eval.get_current_this()->has_property(base_var)) {
                    expr_eval.get_current_this()->set_property(base_var, rhs_val);
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
                            uint32_t w = sm->width;
                            uint32_t off = sm->offset;
                            uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
                            uint64_t full_mask = mask << off;
                            uint64_t new_a = (local->value.aval & ~full_mask) | ((rhs_val.aval & mask) << off);
                            uint64_t new_b = (local->value.bval & ~full_mask) | ((rhs_val.bval & mask) << off);
                            local->value = LogicVal(local->width, new_a, new_b);
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
                    }
                } else if (assign->bit_idx) {
                    LogicVal idx_val = eval_expr(assign->bit_idx.get(), bp.scope);
                    if (!idx_val.is_unknown()) {
                        uint32_t bit = idx_val.aval;
                        uint64_t bit_mask = 1ULL << bit;
                        uint64_t bit_a = (rhs_val.aval & 1) << bit;
                        uint64_t bit_b = (rhs_val.bval & 1) << bit;
                        uint64_t new_a = (local->value.aval & ~bit_mask) | bit_a;
                        uint64_t new_b = (local->value.bval & ~bit_mask) | bit_b;
                        local->value = LogicVal(local->width, new_a, new_b);
                    }
                } else {
                    local->value = LogicOps::cast_width(rhs_val, local->width, local->is_signed);
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
                if (q && assign->bit_idx) {
                    LogicVal idx_val = eval_expr(assign->bit_idx.get(), bp.scope);
                    if (!idx_val.is_unknown()) {
                        int64_t idx = (int64_t)idx_val.aval;
                        q->set_elem(idx, rhs_val);
                    }
                    return;
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
                    if (engine.memory_map.find(target_name) != engine.memory_map.end()) {
                        scoped_mem = target_name;
                    }
                }
                if (engine.memory_map.find(scoped_mem) != engine.memory_map.end() && assign->bit_idx) {
                    LogicVal idx_val = eval_expr(assign->bit_idx.get(), bp.scope);
                    if (!idx_val.is_unknown()) {
                        int64_t addr = (int64_t)idx_val.aval;
                        auto& mem = engine.memory_map.at(scoped_mem);
                        mem.set_word(addr, rhs_val);
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
                                uint32_t w = sm->width;
                                uint32_t off = sm->offset;
                                uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
                                uint64_t full_mask = mask << off;
                                uint64_t new_a = (cur.aval & ~full_mask) | ((rhs_val.aval & mask) << off);
                                uint64_t new_b = (cur.bval & ~full_mask) | ((rhs_val.bval & mask) << off);
                                engine.netlist[net_id].set(LogicVal(cur.width, new_a, new_b));
                                return;
                            }
                        }
                    }
                    if (assign->bit_idx) {
                        LogicVal idx_val = eval_expr(assign->bit_idx.get(), bp.scope);
                        if (!idx_val.is_unknown()) {
                            uint32_t bit = idx_val.aval;
                            auto cur = engine.netlist[net_id].value;
                            uint64_t bit_mask = 1ULL << bit;
                            uint64_t bit_a = (rhs_val.aval & 1) << bit;
                            uint64_t bit_b = (rhs_val.bval & 1) << bit;
                            uint64_t new_a = (cur.aval & ~bit_mask) | bit_a;
                            uint64_t new_b = (cur.bval & ~bit_mask) | bit_b;
                            engine.netlist[net_id].set(LogicVal(cur.width, new_a, new_b));
                        }
                    } else {
                        LogicVal casted = LogicOps::cast_width(rhs_val, engine.netlist[net_id].width, engine.netlist[net_id].is_signed);
                        engine.netlist[net_id].set(casted);
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
