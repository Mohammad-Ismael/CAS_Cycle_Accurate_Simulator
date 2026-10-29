#include "../../include/kernel/expr_evaluator.hpp"
#include "../../include/engine.hpp"
#include "../../include/kernel/logic_ops.hpp"
#include <algorithm>

ExprEvaluator::ExprEvaluator(Engine& eng) : engine(eng) {}

bool ExprEvaluator::eval_function(const std::string& func_name, const std::vector<LogicVal>& arg_vals, LogicVal& result_out) {
    FunctionNode* target_fn = nullptr;
    if (engine.function_map.find(func_name) != engine.function_map.end()) {
        target_fn = engine.function_map.at(func_name);
    }
    if (!target_fn) return false;

    std::string frame_name = func_name + "__call";
    context.push_frame(frame_name);
    if (context.depth() > 100) {
        result_out = LogicVal::make_x(target_fn->return_width);
        context.pop_frame();
        return true;
    }

    // 1. Declare and initialize function return variable
    context.declare_local(target_fn->name, target_fn->return_width, target_fn->return_signed);

    // 2. Declare and initialize input parameters
    for (size_t i = 0; i < target_fn->inputs.size(); ++i) {
        const auto& in_p = target_fn->inputs[i];
        uint32_t w = 1;
        if (in_p.msb_expr && in_p.lsb_expr) {
            auto msb_n = dynamic_cast<NumberNode*>(in_p.msb_expr.get());
            auto lsb_n = dynamic_cast<NumberNode*>(in_p.lsb_expr.get());
            if (msb_n && lsb_n) {
                w = (msb_n->aval >= lsb_n->aval) ? (msb_n->aval - lsb_n->aval + 1) : (lsb_n->aval - msb_n->aval + 1);
            }
        }
        context.declare_local(in_p.name, w, in_p.is_signed);
        if (i < arg_vals.size()) {
            context.set_local_value(in_p.name, arg_vals[i]);
        }
    }

    // 3. Execute body statements directly through Evaluator
    BoundProcess* prev_bp = current_bp;
    BoundProcess bp{target_fn->scope.empty() ? frame_name : target_fn->scope, nullptr, 0, false};
    for (auto& s : target_fn->body) {
        engine.evaluator.execute_statement(s.get(), bp);
    }
    engine.evaluator.set_current_process(prev_bp);
    set_current_process(prev_bp);

    // 4. Retrieve return variable
    LocalVar* ret_var = context.current_frame() ? context.current_frame()->get_var(target_fn->name) : nullptr;
    if (ret_var) {
        result_out = ret_var->value;
    } else {
        result_out = LogicVal::make_x(target_fn->return_width);
    }

    context.pop_frame();
    return true;
}

LogicVal ExprEvaluator::eval(ExpressionNode* expr, const std::string& scope) {
    if (!expr) return LogicVal::make_x(1);

    if (auto str_node = dynamic_cast<StringNode*>(expr)) {
        return LogicVal::make_string(str_node->value);
    }

    if (auto num = dynamic_cast<NumberNode*>(expr)) {
        return LogicVal(num->width, num->aval, num->bval);
    }

    if (auto new_arr = dynamic_cast<NewArrayNode*>(expr)) {
        return eval(new_arr->size_expr.get(), scope);
    }

    if (auto cast_node = dynamic_cast<CastNode*>(expr)) {
        LogicVal val = eval(cast_node->expr.get(), scope);
        std::string scoped_type = scope.empty() ? cast_node->target_type : (scope + "." + cast_node->target_type);
        const EnumType* et = nullptr;
        if (engine.enum_types.find(scoped_type) != engine.enum_types.end()) {
            et = &engine.enum_types.at(scoped_type);
        } else if (engine.enum_types.find(cast_node->target_type) != engine.enum_types.end()) {
            et = &engine.enum_types.at(cast_node->target_type);
        }
        if (et) {
            return LogicOps::cast_width(val, et->width, et->is_signed);
        }
        const PackedStructType* st = nullptr;
        if (engine.struct_types.find(scoped_type) != engine.struct_types.end()) {
            st = &engine.struct_types.at(scoped_type);
        } else if (engine.struct_types.find(cast_node->target_type) != engine.struct_types.end()) {
            st = &engine.struct_types.at(cast_node->target_type);
        }
        if (st) {
            return LogicOps::cast_width(val, st->total_width, st->is_signed);
        }
        return val;
    }

    if (auto method_node = dynamic_cast<MethodCallNode*>(expr)) {
        std::string var_name = method_node->var_name;
        QueueObject* q = nullptr;
        if (context.has_active_frame()) {
            LocalVar* local = context.lookup_local(var_name);
            if (local && local->is_queue) q = &local->queue_data;
        }
        if (!q && current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                LocalVar* local = it->local_frame.get_var(var_name);
                if (local && local->is_queue) {
                    q = &local->queue_data;
                    break;
                }
            }
        }
        if (!q) {
            q = engine.get_queue(var_name, scope);
        }

        if (q) {
            std::string method = method_node->method_name;
            if (method == "size") {
                return LogicVal::make_known(32, q->size());
            } else if (method == "pop_front") {
                return q->pop_front();
            } else if (method == "pop_back") {
                return q->pop_back();
            } else if (method == "delete") {
                if (method_node->args.empty()) {
                    q->delete_all();
                } else {
                    LogicVal idx_v = eval(method_node->args[0].get(), scope);
                    q->delete_at((int64_t)idx_v.aval);
                }
                return LogicVal::make_known(32, 0);
            } else if (method == "push_back") {
                if (!method_node->args.empty()) {
                    LogicVal v = eval(method_node->args[0].get(), scope);
                    q->push_back(v);
                }
                return LogicVal::make_known(32, q->size());
            } else if (method == "push_front") {
                if (!method_node->args.empty()) {
                    LogicVal v = eval(method_node->args[0].get(), scope);
                    q->push_front(v);
                }
                return LogicVal::make_known(32, q->size());
            }
        }

        DynamicArrayObject* da = nullptr;
        if (context.has_active_frame()) {
            LocalVar* local = context.lookup_local(var_name);
            if (local && local->is_dynamic_array) da = &local->dyn_array_data;
        }
        if (!da && current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                LocalVar* local = it->local_frame.get_var(var_name);
                if (local && local->is_dynamic_array) {
                    da = &local->dyn_array_data;
                    break;
                }
            }
        }
        if (!da) {
            da = engine.get_dynamic_array(var_name, scope);
        }

        if (da) {
            std::string method = method_node->method_name;
            if (method == "size") {
                return LogicVal::make_known(32, da->size());
            } else if (method == "delete") {
                da->delete_all();
                return LogicVal::make_known(32, 0);
            }
        }

        AssociativeArrayObject* assoc = nullptr;
        if (context.has_active_frame()) {
            LocalVar* local = context.lookup_local(var_name);
            if (local && local->is_associative_array) assoc = &local->assoc_array_data;
        }
        if (!assoc && current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                LocalVar* local = it->local_frame.get_var(var_name);
                if (local && local->is_associative_array) {
                    assoc = &local->assoc_array_data;
                    break;
                }
            }
        }
        if (!assoc) {
            assoc = engine.get_assoc_array(var_name, scope);
        }

        if (assoc) {
            std::string method = method_node->method_name;
            if (method == "size" || method == "num") {
                return LogicVal::make_known(32, assoc->size());
            } else if (method == "exists") {
                if (!method_node->args.empty()) {
                    LogicVal k = eval(method_node->args[0].get(), scope);
                    if (assoc->is_string_key) {
                        return LogicVal::make_known(1, assoc->exists(k.str_val) ? 1 : 0);
                    } else {
                        uint64_t key = k.aval;
                        if (assoc->key_width < 64 && assoc->key_width > 0) {
                            key &= ((1ULL << assoc->key_width) - 1);
                        }
                        return LogicVal::make_known(1, assoc->exists(key) ? 1 : 0);
                    }
                }
                return LogicVal::make_known(1, 0);
            } else if (method == "first" || method == "last") {
                bool is_first = (method == "first");
                if (assoc->is_string_key) {
                    std::string out_k;
                    bool ok = is_first ? assoc->first(out_k) : assoc->last(out_k);
                    if (ok && !method_node->args.empty()) {
                        if (auto id = dynamic_cast<IdentifierNode*>(method_node->args[0].get())) {
                            LogicVal sv;
                            sv.is_string = true;
                            sv.str_val = out_k;
                            if (context.has_active_frame()) {
                                LocalVar* lv = context.lookup_local(id->name);
                                if (lv) lv->value = sv;
                            } else if (current_bp && !current_bp->call_stack.empty()) {
                                for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                                    LocalVar* lv = it->local_frame.get_var(id->name);
                                    if (lv) { lv->value = sv; break; }
                                }
                            }
                        }
                    }
                    return LogicVal::make_known(1, ok ? 1 : 0);
                } else {
                    uint64_t out_k = 0;
                    bool ok = is_first ? assoc->first(out_k) : assoc->last(out_k);
                    if (ok && !method_node->args.empty()) {
                        if (auto id = dynamic_cast<IdentifierNode*>(method_node->args[0].get())) {
                            bool written = false;
                            if (context.has_active_frame()) {
                                LocalVar* lv = context.lookup_local(id->name);
                                if (lv) {
                                    lv->value = LogicOps::cast_width(LogicVal(64, out_k, 0), lv->width, lv->is_signed);
                                    written = true;
                                }
                            }
                            if (!written && current_bp && !current_bp->call_stack.empty()) {
                                for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                                    LocalVar* lv = it->local_frame.get_var(id->name);
                                    if (lv) {
                                        lv->value = LogicOps::cast_width(LogicVal(64, out_k, 0), lv->width, lv->is_signed);
                                        written = true;
                                        break;
                                    }
                                }
                            }
                            if (!written) {
                                size_t net_id = engine.get_net_id(scope, id->name);
                                if (net_id != (size_t)-1) {
                                    LogicVal casted = LogicOps::cast_width(LogicVal(64, out_k, 0), engine.netlist[net_id].width, engine.netlist[net_id].is_signed);
                                    engine.netlist[net_id].set(casted);
                                }
                            }
                        }
                    }
                    return LogicVal::make_known(1, ok ? 1 : 0);
                }
            } else if (method == "next" || method == "prev") {
                bool is_next = (method == "next");
                if (assoc->is_string_key) {
                    std::string in_out_k;
                    if (!method_node->args.empty()) {
                        LogicVal kv = eval(method_node->args[0].get(), scope);
                        in_out_k = kv.str_val;
                    }
                    bool ok = is_next ? assoc->next(in_out_k) : assoc->prev(in_out_k);
                    if (ok && !method_node->args.empty()) {
                        if (auto id = dynamic_cast<IdentifierNode*>(method_node->args[0].get())) {
                            LogicVal sv;
                            sv.is_string = true;
                            sv.str_val = in_out_k;
                            if (context.has_active_frame()) {
                                LocalVar* lv = context.lookup_local(id->name);
                                if (lv) lv->value = sv;
                            } else if (current_bp && !current_bp->call_stack.empty()) {
                                for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                                    LocalVar* lv = it->local_frame.get_var(id->name);
                                    if (lv) { lv->value = sv; break; }
                                }
                            }
                        }
                    }
                    return LogicVal::make_known(1, ok ? 1 : 0);
                } else {
                    uint64_t in_out_k = 0;
                    if (!method_node->args.empty()) {
                        LogicVal kv = eval(method_node->args[0].get(), scope);
                        in_out_k = kv.aval;
                        if (assoc->key_width < 64 && assoc->key_width > 0) {
                            in_out_k &= ((1ULL << assoc->key_width) - 1);
                        }
                    }
                    bool ok = is_next ? assoc->next(in_out_k) : assoc->prev(in_out_k);
                    if (ok && !method_node->args.empty()) {
                        if (auto id = dynamic_cast<IdentifierNode*>(method_node->args[0].get())) {
                            bool written = false;
                            if (context.has_active_frame()) {
                                LocalVar* lv = context.lookup_local(id->name);
                                if (lv) {
                                    lv->value = LogicOps::cast_width(LogicVal(64, in_out_k, 0), lv->width, lv->is_signed);
                                    written = true;
                                }
                            }
                            if (!written && current_bp && !current_bp->call_stack.empty()) {
                                for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                                    LocalVar* lv = it->local_frame.get_var(id->name);
                                    if (lv) {
                                        lv->value = LogicOps::cast_width(LogicVal(64, in_out_k, 0), lv->width, lv->is_signed);
                                        written = true;
                                        break;
                                    }
                                }
                            }
                            if (!written) {
                                size_t net_id = engine.get_net_id(scope, id->name);
                                if (net_id != (size_t)-1) {
                                    LogicVal casted = LogicOps::cast_width(LogicVal(64, in_out_k, 0), engine.netlist[net_id].width, engine.netlist[net_id].is_signed);
                                    engine.netlist[net_id].set(casted);
                                }
                            }
                        }
                    }
                    return LogicVal::make_known(1, ok ? 1 : 0);
                }
            } else if (method == "delete") {
                if (method_node->args.empty()) {
                    assoc->delete_all();
                } else {
                    LogicVal k = eval(method_node->args[0].get(), scope);
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
                return LogicVal::make_known(32, 0);
            }
        }
        std::string scoped_var = scope.empty() ? var_name : (scope + "." + var_name);

        std::string type_name = "";
        if (current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                LocalVar* local = it->local_frame.get_var(var_name);
                if (local && !local->type_name.empty()) {
                    type_name = local->type_name;
                    break;
                }
            }
        }
        if (type_name.empty() && context.has_active_frame()) {
            LocalVar* local = context.lookup_local(var_name);
            if (local && !local->type_name.empty()) type_name = local->type_name;
        }
        if (type_name.empty()) {
            if (engine.var_to_enum_type.find(scoped_var) != engine.var_to_enum_type.end()) {
                type_name = engine.var_to_enum_type.at(scoped_var);
            } else if (engine.var_to_enum_type.find(var_name) != engine.var_to_enum_type.end()) {
                type_name = engine.var_to_enum_type.at(var_name);
            } else if (engine.net_map.find(scoped_var) != engine.net_map.end()) {
                type_name = engine.netlist[engine.net_map.at(scoped_var)].type_name;
            } else if (engine.net_map.find(var_name) != engine.net_map.end()) {
                type_name = engine.netlist[engine.net_map.at(var_name)].type_name;
            }
        }

        const EnumType* et = nullptr;
        std::string scoped_type = scope.empty() ? type_name : (scope + "." + type_name);
        if (!type_name.empty()) {
            if (engine.enum_types.find(scoped_type) != engine.enum_types.end()) {
                et = &engine.enum_types.at(scoped_type);
            } else if (engine.enum_types.find(type_name) != engine.enum_types.end()) {
                et = &engine.enum_types.at(type_name);
            }
        }
        if (!et) {
            if (engine.enum_types.find(scoped_var) != engine.enum_types.end()) {
                et = &engine.enum_types.at(scoped_var);
            } else if (engine.enum_types.find(var_name) != engine.enum_types.end()) {
                et = &engine.enum_types.at(var_name);
            }
        }

        if (!et) return LogicVal::make_x(1);

        IdentifierNode id_expr(var_name);
        LogicVal cur_val = eval(&id_expr, scope);

        std::string mname = method_node->method_name;
        if (mname == "name") {
            std::string name_str = et->get_name(cur_val);
            return LogicVal::make_string(name_str);
        } else if (mname == "first") {
            return et->first();
        } else if (mname == "last") {
            return et->last();
        } else if (mname == "next") {
            int64_t n = 1;
            if (!method_node->args.empty() && method_node->args[0]) {
                LogicVal n_val = eval(method_node->args[0].get(), scope);
                n = (int64_t)n_val.aval;
            }
            return et->next(cur_val, n);
        } else if (mname == "prev") {
            int64_t n = 1;
            if (!method_node->args.empty() && method_node->args[0]) {
                LogicVal n_val = eval(method_node->args[0].get(), scope);
                n = (int64_t)n_val.aval;
            }
            return et->prev(cur_val, n);
        }
        return LogicVal::make_x(et->width);
    }

    if (auto member_node = dynamic_cast<MemberAccessNode*>(expr)) {
        std::string var_name = member_node->var_name;
        std::string member_name = member_node->member_name;
        std::string scoped_var = scope.empty() ? var_name : (scope + "." + var_name);

        size_t cb_id = engine.get_clocking_block_id(var_name, scope);
        if (cb_id != (size_t)-1) {
            const auto& cb = engine.clocking_blocks[cb_id];
            auto it = cb.signals.find(member_name);
            if (it != cb.signals.end()) {
                if (it->second.direction == "input" || it->second.direction == "inout") {
                    return it->second.sampled_value;
                }
                if (it->second.net_id < engine.netlist.size()) {
                    return engine.netlist[it->second.net_id].value;
                }
            }
        }

        std::string full_hier = var_name + "." + member_name;
        size_t nid = engine.get_net_id(scope, full_hier);
        if (nid != (size_t)-1) {
            return engine.netlist[nid].value;
        }

        IdentifierNode id_expr(var_name);
        LogicVal var_val = eval(&id_expr, scope);

        std::string type_name = "";
        if (current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                LocalVar* local = it->local_frame.get_var(var_name);
                if (local && !local->type_name.empty()) {
                    type_name = local->type_name;
                    break;
                }
            }
        }
        if (type_name.empty() && context.has_active_frame()) {
            LocalVar* local = context.lookup_local(var_name);
            if (local && !local->type_name.empty()) type_name = local->type_name;
        }
        if (type_name.empty()) {
            if (engine.var_to_struct_type.find(scoped_var) != engine.var_to_struct_type.end()) {
                type_name = engine.var_to_struct_type.at(scoped_var);
            } else if (engine.var_to_struct_type.find(var_name) != engine.var_to_struct_type.end()) {
                type_name = engine.var_to_struct_type.at(var_name);
            } else if (engine.net_map.find(scoped_var) != engine.net_map.end()) {
                type_name = engine.netlist[engine.net_map.at(scoped_var)].type_name;
            } else if (engine.net_map.find(var_name) != engine.net_map.end()) {
                type_name = engine.netlist[engine.net_map.at(var_name)].type_name;
            }
        }

        const PackedStructType* st = nullptr;
        std::string scoped_type = scope.empty() ? type_name : (scope + "." + type_name);
        if (!type_name.empty()) {
            if (engine.struct_types.find(scoped_type) != engine.struct_types.end()) {
                st = &engine.struct_types.at(scoped_type);
            } else if (engine.struct_types.find(type_name) != engine.struct_types.end()) {
                st = &engine.struct_types.at(type_name);
            }
        }
        if (!st) {
            if (engine.struct_types.find(scoped_var) != engine.struct_types.end()) {
                st = &engine.struct_types.at(scoped_var);
            } else if (engine.struct_types.find(var_name) != engine.struct_types.end()) {
                st = &engine.struct_types.at(var_name);
            }
        }

        if (!st) return LogicVal::make_x(1);

        const StructMember* sm = st->get_member(member_name);
        if (!sm) return LogicVal::make_x(1);

        uint32_t w = sm->width;
        uint32_t off = sm->offset;
        uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
        uint64_t val_a = (var_val.aval >> off) & mask;
        uint64_t val_b = (var_val.bval >> off) & mask;
        LogicVal res(w, val_a, val_b);
        return res;
    }

    if (auto fn_call = dynamic_cast<FunctionCallNode*>(expr)) {
        std::vector<LogicVal> evaluated_args;
        for (const auto& a : fn_call->args) {
            evaluated_args.push_back(eval(a.get(), scope));
        }

        std::string scoped_fn = scope.empty() ? fn_call->func_name : (scope + "." + fn_call->func_name);
        LogicVal ret(32, 0, 0);

        if (eval_function(scoped_fn, evaluated_args, ret)) {
            return ret;
        }
        if (eval_function(fn_call->func_name, evaluated_args, ret)) {
            return ret;
        }
        return LogicVal::make_x(32);
    }

    if (auto ident = dynamic_cast<IdentifierNode*>(expr)) {
        if (ident->name == "$time") return LogicVal::make_known(64, engine.get_sim_time());

        // Check local activation frame of function call first
        if (context.has_active_frame()) {
            LocalVar* local = context.lookup_local(ident->name);
            if (local) return local->value;
        }

        // Check task activation frame on current process
        if (current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                LocalVar* local = it->local_frame.get_var(ident->name);
                if (local) return local->value;
            }
        }

        std::string scoped_ident = scope.empty() ? ident->name : (scope + "." + ident->name);
        if (engine.enum_constant_map.find(scoped_ident) != engine.enum_constant_map.end()) {
            return engine.enum_constant_map.at(scoped_ident);
        }
        if (engine.enum_constant_map.find(ident->name) != engine.enum_constant_map.end()) {
            return engine.enum_constant_map.at(ident->name);
        }
        if (engine.param_map.find(scoped_ident) != engine.param_map.end()) {
            return LogicVal::make_known(32, engine.param_map.at(scoped_ident));
        }
        if (engine.net_map.find(scoped_ident) != engine.net_map.end()) {
            return engine.netlist[engine.net_map.at(scoped_ident)].value;
        }
        if (engine.param_map.find(ident->name) != engine.param_map.end()) {
            return LogicVal::make_known(32, engine.param_map.at(ident->name));
        }
        if (engine.net_map.find(ident->name) != engine.net_map.end()) {
            return engine.netlist[engine.net_map.at(ident->name)].value;
        }
        auto dot_pos = ident->name.rfind('.');
        if (dot_pos != std::string::npos) {
            std::string base_var = ident->name.substr(0, dot_pos);
            std::string mem_name = ident->name.substr(dot_pos + 1);
            MemberAccessNode man(base_var, mem_name);
            return eval(&man, scope);
        }
        return LogicVal::make_x(1);
    }

    if (auto rep = dynamic_cast<ReplicateNode*>(expr)) {
        LogicVal count_val = eval(rep->count_expr.get(), scope);
        uint64_t count = count_val.is_unknown() ? 0 : count_val.aval;
        LogicVal t_val = eval(rep->target_expr.get(), scope);

        if (count == 0 || t_val.width == 0) return LogicVal(1, 0, 0);
        uint32_t total_w = t_val.width * (uint32_t)count;
        uint64_t new_a = 0;
        uint64_t new_b = 0;
        for (uint64_t i = 0; i < count; ++i) {
            new_a = (new_a << t_val.width) | t_val.aval;
            new_b = (new_b << t_val.width) | t_val.bval;
        }
        return LogicVal(total_w, new_a, new_b);
    }

    if (auto cat = dynamic_cast<ConcatNode*>(expr)) {
        uint32_t total_w = 0;
        std::vector<LogicVal> vals;
        for (auto& elem : cat->elements) {
            LogicVal v = eval(elem.get(), scope);
            vals.push_back(v);
            total_w += v.width;
        }

        uint64_t new_a = 0;
        uint64_t new_b = 0;
        int shift = (int)total_w;

        for (const auto& v : vals) {
            shift -= v.width;
            uint64_t mask = (v.width >= 64) ? ~0ULL : ((1ULL << v.width) - 1);
            if (shift >= 0) {
                new_a |= ((v.aval & mask) << shift);
                new_b |= ((v.bval & mask) << shift);
            }
        }
        return LogicVal(total_w, new_a, new_b);
    }

    if (auto idx_node = dynamic_cast<IndexSelectNode*>(expr)) {
        QueueObject* q = nullptr;
        if (context.has_active_frame()) {
            LocalVar* lq = context.lookup_local(idx_node->name);
            if (lq && lq->is_queue) q = &lq->queue_data;
        }
        if (!q && current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                LocalVar* lq = it->local_frame.get_var(idx_node->name);
                if (lq && lq->is_queue) {
                    q = &lq->queue_data;
                    break;
                }
            }
        }
        if (!q) {
            q = engine.get_queue(idx_node->name, scope);
        }
        if (q) {
            LogicVal addr_val = eval(idx_node->msb_expr.get(), scope);
            if (addr_val.is_unknown()) {
                return LogicVal::make_x(q->elem_width);
            }
            int64_t idx = (int64_t)addr_val.aval;
            return q->get_elem(idx);
        }

        DynamicArrayObject* da = nullptr;
        if (context.has_active_frame()) {
            LocalVar* lda = context.lookup_local(idx_node->name);
            if (lda && lda->is_dynamic_array) da = &lda->dyn_array_data;
        }
        if (!da && current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                LocalVar* lda = it->local_frame.get_var(idx_node->name);
                if (lda && lda->is_dynamic_array) {
                    da = &lda->dyn_array_data;
                    break;
                }
            }
        }
        if (!da) {
            da = engine.get_dynamic_array(idx_node->name, scope);
        }
        if (da) {
            LogicVal addr_val = eval(idx_node->msb_expr.get(), scope);
            if (addr_val.is_unknown()) {
                return LogicVal::make_x(da->elem_width);
            }
            int64_t idx = (int64_t)addr_val.aval;
            return da->get_elem(idx);
        }

        AssociativeArrayObject* assoc = nullptr;
        if (context.has_active_frame()) {
            LocalVar* lassoc = context.lookup_local(idx_node->name);
            if (lassoc && lassoc->is_associative_array) assoc = &lassoc->assoc_array_data;
        }
        if (!assoc && current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                LocalVar* lassoc = it->local_frame.get_var(idx_node->name);
                if (lassoc && lassoc->is_associative_array) {
                    assoc = &lassoc->assoc_array_data;
                    break;
                }
            }
        }
        if (!assoc) {
            assoc = engine.get_assoc_array(idx_node->name, scope);
        }
        if (assoc) {
            LogicVal addr_val = eval(idx_node->msb_expr.get(), scope);
            if (addr_val.is_unknown()) {
                return LogicVal::make_x(assoc->val_width);
            }
            if (assoc->is_string_key) {
                return assoc->get(addr_val.str_val);
            } else {
                uint64_t key = addr_val.aval;
                if (assoc->key_width < 64 && assoc->key_width > 0) {
                    key &= ((1ULL << assoc->key_width) - 1);
                }
                return assoc->get(key);
            }
        }

        LogicVal target_val = LogicVal::make_x(1);

        LocalVar* local = nullptr;
        if (context.has_active_frame()) local = context.lookup_local(idx_node->name);
        if (!local && current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                local = it->local_frame.get_var(idx_node->name);
                if (local) break;
            }
        }
        if (local) {
            target_val = local->value;
        } else {
            std::string scoped_ident = scope.empty() ? idx_node->name : (scope + "." + idx_node->name);
            std::string scoped_mem = scoped_ident;
            if (engine.memory_map.find(scoped_mem) == engine.memory_map.end()) {
                if (engine.memory_map.find(idx_node->name) != engine.memory_map.end()) {
                    scoped_mem = idx_node->name;
                }
            }
            if (engine.memory_map.find(scoped_mem) != engine.memory_map.end()) {
                const auto& mem = engine.memory_map.at(scoped_mem);
                LogicVal addr_val = eval(idx_node->msb_expr.get(), scope);
                if (addr_val.is_unknown()) {
                    return LogicVal::make_x(mem.word_width);
                }
                int64_t addr = (int64_t)addr_val.aval;
                return mem.get_word(addr);
            }

            if (engine.net_map.find(scoped_ident) == engine.net_map.end()) {
                if (engine.net_map.find(idx_node->name) != engine.net_map.end()) {
                    scoped_ident = idx_node->name;
                } else {
                    return LogicVal::make_x(1);
                }
            }
            target_val = engine.netlist[engine.net_map.at(scoped_ident)].value;
        }

        if (!idx_node->is_part_select) {
            LogicVal idx_val = eval(idx_node->msb_expr.get(), scope);
            if (idx_val.is_unknown() || idx_val.aval >= target_val.width) return LogicVal::make_x(1);
            uint32_t sh = idx_val.aval;
            return LogicVal(1, (target_val.aval >> sh) & 1, (target_val.bval >> sh) & 1);
        } else {
            LogicVal msb_v = eval(idx_node->msb_expr.get(), scope);
            LogicVal lsb_v = eval(idx_node->lsb_expr.get(), scope);
            if (msb_v.is_unknown() || lsb_v.is_unknown() || msb_v.aval < lsb_v.aval) {
                uint32_t w = (msb_v.aval >= lsb_v.aval) ? (msb_v.aval - lsb_v.aval + 1) : 1;
                return LogicVal::make_x(w);
            }
            uint32_t msb = msb_v.aval;
            uint32_t lsb = lsb_v.aval;
            uint32_t slice_w = msb - lsb + 1;
            uint64_t mask = (slice_w >= 64) ? ~0ULL : ((1ULL << slice_w) - 1);
            return LogicVal(slice_w, (target_val.aval >> lsb) & mask, (target_val.bval >> lsb) & mask);
        }
    }

    if (auto un = dynamic_cast<UnaryOpNode*>(expr)) {
        LogicVal val = eval(un->operand.get(), scope);

        // Logical and Bitwise Unary
        if (un->op == "!") {
            if (val.is_unknown()) return LogicVal::make_x(1);
            return LogicVal::make_known(1, (val.aval == 0) ? 1 : 0);
        }
        if (un->op == "~") return LogicOps::bitwise_not(val);
        if (un->op == "+") return val;
        if (un->op == "-") {
            if (val.is_unknown()) return LogicVal::make_x(val.width);
            uint64_t mask = (val.width >= 64) ? ~0ULL : ((1ULL << val.width) - 1);
            uint64_t neg_a = (~val.aval + 1) & mask;
            return LogicVal(val.width, neg_a, 0);
        }

        // Unary Reductions (§11.4.3)
        if (un->op == "&")  return LogicOps::reduce_and(val);
        if (un->op == "~&") return LogicOps::reduce_nand(val);
        if (un->op == "|")  return LogicOps::reduce_or(val);
        if (un->op == "~|") return LogicOps::reduce_nor(val);
        if (un->op == "^")  return LogicOps::reduce_xor(val);
        if (un->op == "~^" || un->op == "^~") return LogicOps::reduce_xnor(val);
    }

    if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
        LogicVal l = eval(bin->lhs.get(), scope);
        LogicVal r = eval(bin->rhs.get(), scope);

        bool l_signed = false;
        if (auto id = dynamic_cast<IdentifierNode*>(bin->lhs.get())) {
            LocalVar* loc = context.lookup_local(id->name);
            if (loc) {
                l_signed = loc->is_signed;
            } else {
                std::string full = scope.empty() ? id->name : (scope + "." + id->name);
                if (engine.net_map.find(full) != engine.net_map.end()) {
                    l_signed = engine.netlist[engine.net_map.at(full)].is_signed;
                }
            }
        }
        bool r_signed = false;
        if (auto id = dynamic_cast<IdentifierNode*>(bin->rhs.get())) {
            LocalVar* loc = context.lookup_local(id->name);
            if (loc) {
                r_signed = loc->is_signed;
            } else {
                std::string full = scope.empty() ? id->name : (scope + "." + id->name);
                if (engine.net_map.find(full) != engine.net_map.end()) {
                    r_signed = engine.netlist[engine.net_map.at(full)].is_signed;
                }
            }
        }

        bool expr_signed = l_signed && r_signed;

        if (bin->op == "+") {
            uint32_t w = std::min(64U, std::max(l.width, r.width) + 1);
            LogicVal ext_l = LogicOps::cast_width(l, w, expr_signed);
            LogicVal ext_r = LogicOps::cast_width(r, w, expr_signed);
            if (ext_l.is_unknown() || ext_r.is_unknown()) return LogicVal::make_x(w);
            return LogicVal::make_known(w, ext_l.aval + ext_r.aval);
        }
        if (bin->op == "-") {
            uint32_t w = std::min(64U, std::max(l.width, r.width) + 1);
            LogicVal ext_l = LogicOps::cast_width(l, w, expr_signed);
            LogicVal ext_r = LogicOps::cast_width(r, w, expr_signed);
            if (ext_l.is_unknown() || ext_r.is_unknown()) return LogicVal::make_x(w);
            return LogicVal::make_known(w, ext_l.aval - ext_r.aval);
        }
        if (bin->op == "*") {
            uint32_t w = std::min(64U, l.width + r.width);
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(w);
            return LogicVal::make_known(w, l.aval * r.aval);
        }
        if (bin->op == "&")   return LogicOps::bitwise_and(l, r);
        if (bin->op == "|")   return LogicOps::bitwise_or(l, r);
        if (bin->op == "^")   return LogicOps::bitwise_xor(l, r);
        if (bin->op == "<<")  return LogicOps::shift_left(l, r);
        if (bin->op == ">>")  return LogicOps::shift_right(l, r);
        if (bin->op == ">>>") return LogicOps::arith_shift_right(l, r, l_signed);
        if (l.is_string || r.is_string) {
            if (bin->op == "==" || bin->op == "===") return LogicVal::make_known(1, (l.str_val == r.str_val) ? 1 : 0);
            if (bin->op == "!=" || bin->op == "!==") return LogicVal::make_known(1, (l.str_val != r.str_val) ? 1 : 0);
        }
        if (bin->op == "==")  return LogicOps::logical_eq(l, r);
        if (bin->op == "!=")  return LogicOps::logical_neq(l, r);
        if (bin->op == "===") return LogicOps::case_eq(l, r);
        if (bin->op == "!==") return LogicOps::case_neq(l, r);
        if (bin->op == "<") {
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
            if (expr_signed) {
                int64_t sl = (int64_t)LogicOps::cast_width(l, 64, true).aval;
                int64_t sr = (int64_t)LogicOps::cast_width(r, 64, true).aval;
                return LogicVal::make_known(1, (sl < sr) ? 1 : 0);
            }
            return LogicVal::make_known(1, (l.aval < r.aval) ? 1 : 0);
        }
        if (bin->op == "<=") {
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
            if (expr_signed) {
                int64_t sl = (int64_t)LogicOps::cast_width(l, 64, true).aval;
                int64_t sr = (int64_t)LogicOps::cast_width(r, 64, true).aval;
                return LogicVal::make_known(1, (sl <= sr) ? 1 : 0);
            }
            return LogicVal::make_known(1, (l.aval <= r.aval) ? 1 : 0);
        }
        if (bin->op == ">") {
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
            if (expr_signed) {
                int64_t sl = (int64_t)LogicOps::cast_width(l, 64, true).aval;
                int64_t sr = (int64_t)LogicOps::cast_width(r, 64, true).aval;
                return LogicVal::make_known(1, (sl > sr) ? 1 : 0);
            }
            return LogicVal::make_known(1, (l.aval > r.aval) ? 1 : 0);
        }
        if (bin->op == ">=") {
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
            if (expr_signed) {
                int64_t sl = (int64_t)LogicOps::cast_width(l, 64, true).aval;
                int64_t sr = (int64_t)LogicOps::cast_width(r, 64, true).aval;
                return LogicVal::make_known(1, (sl >= sr) ? 1 : 0);
            }
            return LogicVal::make_known(1, (l.aval >= r.aval) ? 1 : 0);
        }
        if (bin->op == "&&") {
            if ((!l.is_unknown() && l.aval == 0) || (!r.is_unknown() && r.aval == 0)) return LogicVal::make_known(1, 0);
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
            return LogicVal::make_known(1, (l.aval != 0 && r.aval != 0) ? 1 : 0);
        }
        if (bin->op == "||") {
            if ((!l.is_unknown() && l.aval != 0) || (!r.is_unknown() && r.aval != 0)) return LogicVal::make_known(1, 1);
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(1);
            return LogicVal::make_known(1, (l.aval != 0 || r.aval != 0) ? 1 : 0);
        }
    }

    if (auto tern = dynamic_cast<TernaryOpNode*>(expr)) {
        LogicVal cond = eval(tern->condition.get(), scope);
        LogicVal t_val = eval(tern->true_expr.get(), scope);
        LogicVal f_val = eval(tern->false_expr.get(), scope);
        return LogicOps::ternary(cond, t_val, f_val);
    }

    return LogicVal::make_x(1);
}
