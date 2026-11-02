#include "../../include/kernel/expr_evaluator.hpp"
#include "../../include/engine.hpp"
#include "../../include/kernel/logic_ops.hpp"
#include <algorithm>
#include <cmath>

ExprEvaluator::ExprEvaluator(Engine& eng) : engine(eng) {}

bool ExprEvaluator::eval_function(const std::string& func_name, const std::vector<LogicVal>& arg_vals, LogicVal& result_out, const std::vector<ExpressionNode*>& caller_exprs) {
    if (engine.dpi_import_map.find(func_name) != engine.dpi_import_map.end()) {
        std::string call_scope = current_bp ? current_bp->scope : "";
        result_out = eval_dpi_call(engine.dpi_import_map.at(func_name), arg_vals, call_scope);
        return true;
    }

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
    context.declare_local(target_fn->name, target_fn->return_width, target_fn->return_signed, target_fn->return_type_name);
    context.current_frame()->func_return_name = target_fn->name;

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
        context.declare_local(in_p.name, w, in_p.is_signed, in_p.type_name);
        if (in_p.type_name == "string") {
            LocalVar* lv = context.lookup_local(in_p.name);
            if (lv) lv->value = LogicVal::make_string("");
        }
        if (in_p.is_virtual_interface) {
            LocalVar* lv = context.lookup_local(in_p.name);
            if (lv) {
                lv->is_virtual_interface = true;
                lv->interface_name = in_p.interface_name;
                lv->modport_name = in_p.modport_name;
                if (i < arg_vals.size()) {
                    lv->vif_handle = arg_vals[i].aval;
                }
            }
        }
        if (i < arg_vals.size()) {
            context.set_local_value(in_p.name, arg_vals[i]);
        } else if (in_p.default_value) {
            LogicVal def_v = eval(in_p.default_value.get(), target_fn->scope);
            context.set_local_value(in_p.name, def_v);
        }
    }

    // 3. Execute body statements directly through Evaluator
    BoundProcess* prev_bp = current_bp;
    BoundProcess bp{target_fn->scope.empty() ? frame_name : target_fn->scope, nullptr, 0, false};
    set_current_process(&bp);
    engine.evaluator.set_current_process(&bp);
    for (auto& s : target_fn->get_body()) {
        engine.evaluator.execute_statement(s.get(), bp);
        if (context.current_frame() && context.current_frame()->returned) {
            break;
        }
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

    // 5. Write back output / inout arguments to caller
    if (prev_bp && !caller_exprs.empty()) {
        for (size_t i = 0; i < target_fn->inputs.size(); ++i) {
            const auto& in_p = target_fn->inputs[i];
            if (in_p.direction == "output" || in_p.direction == "inout") {
                if (i < caller_exprs.size() && caller_exprs[i]) {
                    if (auto id = dynamic_cast<IdentifierNode*>(caller_exprs[i])) {
                        LocalVar* out_var = context.current_frame() ? context.current_frame()->get_var(in_p.name) : nullptr;
                        if (out_var) {
                            engine.evaluator.write_variable(id->name, out_var->value, *prev_bp);
                        }
                    }
                }
            }
        }
    }

    context.pop_frame();
    return true;
}


bool ExprEvaluator::eval_class_method(ClassObject* obj, FunctionNode* fn, const std::vector<LogicVal>& arg_vals, LogicVal& result_out) {
    if (!obj || !fn) return false;

    ClassObject* prev_this = current_this;
    current_this = obj;

    std::string frame_name = obj->class_type_name + "::" + fn->name + "__call";
    context.push_frame(frame_name);
    if (context.current_frame()) {
        context.current_frame()->func_return_name = fn->name;
    }

    // 1. Declare and initialize function return variable
    context.declare_local(fn->name, fn->return_width, fn->return_signed);

    // 2. Declare and initialize input parameters
    for (size_t i = 0; i < fn->inputs.size(); ++i) {
        const auto& in_p = fn->inputs[i];
        uint32_t w = 1;
        if (in_p.msb_expr && in_p.lsb_expr) {
            auto msb_n = dynamic_cast<NumberNode*>(in_p.msb_expr.get());
            auto lsb_n = dynamic_cast<NumberNode*>(in_p.lsb_expr.get());
            if (msb_n && lsb_n) {
                w = (msb_n->aval >= lsb_n->aval) ? (uint32_t)(msb_n->aval - lsb_n->aval + 1) : (uint32_t)(lsb_n->aval - msb_n->aval + 1);
            }
        } else {
            w = 32;
        }
        context.declare_local(in_p.name, w, in_p.is_signed);
        if (in_p.is_virtual_interface) {
            LocalVar* lv = context.lookup_local(in_p.name);
            if (lv) {
                lv->is_virtual_interface = true;
                lv->interface_name = in_p.interface_name;
                lv->modport_name = in_p.modport_name;
                if (i < arg_vals.size()) {
                    lv->vif_handle = arg_vals[i].aval;
                }
            }
        }
        if (i < arg_vals.size()) {
            context.set_local_value(in_p.name, arg_vals[i]);
        } else if (in_p.default_value) {
            LogicVal def_val = eval(in_p.default_value.get(), "");
            context.set_local_value(in_p.name, def_val);
        }
    }

    // 3. Execute body statements directly through Evaluator
    BoundProcess* prev_bp = current_bp;
    BoundProcess bp{frame_name, nullptr, 0, false};
    for (auto& s : fn->get_body()) {
        if (context.current_frame() && context.current_frame()->returned) break;
        engine.evaluator.execute_statement(s.get(), bp);
    }
    engine.evaluator.set_current_process(prev_bp);
    set_current_process(prev_bp);

    // 4. Retrieve return variable
    LocalVar* ret_var = context.current_frame() ? context.current_frame()->get_var(fn->name) : nullptr;
    if (ret_var) {
        result_out = LogicOps::cast_width(ret_var->value, fn->return_width, fn->return_signed);
    } else {
        result_out = LogicVal(fn->return_width, 0, 0);
    }

    context.pop_frame();
    current_this = prev_this;
    return true;
}

bool ExprEvaluator::eval_static_class_method(const std::string& class_type, FunctionNode* fn, const std::vector<LogicVal>& arg_vals, LogicVal& result_out) {
    if (!fn) return false;

    const ClassNode* prev_class_def = current_class_def;
    current_class_def = engine.get_class_def(class_type);

    std::string frame_name = class_type + "::" + fn->name + "__call";
    context.push_frame(frame_name);
    if (context.current_frame()) {
        context.current_frame()->func_return_name = fn->name;
    }

    // 1. Declare and initialize function return variable
    context.declare_local(fn->name, fn->return_width, fn->return_signed);

    // 2. Declare and initialize input parameters
    for (size_t i = 0; i < fn->inputs.size(); ++i) {
        const auto& in_p = fn->inputs[i];
        uint32_t w = 1;
        if (in_p.msb_expr && in_p.lsb_expr) {
            auto msb_n = dynamic_cast<NumberNode*>(in_p.msb_expr.get());
            auto lsb_n = dynamic_cast<NumberNode*>(in_p.lsb_expr.get());
            if (msb_n && lsb_n) {
                w = (msb_n->aval >= lsb_n->aval) ? (uint32_t)(msb_n->aval - lsb_n->aval + 1) : (uint32_t)(lsb_n->aval - msb_n->aval + 1);
            }
        } else {
            w = 32;
        }
        context.declare_local(in_p.name, w, in_p.is_signed);
        if (in_p.is_virtual_interface) {
            LocalVar* lv = context.lookup_local(in_p.name);
            if (lv) {
                lv->is_virtual_interface = true;
                lv->interface_name = in_p.interface_name;
                lv->modport_name = in_p.modport_name;
                if (i < arg_vals.size()) {
                    lv->vif_handle = arg_vals[i].aval;
                }
            }
        }
        if (i < arg_vals.size()) {
            context.set_local_value(in_p.name, arg_vals[i]);
        } else if (in_p.default_value) {
            LogicVal def_val = eval(in_p.default_value.get(), "");
            context.set_local_value(in_p.name, def_val);
        }
    }

    // 3. Execute body statements directly through Evaluator
    BoundProcess* prev_bp = current_bp;
    BoundProcess bp{frame_name, nullptr, 0, false};
    for (auto& s : fn->get_body()) {
        if (context.current_frame() && context.current_frame()->returned) break;
        engine.evaluator.execute_statement(s.get(), bp);
    }
    engine.evaluator.set_current_process(prev_bp);
    set_current_process(prev_bp);

    // 4. Retrieve return variable
    LocalVar* ret_var = context.current_frame() ? context.current_frame()->get_var(fn->name) : nullptr;
    if (ret_var) {
        result_out = LogicOps::cast_width(ret_var->value, fn->return_width, fn->return_signed);
    } else {
        result_out = LogicVal(fn->return_width, 0, 0);
    }

    context.pop_frame();
    current_class_def = prev_class_def;
    return true;
}

LogicVal ExprEvaluator::eval(ExpressionNode* expr, const std::string& scope) {
    if (!expr) return LogicVal::make_x(1);

    if (dynamic_cast<NullNode*>(expr)) {
        return LogicVal(64, 0, 0);
    }

    if (dynamic_cast<ThisNode*>(expr)) {
        if (current_this) return LogicVal(64, current_this->handle_id, 0);
        return LogicVal(64, 0, 0);
    }

    if (auto sfn = dynamic_cast<SysSampledFuncNode*>(expr)) {
        return eval_sampled_func(sfn, scope);
    }

    if (auto aqn = dynamic_cast<ArrayQueryNode*>(expr)) {
        std::string var_name = "";
        if (auto id = dynamic_cast<IdentifierNode*>(aqn->array_expr.get())) {
            var_name = id->name;
        } else if (auto ma = dynamic_cast<MemberAccessNode*>(aqn->array_expr.get())) {
            var_name = ma->var_name + "." + ma->member_name;
        }

        VariableDimMetadata meta;
        bool found = false;

        // Check local variables in execution context
        if (!var_name.empty() && context.has_active_frame()) {
            LocalVar* loc = context.lookup_local(var_name);
            if (loc) {
                if (loc->is_dynamic_array) {
                    meta.is_dynamic = true;
                    meta.is_vector_or_array = true;
                    meta.unpacked_dims = 1;
                    int64_t n = (int64_t)loc->dyn_array_data.size();
                    int64_t r = (n > 0) ? (n - 1) : -1;
                    meta.dimensions.push_back({0, r, true, true, false});
                    if (loc->dyn_array_data.elem_width > 1) {
                        meta.dimensions.push_back({(int64_t)loc->dyn_array_data.elem_width - 1, 0, false, false, false});
                    }
                    found = true;
                } else if (loc->is_queue) {
                    meta.is_queue = true;
                    meta.is_vector_or_array = true;
                    meta.unpacked_dims = 1;
                    int64_t n = (int64_t)loc->queue_data.size();
                    int64_t r = (n > 0) ? (n - 1) : -1;
                    meta.dimensions.push_back({0, r, true, false, true});
                    if (loc->queue_data.elem_width > 1) {
                        meta.dimensions.push_back({(int64_t)loc->queue_data.elem_width - 1, 0, false, false, false});
                    }
                    found = true;
                } else if (!loc->dimensions.empty()) {
                    meta.dimensions = loc->dimensions;
                    meta.unpacked_dims = loc->unpacked_dims;
                    meta.is_vector_or_array = true;
                    found = true;
                } else if (loc->width > 1) {
                    meta.dimensions.push_back({(int64_t)loc->width - 1, 0, false, false, false});
                    meta.unpacked_dims = 0;
                    meta.is_vector_or_array = true;
                    found = true;
                }
            }
        }

        // Check call stack
        if (!found && !var_name.empty() && current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                LocalVar* loc = it->local_frame.get_var(var_name);
                if (loc) {
                    if (loc->is_dynamic_array) {
                        meta.is_dynamic = true;
                        meta.is_vector_or_array = true;
                        meta.unpacked_dims = 1;
                        int64_t n = (int64_t)loc->dyn_array_data.size();
                        int64_t r = (n > 0) ? (n - 1) : -1;
                        meta.dimensions.push_back({0, r, true, true, false});
                        if (loc->dyn_array_data.elem_width > 1) {
                            meta.dimensions.push_back({(int64_t)loc->dyn_array_data.elem_width - 1, 0, false, false, false});
                        }
                        found = true;
                        break;
                    } else if (loc->is_queue) {
                        meta.is_queue = true;
                        meta.is_vector_or_array = true;
                        meta.unpacked_dims = 1;
                        int64_t n = (int64_t)loc->queue_data.size();
                        int64_t r = (n > 0) ? (n - 1) : -1;
                        meta.dimensions.push_back({0, r, true, false, true});
                        if (loc->queue_data.elem_width > 1) {
                            meta.dimensions.push_back({(int64_t)loc->queue_data.elem_width - 1, 0, false, false, false});
                        }
                        found = true;
                        break;
                    } else if (!loc->dimensions.empty()) {
                        meta.dimensions = loc->dimensions;
                        meta.unpacked_dims = loc->unpacked_dims;
                        meta.is_vector_or_array = true;
                        found = true;
                        break;
                    } else if (loc->width > 1) {
                        meta.dimensions.push_back({(int64_t)loc->width - 1, 0, false, false, false});
                        meta.unpacked_dims = 0;
                        meta.is_vector_or_array = true;
                        found = true;
                        break;
                    }
                }
            }
        }

        // Check engine dynamic array
        if (!found && !var_name.empty()) {
            DynamicArrayObject* da = engine.get_dynamic_array(var_name, scope);
            if (da) {
                meta.is_dynamic = true;
                meta.is_vector_or_array = true;
                meta.unpacked_dims = 1;
                int64_t n = (int64_t)da->size();
                int64_t r = (n > 0) ? (n - 1) : -1;
                meta.dimensions.push_back({0, r, true, true, false});
                if (da->elem_width > 1) {
                    meta.dimensions.push_back({(int64_t)da->elem_width - 1, 0, false, false, false});
                }
                found = true;
            }
        }

        // Check engine queue
        if (!found && !var_name.empty()) {
            QueueObject* q = engine.get_queue(var_name, scope);
            if (q) {
                meta.is_queue = true;
                meta.is_vector_or_array = true;
                meta.unpacked_dims = 1;
                int64_t n = (int64_t)q->size();
                int64_t r = (n > 0) ? (n - 1) : -1;
                meta.dimensions.push_back({0, r, true, false, true});
                if (q->elem_width > 1) {
                    meta.dimensions.push_back({(int64_t)q->elem_width - 1, 0, false, false, false});
                }
                found = true;
            }
        }

        // Check engine memory_map (unpacked array)
        if (!found && !var_name.empty()) {
            std::string scoped_var = scope.empty() ? var_name : (scope + "." + var_name);
            std::string mem_key = "";
            if (engine.memory_map.find(scoped_var) != engine.memory_map.end()) mem_key = scoped_var;
            else if (engine.memory_map.find(var_name) != engine.memory_map.end()) mem_key = var_name;
            if (!mem_key.empty()) {
                const auto& mem = engine.memory_map.at(mem_key);
                meta.dimensions = mem.dimensions;
                meta.unpacked_dims = mem.unpacked_dims_count;
                meta.is_vector_or_array = true;
                found = true;
            }
        }

        // Check engine.var_dimensions
        if (!found && !var_name.empty()) {
            std::string scoped_var = scope.empty() ? var_name : (scope + "." + var_name);
            if (engine.var_dimensions.find(scoped_var) != engine.var_dimensions.end()) {
                meta = engine.var_dimensions.at(scoped_var);
                found = true;
            } else if (engine.var_dimensions.find(var_name) != engine.var_dimensions.end()) {
                meta = engine.var_dimensions.at(var_name);
                found = true;
            }
        }

        // Check engine.net_map
        if (!found && !var_name.empty()) {
            std::string scoped_var = scope.empty() ? var_name : (scope + "." + var_name);
            size_t nid = (size_t)-1;
            if (engine.net_map.find(scoped_var) != engine.net_map.end()) nid = engine.net_map.at(scoped_var);
            else if (engine.net_map.find(var_name) != engine.net_map.end()) nid = engine.net_map.at(var_name);
            if (nid != (size_t)-1) {
                const auto& net = engine.netlist[nid];
                if (net.width > 1) {
                    meta.dimensions.push_back({(int64_t)net.width - 1, 0, false, false, false});
                    meta.unpacked_dims = 0;
                    meta.is_vector_or_array = true;
                    found = true;
                }
            }
        }

        // Diagnostic enforcement: non-array / non-vector variables
        if (!found || !meta.is_vector_or_array || meta.dimensions.empty()) {
            throw std::runtime_error("[CAS-DTINPCIL] Non-array or non-vector variable '" + var_name + "' passed to array querying function per IEEE 1800-2023 section 7.11");
        }

        if (aqn->query_type == ArrayQueryType::DIMENSIONS) {
            return LogicVal::make_known(32, meta.dimensions.size(), true);
        }
        if (aqn->query_type == ArrayQueryType::UNPACKED_DIMENSIONS) {
            return LogicVal::make_known(32, meta.unpacked_dims, true);
        }

        int64_t dim = 1;
        if (aqn->dim_expr) {
            LogicVal dval = eval(aqn->dim_expr.get(), scope);
            dim = (int64_t)dval.aval;
        }

        // Diagnostic enforcement: invalid dimension index
        if (dim < 1 || (size_t)dim > meta.dimensions.size()) {
            throw std::runtime_error("[CAS-DTINPCIL] Invalid dimension index " + std::to_string(dim) + " for array querying function per IEEE 1800-2023 section 7.11");
        }

        const auto& d = meta.dimensions[dim - 1];
        switch (aqn->query_type) {
            case ArrayQueryType::SIZE:
                return LogicVal::make_known(32, (uint64_t)d.size(), true);
            case ArrayQueryType::LEFT:
                return LogicVal::make_known(32, (uint64_t)(int64_t)d.left, true);
            case ArrayQueryType::RIGHT:
                return LogicVal::make_known(32, (uint64_t)(int64_t)d.right, true);
            case ArrayQueryType::LOW:
                return LogicVal::make_known(32, (uint64_t)(int64_t)d.low(), true);
            case ArrayQueryType::HIGH:
                return LogicVal::make_known(32, (uint64_t)(int64_t)d.high(), true);
            case ArrayQueryType::INCREMENT: {
                int64_t inc = d.increment();
                uint64_t val = (inc < 0) ? (uint64_t)(uint32_t)-1 : (uint64_t)inc;
                LogicVal res = LogicVal::make_known(32, val, true);
                return res;
            }
            default:
                break;
        }
        return LogicVal::make_x(32);
    }

    if (dynamic_cast<SequenceRepetitionNode*>(expr)) {
        throw std::runtime_error("[CAS-DTINPCIL] SVA sequence repetition operator used outside sequence/property context per IEEE 1800-2023 section 16.9.2");
    }

    if (dynamic_cast<SequenceConcatNode*>(expr)) {
        throw std::runtime_error("[CAS-DTINPCIL] SVA sequence concatenation used outside sequence/property context per IEEE 1800-2023 section 16.9.2");
    }

    if (auto match_node = dynamic_cast<SequenceMatchItemNode*>(expr)) {
        if (!current_sva_local_vars) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: SVA sequence match item used outside sequence/property context per IEEE 1800-2023 section 16.10 & 16.11");
        }
        LogicVal val = eval(match_node->expr.get(), scope);
        if (!val.is_unknown() && val.is_true()) {
            for (const auto& a : match_node->match_assignments) {
                if (!a.is_subroutine) {
                    LogicVal rval = eval(a.expr.get(), scope);
                    auto it = current_sva_local_vars->find(a.var_name);
                    if (it != current_sva_local_vars->end() && it->second.width > 0) {
                        uint32_t target_w = it->second.width;
                        if (rval.width != target_w) {
                            rval = LogicVal(target_w, rval.aval, rval.bval);
                        }
                    }
                    (*current_sva_local_vars)[a.var_name] = rval;
                } else {
                    if (pending_subroutines) {
                        std::vector<LogicVal> evaluated_args;
                        if (auto fn = dynamic_cast<FunctionCallNode*>(a.expr.get())) {
                            for (const auto& arg : fn->args) {
                                evaluated_args.push_back(eval(arg.get(), scope));
                            }
                        }
                        pending_subroutines->push_back({a.var_name, evaluated_args, scope});
                    }
                }
            }
        }
        return val;
    }

    if (auto cast_call = dynamic_cast<SysCastCallNode*>(expr)) {
        std::string dest_name;
        if (auto id = dynamic_cast<IdentifierNode*>(cast_call->dest_expr.get())) {
            dest_name = id->name;
        } else {
            return LogicVal::make_known(32, 0);
        }

        LocalVar* dest_local = nullptr;
        if (context.has_active_frame()) {
            dest_local = context.lookup_local(dest_name);
        }
        if (!dest_local && current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                dest_local = it->local_frame.get_var(dest_name);
                if (dest_local) break;
            }
        }
        std::string dest_class_type;
        size_t dest_nid = (size_t)-1;
        if (dest_local && dest_local->is_class_handle) {
            dest_class_type = dest_local->class_type_name;
        } else {
            dest_nid = engine.get_net_id(scope, dest_name);
            if (dest_nid != (size_t)-1 && engine.netlist[dest_nid].is_class_handle) {
                dest_class_type = engine.get_class_type(dest_name, scope);
            }
        }

        if (dest_class_type.empty()) {
            return LogicVal::make_known(32, 0);
        }

        LogicVal src_val = eval(cast_call->src_expr.get(), scope);
        uint64_t src_handle = src_val.aval;

        if (src_handle == 0) {
            if (dest_local) {
                dest_local->value = LogicVal(64, 0, 0);
            } else if (dest_nid != (size_t)-1) {
                engine.netlist[dest_nid].set(LogicVal(64, 0, 0));
            }
            return LogicVal::make_known(32, 1);
        }

        auto src_obj = engine.get_class_object(src_handle);
        if (!src_obj) {
            return LogicVal::make_known(32, 0);
        }

        if (src_obj->is_instance_of(dest_class_type)) {
            if (dest_local) {
                dest_local->value = LogicVal(64, src_handle, 0);
            } else if (dest_nid != (size_t)-1) {
                engine.netlist[dest_nid].set(LogicVal(64, src_handle, 0));
            }
            return LogicVal::make_known(32, 1);
        } else {
            return LogicVal::make_known(32, 0);
        }
    }

    if (auto cnew = dynamic_cast<ClassNewNode*>(expr)) {
        if (cnew->class_type_name == "semaphore") {
            int keys = 0;
            if (!cnew->args.empty()) {
                LogicVal kval = eval(cnew->args[0].get(), scope);
                keys = kval.is_unknown() ? 0 : (int)kval.aval;
            }
            auto sobj = engine.allocate_semaphore(keys);
            return LogicVal(64, sobj->handle_id, 0);
        }
        if (cnew->class_type_name == "mailbox") {
            int bound = 0;
            if (!cnew->args.empty()) {
                LogicVal bval = eval(cnew->args[0].get(), scope);
                bound = bval.is_unknown() ? 0 : (int)bval.aval;
            }
            auto mobj = engine.allocate_mailbox(bound);
            return LogicVal(64, mobj->handle_id, 0);
        }
        if (!cnew->class_type_name.empty()) {
            auto obj = engine.allocate_class_object(cnew->class_type_name);
            FunctionNode* ctor = obj->class_def ? obj->class_def->get_constructor() : nullptr;
            if (ctor) {
                std::vector<LogicVal> arg_vals;
                for (auto& a : cnew->args) arg_vals.push_back(eval(a.get(), scope));
                LogicVal dummy;
                eval_class_method(obj.get(), ctor, arg_vals, dummy);
            }
            return LogicVal(64, obj->handle_id, 0);
        }
        return LogicVal(64, 0, 0);
    }

    if (auto smc = dynamic_cast<StaticMethodCallNode*>(expr)) {
        std::string full_fn = smc->class_type_name + "::" + smc->method_name;
        if (engine.function_map.find(full_fn) != engine.function_map.end() ||
            engine.function_map.find(smc->method_name) != engine.function_map.end()) {
            std::vector<LogicVal> evaluated_args;
            for (auto& a : smc->args) evaluated_args.push_back(eval(a.get(), scope));
            LogicVal ret(32, 0, 0);
            if (eval_function(full_fn, evaluated_args, ret)) return ret;
            if (eval_function(smc->method_name, evaluated_args, ret)) return ret;
        }

        const ClassNode* cls_def = engine.get_class_def(smc->class_type_name);
        if (cls_def) {
            FunctionNode* fn = cls_def->get_function(smc->method_name);
            if (fn) {
                std::vector<LogicVal> arg_vals;
                for (auto& a : smc->args) arg_vals.push_back(eval(a.get(), scope));
                LogicVal res;
                eval_static_class_method(smc->class_type_name, fn, arg_vals, res);
                return res;
            }
        }
        return LogicVal::make_known(32, 0);
    }

    if (auto sma = dynamic_cast<StaticMemberAccessNode*>(expr)) {
        std::string full_id = sma->class_type_name + "::" + sma->member_name;
        if (engine.param_map.find(full_id) != engine.param_map.end()) {
            return LogicVal::make_known(32, engine.param_map.at(full_id));
        }
        if (engine.enum_constant_map.find(full_id) != engine.enum_constant_map.end()) {
            return engine.enum_constant_map.at(full_id);
        }
        if (engine.net_map.find(full_id) != engine.net_map.end()) {
            return engine.netlist[engine.net_map.at(full_id)].value;
        }
        if (engine.package_defs.find(sma->class_type_name) != engine.package_defs.end()) {
            const auto& pkg = engine.package_defs.at(sma->class_type_name);
            if (pkg->param_constants.count(sma->member_name)) {
                return LogicVal::make_known(32, pkg->param_constants.at(sma->member_name));
            }
            if (pkg->enum_constant_map.count(sma->member_name)) {
                return pkg->enum_constant_map.at(sma->member_name);
            }
        }
        return engine.get_class_static_var(sma->class_type_name, sma->member_name);
    }

    if (auto inside_node = dynamic_cast<InsideNode*>(expr)) {
        LogicVal target_val = eval(inside_node->target.get(), scope);
        for (const auto& r : inside_node->ranges) {
            if (r.high) {
                LogicVal lo = eval(r.low.get(), scope);
                LogicVal hi = eval(r.high.get(), scope);
                if (target_val.aval >= lo.aval && target_val.aval <= hi.aval &&
                    target_val.bval == 0 && lo.bval == 0 && hi.bval == 0) {
                    return LogicVal::make_known(1, 1);
                }
            } else {
                LogicVal v = eval(r.low.get(), scope);
                if (target_val.aval == v.aval && target_val.bval == 0 && v.bval == 0) {
                    return LogicVal::make_known(1, 1);
                }
            }
        }
        return LogicVal::make_known(1, 0);
    }

    if (auto rcall = dynamic_cast<RandomizeCallNode*>(expr)) {
        ClassObject* cl_obj = nullptr;
        if (!rcall->target_handle) {
            cl_obj = current_this;
        } else if (dynamic_cast<ThisNode*>(rcall->target_handle.get())) {
            cl_obj = current_this;
        } else if (auto id = dynamic_cast<IdentifierNode*>(rcall->target_handle.get())) {
            std::string var_name = id->name;
            if (var_name == "this") {
                cl_obj = current_this;
            } else {
                if (context.has_active_frame()) {
                    LocalVar* local = context.lookup_local(var_name);
                    if (local && local->is_class_handle) {
                        cl_obj = engine.get_class_object(local->value.aval).get();
                    }
                }
                if (!cl_obj && current_bp && !current_bp->call_stack.empty()) {
                    for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                        LocalVar* local = it->local_frame.get_var(var_name);
                        if (local && local->is_class_handle) {
                            cl_obj = engine.get_class_object(local->value.aval).get();
                            break;
                        }
                    }
                }
                if (!cl_obj) {
                    size_t nid = engine.get_net_id(scope, var_name);
                    if (nid != (size_t)-1 && engine.netlist[nid].is_class_handle) {
                        cl_obj = engine.get_class_object(engine.netlist[nid].value.aval).get();
                    }
                }
            }
        }
        if (!cl_obj) {
            return LogicVal::make_known(32, 0);
        }

        ClassObject* prev_this = current_this;
        current_this = cl_obj;
        bool ok = engine.solver.solve(cl_obj, rcall->inline_constraints, scope, [&](ExpressionNode* e) {
            return eval(e, scope);
        });
        current_this = prev_this;
        return LogicVal::make_known(32, ok ? 1 : 0);
    }

    if (auto str_node = dynamic_cast<StringNode*>(expr)) {
        return LogicVal::make_string(str_node->value);
    }

    if (auto num = dynamic_cast<NumberNode*>(expr)) {
        if (num->is_real) {
            return LogicVal::make_real(num->real_val);
        }
        return LogicVal(num->width, num->aval, num->bval);
    }

    if (auto new_arr = dynamic_cast<NewArrayNode*>(expr)) {
        return eval(new_arr->size_expr.get(), scope);
    }

    if (auto cast_node = dynamic_cast<CastNode*>(expr)) {
        LogicVal val = eval(cast_node->expr.get(), scope);
        if (cast_node->width > 0) {
            return LogicOps::cast_width(val, cast_node->width, cast_node->is_signed);
        }
        if (cast_node->target_type == "bit" || cast_node->target_type == "logic") {
            return LogicOps::cast_width(val, 1, false);
        }
        if (cast_node->target_type == "int" || cast_node->target_type == "integer") {
            if (val.is_real) {
                return LogicVal::make_known(32, static_cast<uint64_t>(std::round(val.real_val)));
            }
            return LogicOps::cast_width(val, 32, true);
        }
        if (cast_node->target_type == "shortint") {
            if (val.is_real) {
                return LogicVal::make_known(16, static_cast<uint64_t>(std::round(val.real_val)));
            }
            return LogicOps::cast_width(val, 16, true);
        }
        if (cast_node->target_type == "byte") {
            if (val.is_real) {
                return LogicVal::make_known(8, static_cast<uint64_t>(std::round(val.real_val)));
            }
            return LogicOps::cast_width(val, 8, true);
        }
        if (cast_node->target_type == "longint") {
            if (val.is_real) {
                return LogicVal::make_known(64, static_cast<uint64_t>(std::round(val.real_val)));
            }
            return LogicOps::cast_width(val, 64, true);
        }
        if (cast_node->target_type == "real" || cast_node->target_type == "shortreal") {
            if (val.is_real) return val;
            return LogicVal::make_real(static_cast<double>(val.aval));
        }
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
        const SimpleTypeInfo* si = nullptr;
        if (engine.simple_types.find(scoped_type) != engine.simple_types.end()) {
            si = &engine.simple_types.at(scoped_type);
        } else if (engine.simple_types.find(cast_node->target_type) != engine.simple_types.end()) {
            si = &engine.simple_types.at(cast_node->target_type);
        }
        if (si) {
            return LogicOps::cast_width(val, si->width, si->is_signed);
        }
        return val;
    }

    if (auto scast = dynamic_cast<StaticCastExprNode*>(expr)) {
        if (scast->kind == CastKind::SIZE) {
            LogicVal val = eval(scast->expr.get(), scope);
            if (scast->target_width == 0) {
                throw std::runtime_error("[CAS-DTINPCIL] Size cast bit width must be positive integer per IEEE 1800-2023 section 6.24.1");
            }
            return LogicOps::cast_width(val, scast->target_width, val.is_signed);
        }
        if (scast->kind == CastKind::SIGNED) {
            LogicVal val = eval(scast->expr.get(), scope);
            val.is_signed = true;
            return val;
        }
        if (scast->kind == CastKind::UNSIGNED) {
            LogicVal val = eval(scast->expr.get(), scope);
            val.is_signed = false;
            return val;
        }
        if (scast->kind == CastKind::TYPE) {
            LogicVal val = eval(scast->expr.get(), scope);
            std::string target_type = scast->target_type_name;
            if (target_type == "bit" || target_type == "logic" || target_type == "reg") {
                return LogicOps::cast_width(val, 1, false);
            }
            if (target_type == "int" || target_type == "integer") {
                if (val.is_real) {
                    return LogicVal::make_known(32, static_cast<uint64_t>(std::round(val.real_val)), true);
                }
                return LogicOps::cast_width(val, 32, true);
            }
            if (target_type == "shortint") {
                if (val.is_real) {
                    return LogicVal::make_known(16, static_cast<uint64_t>(std::round(val.real_val)), true);
                }
                return LogicOps::cast_width(val, 16, true);
            }
            if (target_type == "byte") {
                if (val.is_real) {
                    return LogicVal::make_known(8, static_cast<uint64_t>(std::round(val.real_val)), true);
                }
                return LogicOps::cast_width(val, 8, true);
            }
            if (target_type == "longint") {
                if (val.is_real) {
                    return LogicVal::make_known(64, static_cast<uint64_t>(std::round(val.real_val)), true);
                }
                return LogicOps::cast_width(val, 64, true);
            }
            if (target_type == "real" || target_type == "shortreal") {
                if (val.is_real) return val;
                return LogicVal::make_real(static_cast<double>(val.aval));
            }
            if (target_type == "signed") {
                val.is_signed = true;
                return val;
            }
            if (target_type == "unsigned") {
                val.is_signed = false;
                return val;
            }
            std::string scoped_type = scope.empty() ? target_type : (scope + "." + target_type);
            const EnumType* et = nullptr;
            if (engine.enum_types.find(scoped_type) != engine.enum_types.end()) {
                et = &engine.enum_types.at(scoped_type);
            } else if (engine.enum_types.find(target_type) != engine.enum_types.end()) {
                et = &engine.enum_types.at(target_type);
            }
            if (et) {
                return LogicOps::cast_width(val, et->width, et->is_signed);
            }

            // Look up Unpacked Array Type Info
            const UnpackedArrayTypeInfo* uarr = nullptr;
            if (engine.unpacked_array_types.find(scoped_type) != engine.unpacked_array_types.end()) {
                uarr = &engine.unpacked_array_types.at(scoped_type);
            } else if (engine.unpacked_array_types.find(target_type) != engine.unpacked_array_types.end()) {
                uarr = &engine.unpacked_array_types.at(target_type);
            }
            if (!uarr) {
                auto colon = target_type.find("::");
                if (colon != std::string::npos) {
                    std::string pkg = target_type.substr(0, colon);
                    std::string typ = target_type.substr(colon + 2);
                    auto pit = engine.package_defs.find(pkg);
                    if (pit != engine.package_defs.end() && pit->second->unpacked_array_typedefs.count(typ)) {
                        const auto& ut = pit->second->unpacked_array_typedefs.at(typ);
                        static thread_local UnpackedArrayTypeInfo tmp_uarr;
                        tmp_uarr = {ut.name, ut.elem_type, ut.elem_width, ut.elem_signed, ut.array_size, ut.start_idx, ut.end_idx};
                        uarr = &tmp_uarr;
                    }
                } else {
                    for (const auto& pkg_pair : engine.package_defs) {
                        if (pkg_pair.second->unpacked_array_typedefs.count(target_type)) {
                            const auto& ut = pkg_pair.second->unpacked_array_typedefs.at(target_type);
                            static thread_local UnpackedArrayTypeInfo tmp_uarr;
                            tmp_uarr = {ut.name, ut.elem_type, ut.elem_width, ut.elem_signed, ut.array_size, ut.start_idx, ut.end_idx};
                            uarr = &tmp_uarr;
                            break;
                        }
                    }
                }
            }

            // Look up Packed Struct Type Info
            const PackedStructType* st = nullptr;
            if (engine.struct_types.find(scoped_type) != engine.struct_types.end()) {
                st = &engine.struct_types.at(scoped_type);
            } else if (engine.struct_types.find(target_type) != engine.struct_types.end()) {
                st = &engine.struct_types.at(target_type);
            }
            if (!st) {
                auto colon = target_type.find("::");
                if (colon != std::string::npos) {
                    std::string pkg = target_type.substr(0, colon);
                    std::string typ = target_type.substr(colon + 2);
                    auto pit = engine.package_defs.find(pkg);
                    if (pit != engine.package_defs.end() && pit->second->struct_typedefs.count(typ)) {
                        const auto& sdef = pit->second->struct_typedefs.at(typ);
                        static thread_local PackedStructType tmp_st;
                        tmp_st.name = typ;
                        tmp_st.total_width = sdef->total_width;
                        tmp_st.is_signed = sdef->is_signed;
                        st = &tmp_st;
                    }
                } else {
                    for (const auto& pkg_pair : engine.package_defs) {
                        if (pkg_pair.second->struct_typedefs.count(target_type)) {
                            const auto& sdef = pkg_pair.second->struct_typedefs.at(target_type);
                            static thread_local PackedStructType tmp_st;
                            tmp_st.name = target_type;
                            tmp_st.total_width = sdef->total_width;
                            tmp_st.is_signed = sdef->is_signed;
                            st = &tmp_st;
                            break;
                        }
                    }
                }
            }

            // Look up Simple Type Info
            const SimpleTypeInfo* si = nullptr;
            if (engine.simple_types.find(scoped_type) != engine.simple_types.end()) {
                si = &engine.simple_types.at(scoped_type);
            } else if (engine.simple_types.find(target_type) != engine.simple_types.end()) {
                si = &engine.simple_types.at(target_type);
            }
            if (!si) {
                auto colon = target_type.find("::");
                if (colon != std::string::npos) {
                    std::string pkg = target_type.substr(0, colon);
                    std::string typ = target_type.substr(colon + 2);
                    auto pit = engine.package_defs.find(pkg);
                    if (pit != engine.package_defs.end() && pit->second->simple_typedefs.count(typ)) {
                        const auto& styp = pit->second->simple_typedefs.at(typ);
                        static thread_local SimpleTypeInfo tmp_si;
                        tmp_si = {styp.name, styp.base_type, styp.width, styp.is_signed};
                        si = &tmp_si;
                    }
                } else {
                    for (const auto& pkg_pair : engine.package_defs) {
                        if (pkg_pair.second->simple_typedefs.count(target_type)) {
                            const auto& styp = pkg_pair.second->simple_typedefs.at(target_type);
                            static thread_local SimpleTypeInfo tmp_si;
                            tmp_si = {styp.name, styp.base_type, styp.width, styp.is_signed};
                            si = &tmp_si;
                            break;
                        }
                    }
                }
            }

            if (!uarr && !st && !si) {
                throw std::runtime_error("[CAS-DTINPCIL] Undefined or unresolved type identifier in static cast per IEEE 1800-2023 section 6.24.1: '" + target_type + "'");
            }

            // Flatten source to bitstream
            std::vector<std::pair<bool, bool>> stream_bits;
            bool got_stream = get_stream_bits(scast->expr.get(), scope, stream_bits);
            if (!got_stream) {
                if (val.is_unpacked) {
                    for (const auto& elem : val.unpacked_elements) {
                        for (int b = (int)elem.width - 1; b >= 0; --b) {
                            stream_bits.push_back({ (bool)((elem.aval >> b) & 1), (bool)((elem.bval >> b) & 1) });
                        }
                    }
                } else {
                    for (int b = (int)val.width - 1; b >= 0; --b) {
                        stream_bits.push_back({ (bool)((val.aval >> b) & 1), (bool)((val.bval >> b) & 1) });
                    }
                }
            }
            uint32_t src_width = (uint32_t)stream_bits.size();

            // Destination 1: Unpacked Array
            if (uarr) {
                uint32_t target_width = uarr->total_bit_width();
                if (src_width != target_width) {
                    throw std::runtime_error("[CAS-DTINPCIL] Bit-stream cast source size (" + std::to_string(src_width) + " bits) does not match destination type '" + target_type + "' size (" + std::to_string(target_width) + " bits) per IEEE 1800-2023 section 6.24.3");
                }
                uint64_t a_all = 0, b_all = 0;
                for (size_t b = 0; b < stream_bits.size() && b < 64; ++b) {
                    if (stream_bits[b].first) a_all |= (1ULL << (target_width - 1 - b));
                    if (stream_bits[b].second) b_all |= (1ULL << (target_width - 1 - b));
                }
                LogicVal res(target_width, a_all, b_all, uarr->elem_signed);
                res.is_unpacked = true;
                res.unpacked_elements.resize(uarr->array_size);
                for (size_t i = 0; i < uarr->array_size; ++i) {
                    uint64_t ea = 0, eb = 0;
                    for (size_t b = 0; b < uarr->elem_width; ++b) {
                        size_t bit_idx = i * uarr->elem_width + b;
                        if (bit_idx < stream_bits.size()) {
                            if (stream_bits[bit_idx].first) ea |= (1ULL << (uarr->elem_width - 1 - b));
                            if (stream_bits[bit_idx].second) eb |= (1ULL << (uarr->elem_width - 1 - b));
                        }
                    }
                    res.unpacked_elements[i] = LogicVal(uarr->elem_width, ea, eb, uarr->elem_signed);
                }
                return res;
            }

            // Destination 2: Packed Struct
            if (st) {
                uint32_t target_width = st->total_width;
                if (src_width != target_width) {
                    throw std::runtime_error("[CAS-DTINPCIL] Bit-stream cast source size (" + std::to_string(src_width) + " bits) does not match destination struct '" + target_type + "' width (" + std::to_string(target_width) + " bits) per IEEE 1800-2023 section 6.24.3");
                }
                uint64_t a_all = 0, b_all = 0;
                for (size_t b = 0; b < stream_bits.size() && b < 64; ++b) {
                    if (stream_bits[b].first) a_all |= (1ULL << (target_width - 1 - b));
                    if (stream_bits[b].second) b_all |= (1ULL << (target_width - 1 - b));
                }
                return LogicVal(target_width, a_all, b_all, st->is_signed);
            }

            // Destination 3: Simple Type / Vector Type
            if (si) {
                uint32_t target_width = si->width;
                if (src_width != target_width) {
                    throw std::runtime_error("[CAS-DTINPCIL] Bit-stream cast source size (" + std::to_string(src_width) + " bits) does not match destination type '" + target_type + "' width (" + std::to_string(target_width) + " bits) per IEEE 1800-2023 section 6.24.3");
                }
                uint64_t a_all = 0, b_all = 0;
                for (size_t b = 0; b < stream_bits.size() && b < 64; ++b) {
                    if (stream_bits[b].first) a_all |= (1ULL << (target_width - 1 - b));
                    if (stream_bits[b].second) b_all |= (1ULL << (target_width - 1 - b));
                }
                return LogicVal(target_width, a_all, b_all, si->is_signed);
            }

            return val;
        }
    }

    if (auto method_node = dynamic_cast<MethodCallNode*>(expr)) {
        std::string var_name = method_node->var_name;

        if (engine.is_semaphore(var_name, scope)) {
            uint64_t handle_id = engine.get_semaphore_handle_id(var_name, scope, current_bp, &context);
            auto sobj = engine.get_semaphore(handle_id);
            if (sobj) {
                if (method_node->method_name == "try_get") {
                    int count = 1;
                    if (!method_node->args.empty()) {
                        LogicVal kval = eval(method_node->args[0].get(), scope);
                        count = kval.is_unknown() ? 0 : (int)kval.aval;
                    }
                    if (sobj->keys >= count) {
                        sobj->keys -= count;
                        return LogicVal::make_known(32, 1);
                    } else {
                        return LogicVal::make_known(32, 0);
                    }
                }
            }
            return LogicVal::make_known(32, 0);
        }

        if (engine.is_mailbox(var_name, scope)) {
            uint64_t handle_id = engine.get_mailbox_handle_id(var_name, scope, current_bp, &context);
            auto mobj = engine.get_mailbox(handle_id);
            if (mobj) {
                if (method_node->method_name == "num") {
                    return LogicVal::make_known(32, (uint64_t)mobj->queue.size());
                }
                if (method_node->method_name == "try_put") {
                    if (!method_node->args.empty()) {
                        LogicVal msg = eval(method_node->args[0].get(), scope);
                        if (mobj->bound > 0 && mobj->queue.size() >= (size_t)mobj->bound) {
                            return LogicVal::make_known(32, 0);
                        }
                        if (!mobj->waiting_get_threads.empty()) {
                            auto front = mobj->waiting_get_threads.front();
                            mobj->waiting_get_threads.pop_front();
                            size_t wpid = front.pid;
                            if (wpid < engine.processes.size()) {
                                auto& wake_bp = engine.processes[wpid];
                                engine.evaluator.write_variable(front.dest_var_name, msg, wake_bp);
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
                        return LogicVal::make_known(32, 1);
                    }
                    return LogicVal::make_known(32, 0);
                }
                if (method_node->method_name == "try_get") {
                    if (mobj->queue.empty()) {
                        return LogicVal::make_known(32, 0);
                    }
                    LogicVal msg = mobj->queue.front();
                    mobj->queue.pop_front();
                    if (!method_node->args.empty()) {
                        if (auto id_node = dynamic_cast<IdentifierNode*>(method_node->args[0].get())) {
                            if (current_bp) {
                                engine.evaluator.write_variable(id_node->name, msg, *current_bp);
                            }
                        }
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
                    return LogicVal::make_known(32, 1);
                }
                if (method_node->method_name == "try_peek") {
                    if (mobj->queue.empty()) {
                        return LogicVal::make_known(32, 0);
                    }
                    LogicVal msg = mobj->queue.front();
                    if (!method_node->args.empty()) {
                        if (auto id_node = dynamic_cast<IdentifierNode*>(method_node->args[0].get())) {
                            if (current_bp) {
                                engine.evaluator.write_variable(id_node->name, msg, *current_bp);
                            }
                        }
                    }
                    return LogicVal::make_known(32, 1);
                }
            }
            return LogicVal::make_known(32, 0);
        }

        ClassObject* cl_obj = nullptr;
        if (var_name == "this" || var_name == "super") {
            cl_obj = current_this;
        } else {
            if (context.has_active_frame()) {
                LocalVar* local = context.lookup_local(var_name);
                if (local && local->is_class_handle) {
                    cl_obj = engine.get_class_object(local->value.aval).get();
                }
            }
            if (!cl_obj && current_bp && !current_bp->call_stack.empty()) {
                for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                    LocalVar* local = it->local_frame.get_var(var_name);
                    if (local && local->is_class_handle) {
                        cl_obj = engine.get_class_object(local->value.aval).get();
                        break;
                    }
                }
            }
            if (!cl_obj) {
                size_t nid = engine.get_net_id(scope, var_name);
                if (nid != (size_t)-1 && engine.netlist[nid].is_class_handle) {
                    cl_obj = engine.get_class_object(engine.netlist[nid].value.aval).get();
                }
            }
            if (!cl_obj && var_name.find('.') != std::string::npos) {
                size_t last_dot = var_name.rfind('.');
                std::string base_path = var_name.substr(0, last_dot);
                std::string prop = var_name.substr(last_dot + 1);
                MemberAccessNode sub_man(base_path, prop);
                LogicVal handle_val = eval(&sub_man, scope);
                if (!handle_val.is_unknown() && handle_val.aval != 0) {
                    cl_obj = engine.get_class_object(handle_val.aval).get();
                }
            }
        }
        if (cl_obj) {
            if (method_node->method_name == "randomize") {
                std::vector<std::unique_ptr<ExpressionNode>> dummy_inline;
                ClassObject* prev_this = current_this;
                current_this = cl_obj;
                bool ok = engine.solver.solve(cl_obj, dummy_inline, scope, [&](ExpressionNode* e) {
                    return eval(e, scope);
                });
                current_this = prev_this;
                return LogicVal::make_known(32, ok ? 1 : 0);
            }
            FunctionNode* fn = nullptr;
            if (var_name == "super") {
                if (cl_obj->class_def && cl_obj->class_def->base_class) {
                    fn = cl_obj->class_def->base_class->find_function(method_node->method_name);
                }
            } else {
                std::string static_type = "";
                if (var_name == "this") static_type = cl_obj->class_type_name;
                else {
                    if (context.has_active_frame()) {
                        LocalVar* local = context.lookup_local(var_name);
                        if (local && local->is_class_handle) static_type = local->class_type_name;
                    }
                    if (static_type.empty() && current_bp && !current_bp->call_stack.empty()) {
                        for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                            LocalVar* local = it->local_frame.get_var(var_name);
                            if (local && local->is_class_handle) { static_type = local->class_type_name; break; }
                        }
                    }
                    if (static_type.empty()) {
                        size_t nid = engine.get_net_id(scope, var_name);
                        if (nid != (size_t)-1 && engine.netlist[nid].is_class_handle) {
                            static_type = engine.get_class_type(var_name, scope);
                        }
                    }
                }
                const ClassNode* static_def = !static_type.empty() ? engine.get_class_def(static_type) : cl_obj->class_def;
                bool is_virt = false;
                if (static_def) is_virt = static_def->is_method_virtual(method_node->method_name);
                else if (cl_obj->class_def) is_virt = cl_obj->class_def->is_method_virtual(method_node->method_name);

                if (is_virt && cl_obj->class_def) {
                    fn = cl_obj->class_def->dispatch_function(method_node->method_name);
                } else if (static_def) {
                    fn = static_def->find_function(method_node->method_name);
                } else if (cl_obj->class_def) {
                    fn = cl_obj->class_def->find_function(method_node->method_name);
                }
            }

            if (fn) {
                std::vector<LogicVal> arg_vals;
                for (auto& a : method_node->args) arg_vals.push_back(eval(a.get(), scope));
                LogicVal res;
                if (fn->is_static) {
                    if (eval_static_class_method(cl_obj->class_type_name, fn, arg_vals, res)) {
                        return res;
                    }
                } else if (eval_class_method(cl_obj, fn, arg_vals, res)) {
                    return res;
                }
            }
        }
        CovergroupInstance* cg_inst = nullptr;
        if (!cl_obj) {
            uint64_t handle_id = 0;
            if (context.has_active_frame()) {
                LocalVar* local = context.lookup_local(var_name);
                if (local && local->is_covergroup_handle) {
                    handle_id = local->value.aval;
                }
            }
            if (handle_id == 0 && current_bp && !current_bp->call_stack.empty()) {
                for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                    LocalVar* local = it->local_frame.get_var(var_name);
                    if (local && local->is_covergroup_handle) {
                        handle_id = local->value.aval;
                        break;
                    }
                }
            }
            if (handle_id == 0) {
                size_t nid = engine.get_net_id(scope, var_name);
                if (nid != (size_t)-1 && engine.netlist[nid].is_covergroup_handle) {
                    handle_id = engine.netlist[nid].value.aval;
                }
            }
            if (handle_id != 0) {
                cg_inst = engine.get_covergroup_instance(handle_id).get();
            }
        }
        if (cg_inst) {
            if (method_node->method_name == "get_coverage" || method_node->method_name == "get_inst_coverage") {
                double cov = cg_inst->get_coverage();
                return LogicVal::make_real(cov);
            }
            if (method_node->method_name == "sample") {
                cg_inst->sample([&](ExpressionNode* e, const std::string& sc) {
                    return eval(e, sc.empty() ? scope : sc);
                });
                return LogicVal::make_known(32, 0);
            }
        }

        std::vector<LogicVal> array_elems;
        uint32_t a_elem_width = 32;
        bool a_is_signed = false;
        if (get_array_elements(var_name, scope, array_elems, a_elem_width, a_is_signed)) {
            std::string method = method_node->method_name;
            std::string iter_name = "item";
            if (!method_node->args.empty()) {
                if (auto id = dynamic_cast<IdentifierNode*>(method_node->args[0].get())) {
                    iter_name = id->name;
                }
            }
            if (method == "sum" || method == "product" || method == "and" || method == "or" || method == "xor") {
                return eval_array_reduction(method, array_elems, a_elem_width, a_is_signed, method_node->with_expr.get(), iter_name, scope);
            }
            if (method == "min" || method == "max") {
                auto loc_res = eval_array_locator(method, array_elems, a_elem_width, a_is_signed, method_node->with_expr.get(), iter_name, scope);
                return loc_res.empty() ? LogicVal::make_x(a_elem_width) : loc_res[0];
            }
            if (method == "find" || method == "find_index" || method == "find_first" || method == "find_last" ||
                method == "find_first_index" || method == "find_last_index") {
                auto loc_res = eval_array_locator(method, array_elems, a_elem_width, a_is_signed, method_node->with_expr.get(), iter_name, scope);
                return LogicVal::make_known(32, (uint64_t)loc_res.size());
            }
        }

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
        if (!da && current_this && current_this->has_dynamic_array(var_name)) {
            da = current_this->get_dynamic_array(var_name);
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

        // IEEE 1800-2023 §6.16: String Built-in Methods
        std::string sm = method_node->method_name;
        if (sm == "len" || sm == "putc" || sm == "getc" || sm == "toupper" || sm == "tolower" ||
            sm == "compare" || sm == "icompare" || sm == "substr" || sm == "atoi" ||
            sm == "atohex" || sm == "atooct" || sm == "atobin" || sm == "atoreal") {

            LocalVar* local_target = nullptr;
            size_t net_target_id = (size_t)-1;
            LogicVal str_val;
            bool found_target = false;
            std::string var_tname = "";

            if (context.has_active_frame()) {
                local_target = context.lookup_local(var_name);
                if (local_target) {
                    str_val = local_target->value;
                    var_tname = local_target->type_name;
                    found_target = true;
                }
            }
            if (!found_target && current_bp && !current_bp->call_stack.empty()) {
                for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                    LocalVar* lv = it->local_frame.get_var(var_name);
                    if (lv) {
                        local_target = lv;
                        str_val = lv->value;
                        var_tname = lv->type_name;
                        found_target = true;
                        break;
                    }
                }
            }
            if (!found_target) {
                size_t nid = engine.get_net_id(scope, var_name);
                if (nid != (size_t)-1) {
                    net_target_id = nid;
                    str_val = engine.netlist[nid].value;
                    var_tname = engine.netlist[nid].type_name;
                    found_target = true;
                }
            }
            if (!found_target && current_this && current_this->has_property(var_name)) {
                str_val = current_this->get_property(var_name);
                found_target = true;
            }

            if (!found_target || (!str_val.is_string && var_tname != "string")) {
                throw std::runtime_error("[CAS-DTINPCIL] Non-string target '" + var_name + "' called string method '" + sm + "()' per IEEE 1800-2023 section 6.16");
            }

            if (sm == "len") {
                return LogicVal::make_known(32, (uint64_t)str_val.str_val.size(), true);
            }
            if (sm == "toupper") {
                std::string s = str_val.str_val;
                for (char& c : s) c = (char)std::toupper((unsigned char)c);
                return LogicVal::make_string(s);
            }
            if (sm == "tolower") {
                std::string s = str_val.str_val;
                for (char& c : s) c = (char)std::tolower((unsigned char)c);
                return LogicVal::make_string(s);
            }
            if (sm == "getc") {
                int idx = 0;
                if (!method_node->args.empty()) {
                    LogicVal iv = eval(method_node->args[0].get(), scope);
                    idx = (int)iv.to_signed();
                }
                if (idx < 0 || idx >= (int)str_val.str_val.size()) {
                    return LogicVal::make_known(32, 0);
                }
                uint8_t byte_val = (uint8_t)str_val.str_val[idx];
                return LogicVal::make_known(32, byte_val);
            }
            if (sm == "putc") {
                if (method_node->args.size() >= 2) {
                    LogicVal iv = eval(method_node->args[0].get(), scope);
                    LogicVal cv = eval(method_node->args[1].get(), scope);
                    int idx = (int)iv.to_signed();
                    if (idx >= 0 && idx < (int)str_val.str_val.size()) {
                        str_val.str_val[idx] = (char)(cv.aval & 0xFF);
                        if (local_target) {
                            local_target->value = str_val;
                        } else if (net_target_id != (size_t)-1) {
                            engine.netlist[net_target_id].set(str_val);
                        } else if (current_this) {
                            current_this->set_property(var_name, str_val);
                        }
                    }
                }
                return LogicVal::make_known(32, 0);
            }
            if (sm == "compare") {
                std::string cmp_s = "";
                if (!method_node->args.empty()) {
                    LogicVal sv = eval(method_node->args[0].get(), scope);
                    cmp_s = sv.str_val;
                }
                int res = 0;
                if (str_val.str_val < cmp_s) res = -1;
                else if (str_val.str_val > cmp_s) res = 1;
                return LogicVal::make_known(32, (uint64_t)(int64_t)res, true);
            }
            if (sm == "icompare") {
                std::string cmp_s = "";
                if (!method_node->args.empty()) {
                    LogicVal sv = eval(method_node->args[0].get(), scope);
                    cmp_s = sv.str_val;
                }
                std::string s1_up = str_val.str_val;
                std::string s2_up = cmp_s;
                for (char& c : s1_up) c = (char)std::toupper((unsigned char)c);
                for (char& c : s2_up) c = (char)std::toupper((unsigned char)c);
                int res = 0;
                if (s1_up < s2_up) res = -1;
                else if (s1_up > s2_up) res = 1;
                return LogicVal::make_known(32, (uint64_t)(int64_t)res, true);
            }
            if (sm == "substr") {
                int i = 0, j = 0;
                if (method_node->args.size() >= 2) {
                    LogicVal iv = eval(method_node->args[0].get(), scope);
                    LogicVal jv = eval(method_node->args[1].get(), scope);
                    i = (int)iv.to_signed();
                    j = (int)jv.to_signed();
                }
                if (i < 0 || j < i || j >= (int)str_val.str_val.size()) {
                    return LogicVal::make_string("");
                }
                return LogicVal::make_string(str_val.str_val.substr(i, j - i + 1));
            }
            if (sm == "atoi") {
                int32_t val = 0;
                try {
                    std::string s = str_val.str_val;
                    size_t k = 0;
                    while (k < s.size() && (s[k] == ' ' || s[k] == '\t' || s[k] == '_')) k++;
                    bool neg = false;
                    if (k < s.size() && (s[k] == '+' || s[k] == '-')) {
                        if (s[k] == '-') neg = true;
                        k++;
                    }
                    int64_t acc = 0;
                    while (k < s.size() && (std::isdigit((unsigned char)s[k]) || s[k] == '_')) {
                        if (s[k] != '_') {
                            acc = acc * 10 + (s[k] - '0');
                        }
                        k++;
                    }
                    if (neg) acc = -acc;
                    val = (int32_t)acc;
                } catch (...) {}
                return LogicVal::make_known(32, (uint64_t)(int64_t)val, true);
            }
            if (sm == "atohex") {
                int32_t val = 0;
                try {
                    std::string s = str_val.str_val;
                    size_t k = 0;
                    while (k < s.size() && (s[k] == ' ' || s[k] == '\t' || s[k] == '_')) k++;
                    if (k + 1 < s.size() && s[k] == '0' && (s[k+1] == 'x' || s[k+1] == 'X')) k += 2;
                    int64_t acc = 0;
                    while (k < s.size()) {
                        char c = s[k++];
                        if (c == '_') continue;
                        if (c >= '0' && c <= '9') acc = (acc << 4) | (c - '0');
                        else if (c >= 'a' && c <= 'f') acc = (acc << 4) | (c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') acc = (acc << 4) | (c - 'A' + 10);
                        else break;
                    }
                    val = (int32_t)acc;
                } catch (...) {}
                return LogicVal::make_known(32, (uint64_t)(int64_t)val, true);
            }
            if (sm == "atooct") {
                int32_t val = 0;
                try {
                    std::string s = str_val.str_val;
                    size_t k = 0;
                    while (k < s.size() && (s[k] == ' ' || s[k] == '\t' || s[k] == '_')) k++;
                    int64_t acc = 0;
                    while (k < s.size()) {
                        char c = s[k++];
                        if (c == '_') continue;
                        if (c >= '0' && c <= '7') acc = (acc << 3) | (c - '0');
                        else break;
                    }
                    val = (int32_t)acc;
                } catch (...) {}
                return LogicVal::make_known(32, (uint64_t)(int64_t)val, true);
            }
            if (sm == "atobin") {
                int32_t val = 0;
                try {
                    std::string s = str_val.str_val;
                    size_t k = 0;
                    while (k < s.size() && (s[k] == ' ' || s[k] == '\t' || s[k] == '_')) k++;
                    int64_t acc = 0;
                    while (k < s.size()) {
                        char c = s[k++];
                        if (c == '_') continue;
                        if (c == '0' || c == '1') acc = (acc << 1) | (c - '0');
                        else break;
                    }
                    val = (int32_t)acc;
                } catch (...) {}
                return LogicVal::make_known(32, (uint64_t)(int64_t)val, true);
            }
            if (sm == "atoreal") {
                double r = 0.0;
                try {
                    r = std::stod(str_val.str_val);
                } catch (...) {}
                return LogicVal::make_real(r);
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

        std::string full_sig = scoped_var + "." + member_name;
        if (engine.net_map.find(full_sig) != engine.net_map.end()) {
            size_t nid = engine.net_map.at(full_sig);
            if (preponed_override && nid < preponed_override->size()) {
                return (*preponed_override)[nid];
            }
            return engine.netlist[nid].value;
        }
        std::string direct_sig = var_name + "." + member_name;
        if (engine.net_map.find(direct_sig) != engine.net_map.end()) {
            size_t nid = engine.net_map.at(direct_sig);
            if (preponed_override && nid < preponed_override->size()) {
                return (*preponed_override)[nid];
            }
            return engine.netlist[nid].value;
        }

        if (engine.interface_instances.find(scoped_var) != engine.interface_instances.end() ||
            engine.interface_instances.find(var_name) != engine.interface_instances.end()) {
            std::string resolved_path = (engine.interface_instances.find(scoped_var) != engine.interface_instances.end()) ? scoped_var : var_name;
            uint64_t hid = const_cast<Engine&>(engine).get_or_create_vif_handle(resolved_path, member_name);
            return LogicVal(64, hid, 0);
        }

        VirtualInterfaceHandle vif = const_cast<Engine&>(engine).lookup_virtual_interface(var_name, scope, current_this, current_bp);
        if (vif.is_valid_vif) {
            size_t target_net_id = const_cast<Engine&>(engine).resolve_vif_net_id(vif, member_name);
            if (target_net_id != (size_t)-1) {
                return engine.netlist[target_net_id].value;
            }
        }

        if (var_name == "this" || var_name == "super") {
            if (current_this) {
                if (current_this->class_def && current_this->class_def->has_static_member(member_name)) {
                    return engine.get_class_static_var(current_this->class_type_name, member_name);
                }
                if (current_this->has_virtual_interface(member_name)) {
                    return LogicVal(64, current_this->get_virtual_interface(member_name).id, 0);
                }
                return current_this->get_property(member_name);
            }
            return LogicVal::make_x(32);
        }

        ClassObject* cl_obj = nullptr;
        if (context.has_active_frame()) {
            LocalVar* local = context.lookup_local(var_name);
            if (local && local->is_class_handle) {
                cl_obj = engine.get_class_object(local->value.aval).get();
            }
        }
        if (!cl_obj && current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                LocalVar* local = it->local_frame.get_var(var_name);
                if (local && local->is_class_handle) {
                    cl_obj = engine.get_class_object(local->value.aval).get();
                    break;
                }
            }
        }
        if (!cl_obj) {
            size_t cl_nid = engine.get_net_id(scope, var_name);
            if (cl_nid != (size_t)-1 && engine.netlist[cl_nid].is_class_handle) {
                cl_obj = engine.get_class_object(engine.netlist[cl_nid].value.aval).get();
            }
        }
        if (!cl_obj && var_name.find('.') != std::string::npos) {
            size_t last_dot = var_name.rfind('.');
            std::string base_path = var_name.substr(0, last_dot);
            std::string prop = var_name.substr(last_dot + 1);
            MemberAccessNode sub_man(base_path, prop);
            LogicVal handle_val = eval(&sub_man, scope);
            if (!handle_val.is_unknown() && handle_val.aval != 0) {
                cl_obj = engine.get_class_object(handle_val.aval).get();
            }
        }
        if (cl_obj) {
            if (cl_obj->class_def && cl_obj->class_def->has_static_member(member_name)) {
                return engine.get_class_static_var(cl_obj->class_type_name, member_name);
            }
            return cl_obj->get_property(member_name);
        }

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
        if (fn_call->func_name == "$sformatf") {
            if (fn_call->args.empty()) {
                return LogicVal::make_string("");
            }
            LogicVal fmt_val = eval(fn_call->args[0].get(), scope);
            std::string fmt_str = fmt_val.is_string ? fmt_val.str_val : "";
            std::stringstream ss;
            size_t arg_idx = 1;
            size_t fmt_len = fmt_str.size();
            for (size_t i = 0; i < fmt_len; ++i) {
                if (fmt_str[i] == '%' && i + 1 < fmt_len) {
                    ++i;
                    if (fmt_str[i] == '%') {
                        ss << '%';
                        continue;
                    }
                    bool pad_zero = false;
                    int width = 0;
                    if (fmt_str[i] == '0') {
                        pad_zero = true;
                        ++i;
                    }
                    while (i < fmt_len && std::isdigit((unsigned char)fmt_str[i])) {
                        width = width * 10 + (fmt_str[i] - '0');
                        ++i;
                    }
                    char spec = (i < fmt_len) ? fmt_str[i] : ' ';

                    if (arg_idx < fn_call->args.size()) {
                        LogicVal val = eval(fn_call->args[arg_idx++].get(), scope);
                        if (spec == 's') {
                            if (val.is_string) ss << val.str_val;
                            else ss << val.aval;
                        } else if (val.is_unknown() && (spec == 'd' || spec == 't')) {
                            ss << "x";
                        } else if (spec == 'd' || spec == 't') {
                            if (val.is_real) {
                                ss << (int64_t)val.real_val;
                            } else if (val.is_signed) {
                                ss << val.to_signed();
                            } else {
                                ss << val.aval;
                            }
                        } else if (spec == 'f') {
                            ss << (val.is_real ? val.real_val : (double)val.aval);
                        } else if (spec == 'b') {
                            ss << val.to_bin_string();
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
                    ss << fmt_str[i];
                }
            }
            return LogicVal::make_string(ss.str());
        }
        if (fn_call->func_name == "$rtoi") {
            if (!fn_call->args.empty()) {
                LogicVal arg_val = eval(fn_call->args[0].get(), scope);
                int64_t ival = 0;
                if (arg_val.is_real) {
                    ival = static_cast<int64_t>(arg_val.real_val);
                } else {
                    ival = static_cast<int64_t>(arg_val.aval);
                }
                return LogicVal::make_known(32, static_cast<uint64_t>(ival));
            }
            return LogicVal::make_known(32, 0);
        }
        std::string scoped_fn = scope.empty() ? fn_call->func_name : (scope + "." + fn_call->func_name);
        DpiImportNode* dpi_node = nullptr;
        if (engine.dpi_import_map.find(scoped_fn) != engine.dpi_import_map.end()) {
            dpi_node = engine.dpi_import_map.at(scoped_fn);
        } else if (engine.dpi_import_map.find(fn_call->func_name) != engine.dpi_import_map.end()) {
            dpi_node = engine.dpi_import_map.at(fn_call->func_name);
        }
        if (dpi_node) {
            return eval_dpi_call(dpi_node, fn_call->args, scope);
        }

        FunctionNode* target_fn = nullptr;
        if (engine.function_map.find(scoped_fn) != engine.function_map.end()) {
            target_fn = engine.function_map.at(scoped_fn);
        } else if (engine.function_map.find(fn_call->func_name) != engine.function_map.end()) {
            target_fn = engine.function_map.at(fn_call->func_name);
        }

        if (!target_fn && current_this && current_this->class_def) {
            FunctionNode* mfn = current_this->class_def->dispatch_function(fn_call->func_name);
            if (mfn) {
                std::vector<LogicVal> arg_vals;
                for (auto& a : fn_call->args) {
                    arg_vals.push_back(eval(a.get(), scope));
                }
                LogicVal res;
                if (eval_class_method(current_this, mfn, arg_vals, res)) {
                    return res;
                }
            }
        }

        if (target_fn) {
            const size_t num_formals = target_fn->inputs.size();
            struct ArgBinding {
                ExpressionNode* actual_expr = nullptr;
                bool is_passed = false;
            };
            std::vector<ArgBinding> bindings(num_formals);

            for (size_t i = 0; i < fn_call->args.size(); ++i) {
                bool is_named = (i < fn_call->arg_names.size() && !fn_call->arg_names[i].empty());
                if (!is_named) {
                    if (i >= num_formals) {
                        throw std::runtime_error("[CAS-DTINPCIL] Too many positional arguments in subroutine call '" + fn_call->func_name + "' per IEEE 1800-2023 section 13.5");
                    }
                    bindings[i].is_passed = true;
                    bindings[i].actual_expr = fn_call->args[i].get();
                } else {
                    const std::string& port_name = fn_call->arg_names[i];
                    int formal_idx = -1;
                    for (size_t k = 0; k < num_formals; ++k) {
                        if (target_fn->inputs[k].name == port_name) {
                            formal_idx = (int)k;
                            break;
                        }
                    }
                    if (formal_idx == -1) {
                        throw std::runtime_error("[CAS-DTINPCIL] Unrecognized formal argument name '" + port_name + "' in function call '" + fn_call->func_name + "' per IEEE 1800-2023 section 13.5.1");
                    }
                    if (bindings[formal_idx].is_passed) {
                        throw std::runtime_error("[CAS-DTINPCIL] Duplicate binding for formal argument '" + port_name + "' in function call '" + fn_call->func_name + "' per IEEE 1800-2023 section 13.5.1");
                    }
                    bindings[formal_idx].is_passed = true;
                    bindings[formal_idx].actual_expr = fn_call->args[i].get();
                }
            }

            std::vector<LogicVal> evaluated_args(num_formals);
            std::vector<ExpressionNode*> caller_nodes(num_formals, nullptr);
            for (size_t j = 0; j < num_formals; ++j) {
                const auto& in_p = target_fn->inputs[j];
                ExpressionNode* val_expr = nullptr;
                if (bindings[j].is_passed) {
                    if (bindings[j].actual_expr != nullptr) {
                        val_expr = bindings[j].actual_expr;
                    } else {
                        // Empty slot
                        if (in_p.default_value) {
                            val_expr = in_p.default_value.get();
                        } else {
                            throw std::runtime_error("[CAS-DTINPCIL] Missing mandatory subroutine argument '" + in_p.name + "' without default value in function call '" + fn_call->func_name + "' per IEEE 1800-2023 section 13.5.3");
                        }
                    }
                } else {
                    // Omitted in call
                    if (in_p.default_value) {
                        val_expr = in_p.default_value.get();
                    } else {
                        throw std::runtime_error("[CAS-DTINPCIL] Missing mandatory subroutine argument '" + in_p.name + "' without default value in function call '" + fn_call->func_name + "' per IEEE 1800-2023 section 13.5.3");
                    }
                }

                caller_nodes[j] = val_expr;
                if (val_expr) {
                    evaluated_args[j] = eval(val_expr, scope);
                } else {
                    evaluated_args[j] = LogicVal::make_x(32);
                }
            }

            LogicVal ret(32, 0, 0);
            if (eval_function(scoped_fn, evaluated_args, ret, caller_nodes)) {
                return ret;
            }
            if (eval_function(fn_call->func_name, evaluated_args, ret, caller_nodes)) {
                return ret;
            }
            return LogicVal::make_x(32);
        }

        std::vector<LogicVal> evaluated_args;
        std::vector<ExpressionNode*> caller_nodes;
        for (const auto& a : fn_call->args) {
            evaluated_args.push_back(a ? eval(a.get(), scope) : LogicVal::make_x(32));
            caller_nodes.push_back(a.get());
        }

        LogicVal ret(32, 0, 0);

        if (eval_function(scoped_fn, evaluated_args, ret, caller_nodes)) {
            return ret;
        }
        if (eval_function(fn_call->func_name, evaluated_args, ret, caller_nodes)) {
            return ret;
        }
        return LogicVal::make_x(32);
    }

    if (auto ident = dynamic_cast<IdentifierNode*>(expr)) {
        if (ident->name == "$time") return LogicVal::make_known(64, engine.get_sim_time());

        // Check SVA thread local variables
        if (current_sva_local_vars) {
            auto it = current_sva_local_vars->find(ident->name);
            if (it != current_sva_local_vars->end()) {
                return it->second;
            }
        }

        // Check local activation frame of function call first
        if (context.has_active_frame()) {
            LocalVar* local = context.lookup_local(ident->name);
            if (local) {
                LogicVal res = local->value;
                res.is_signed = local->is_signed;
                return res;
            }
        }

        // Check task activation frame on current process
        if (current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                LocalVar* local = it->local_frame.get_var(ident->name);
                if (local) {
                    LogicVal res = local->value;
                    res.is_signed = local->is_signed;
                    return res;
                }
            }
        }

        if (current_this) {
            if (current_this->has_virtual_interface(ident->name)) {
                return LogicVal(64, current_this->get_virtual_interface(ident->name).id, 0);
            }
            if (current_this->has_property(ident->name)) {
                return current_this->get_property(ident->name);
            }
        }
        if (current_this && current_this->class_def) {
            if (current_this->class_def->param_values.count(ident->name)) {
                return LogicVal::make_known(32, current_this->class_def->param_values.at(ident->name));
            }
            if (current_this->class_def->has_static_member(ident->name)) {
                return engine.get_class_static_var(current_this->class_type_name, ident->name);
            }
        }
        if (current_class_def) {
            if (current_class_def->param_values.count(ident->name)) {
                return LogicVal::make_known(32, current_class_def->param_values.at(ident->name));
            }
            if (current_class_def->has_static_member(ident->name)) {
                return engine.get_class_static_var(current_class_def->name, ident->name);
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
        if (engine.interface_instances.find(scoped_ident) != engine.interface_instances.end() ||
            engine.interface_instances.find(ident->name) != engine.interface_instances.end()) {
            std::string resolved_path = (engine.interface_instances.find(scoped_ident) != engine.interface_instances.end()) ? scoped_ident : ident->name;
            uint64_t hid = const_cast<Engine&>(engine).get_or_create_vif_handle(resolved_path, "");
            return LogicVal(64, hid, 0);
        }
        size_t nid = engine.get_net_id(scope, ident->name);
        if (nid != (size_t)-1) {
            if (preponed_override && nid < preponed_override->size()) {
                LogicVal res = (*preponed_override)[nid];
                res.is_signed = engine.netlist[nid].is_signed;
                return res;
            }
            LogicVal res = engine.netlist[nid].value;
            res.is_signed = engine.netlist[nid].is_signed;
            return res;
        }
        if (engine.param_map.find(ident->name) != engine.param_map.end()) {
            return LogicVal::make_known(32, engine.param_map.at(ident->name));
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
        bool any_string = false;
        for (auto& elem : cat->elements) {
            LogicVal v = eval(elem.get(), scope);
            if (v.is_string) any_string = true;
            vals.push_back(v);
            total_w += v.width;
        }

        if (any_string) {
            std::string res_str = "";
            for (const auto& v : vals) {
                if (v.is_string) {
                    res_str += v.str_val;
                } else {
                    for (int i = (int)v.width - 8; i >= 0; i -= 8) {
                        char c = (char)((v.aval >> i) & 0xFF);
                        if (c != '\0') res_str.push_back(c);
                    }
                }
            }
            return LogicVal::make_string(res_str);
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

    if (auto sc = dynamic_cast<StreamingConcatNode*>(expr)) {
        return eval_streaming_concat(sc, scope);
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
            LogicVal elem = q->get_elem(idx);
            if (idx_node->has_bit_select && idx_node->bit_msb_expr) {
                uint64_t b_msb = eval(idx_node->bit_msb_expr.get(), scope).aval;
                uint64_t b_lsb = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : b_msb;
                uint32_t w = b_msb >= b_lsb ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
                uint64_t val = (elem.aval >> b_lsb) & mask;
                return LogicVal::make_known(w, val);
            }
            return elem;
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
        if (!da && current_this && current_this->has_dynamic_array(idx_node->name)) {
            da = current_this->get_dynamic_array(idx_node->name);
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
            LogicVal elem = da->get_elem(idx);
            if (idx_node->has_bit_select && idx_node->bit_msb_expr) {
                uint64_t b_msb = eval(idx_node->bit_msb_expr.get(), scope).aval;
                uint64_t b_lsb = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : b_msb;
                uint32_t w = b_msb >= b_lsb ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
                uint64_t val = (elem.aval >> b_lsb) & mask;
                return LogicVal::make_known(w, val);
            }
            return elem;
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
        bool found_sva_local = false;
        if (current_sva_local_vars) {
            auto it = current_sva_local_vars->find(idx_node->name);
            if (it != current_sva_local_vars->end()) {
                target_val = it->second;
                found_sva_local = true;
            }
        }
        if (!found_sva_local && context.has_active_frame()) local = context.lookup_local(idx_node->name);
        if (!found_sva_local && !local && current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                local = it->local_frame.get_var(idx_node->name);
                if (local) break;
            }
        }
        if (found_sva_local) {
            // target_val already set from SVA local variable
        } else if (local) {
            if (local->is_unpacked_array) {
                LogicVal addr_val = eval(idx_node->msb_expr.get(), scope);
                if (addr_val.is_unknown()) {
                    return LogicVal::make_x(local->elem_width);
                }
                int64_t idx = (int64_t)addr_val.aval;
                int64_t rel_idx = idx - local->arr_start_idx;
                if (rel_idx >= 0 && (size_t)rel_idx < local->unpacked_elements.size()) {
                    LogicVal elem = local->unpacked_elements[rel_idx];
                    if (idx_node->has_bit_select && idx_node->bit_msb_expr) {
                        uint64_t b_msb = eval(idx_node->bit_msb_expr.get(), scope).aval;
                        uint64_t b_lsb = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : b_msb;
                        uint32_t w = b_msb >= b_lsb ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                        uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
                        uint64_t val = (elem.aval >> b_lsb) & mask;
                        return LogicVal::make_known(w, val);
                    }
                    return elem;
                }
                return LogicVal::make_x(local->elem_width);
            }
            target_val = local->value;
        } else {
            std::string scoped_ident = scope.empty() ? idx_node->name : (scope + "." + idx_node->name);
            std::string scoped_mem = scoped_ident;
            if (engine.memory_map.find(scoped_mem) == engine.memory_map.end()) {
                std::string cur_s = scope;
                while (!cur_s.empty() && engine.memory_map.find(cur_s + "." + idx_node->name) == engine.memory_map.end()) {
                    auto dot = cur_s.rfind('.');
                    if (dot == std::string::npos) cur_s = "";
                    else cur_s = cur_s.substr(0, dot);
                }
                if (!cur_s.empty()) scoped_mem = cur_s + "." + idx_node->name;
                else if (engine.memory_map.find(idx_node->name) != engine.memory_map.end()) {
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
                LogicVal elem = mem.get_word(addr);
                if (idx_node->has_bit_select && idx_node->bit_msb_expr) {
                    uint64_t b_msb = eval(idx_node->bit_msb_expr.get(), scope).aval;
                    uint64_t b_lsb = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : b_msb;
                    uint32_t w = b_msb >= b_lsb ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                    uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
                    uint64_t val = (elem.aval >> b_lsb) & mask;
                    return LogicVal::make_known(w, val);
                }
                return elem;
            }

            size_t nid = (size_t)-1;
            auto dot_p = idx_node->name.find('.');
            if (dot_p != std::string::npos) {
                std::string base = idx_node->name.substr(0, dot_p);
                std::string mem = idx_node->name.substr(dot_p + 1);
                VirtualInterfaceHandle vif = const_cast<Engine&>(engine).lookup_virtual_interface(base, scope, current_this, current_bp);
                if (vif.id != 0 || !vif.inst_path.empty()) {
                    nid = const_cast<Engine&>(engine).resolve_vif_net_id(vif, mem);
                }
            }
            if (nid == (size_t)-1) {
                nid = engine.get_net_id(scope, idx_node->name);
            }
            if (nid != (size_t)-1) {
                if (preponed_override && nid < preponed_override->size()) {
                    target_val = (*preponed_override)[nid];
                } else {
                    target_val = engine.netlist[nid].value;
                }
            } else {
                return LogicVal::make_x(1);
            }
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
            return LogicVal(val.width, neg_a, 0, true);
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

        bool l_signed = l.is_signed;
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
        bool r_signed = r.is_signed;
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
            if (bin->op == "<")  return LogicVal::make_known(1, (l.str_val < r.str_val) ? 1 : 0);
            if (bin->op == "<=") return LogicVal::make_known(1, (l.str_val <= r.str_val) ? 1 : 0);
            if (bin->op == ">")  return LogicVal::make_known(1, (l.str_val > r.str_val) ? 1 : 0);
            if (bin->op == ">=") return LogicVal::make_known(1, (l.str_val >= r.str_val) ? 1 : 0);
        }
        if (bin->op == "==" || bin->op == "!=") {
            uint32_t w = std::max(l.width, r.width);
            LogicVal el = (l.width != w) ? LogicOps::cast_width(l, w, expr_signed) : l;
            LogicVal er = (r.width != w) ? LogicOps::cast_width(r, w, expr_signed) : r;
            if (bin->op == "==")  return LogicOps::logical_eq(el, er);
            if (bin->op == "!=")  return LogicOps::logical_neq(el, er);
        }
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

LogicVal ExprEvaluator::eval_sampled_func(SysSampledFuncNode* func, const std::string& scope) {
    if (!func || !func->expr) {
        return LogicVal(1, 0, 0);
    }

    if (!current_clock_history || current_clock_history->empty()) {
        return LogicVal(1, 0, 0);
    }

    const auto& history = *current_clock_history;
    size_t hist_len = history.size();

    if (func->func_type == SysSampledFuncType::PAST) {
        uint64_t cycles = 1;
        if (func->num_cycles_expr) {
            LogicVal c_val = eval(func->num_cycles_expr.get(), scope);
            cycles = c_val.aval;
        }
        if (cycles == 0) {
            const auto* saved_override = preponed_override;
            preponed_override = &history.back();
            LogicVal res = eval(func->expr.get(), scope);
            preponed_override = saved_override;
            return res;
        }
        if (hist_len <= cycles) {
            const auto* saved_override = preponed_override;
            preponed_override = &history.back();
            LogicVal cur = eval(func->expr.get(), scope);
            preponed_override = saved_override;
            return LogicVal::make_x(cur.width ? cur.width : 1);
        }
        size_t target_idx = hist_len - 1 - cycles;
        const auto* saved_override = preponed_override;
        preponed_override = &history[target_idx];
        LogicVal res = eval(func->expr.get(), scope);
        preponed_override = saved_override;
        return res;
    }

    const auto* saved_override = preponed_override;
    preponed_override = &history.back();
    LogicVal val_curr = eval(func->expr.get(), scope);

    LogicVal val_prev;
    if (hist_len >= 2) {
        preponed_override = &history[hist_len - 2];
        val_prev = eval(func->expr.get(), scope);
    } else {
        val_prev = LogicVal::make_x(val_curr.width ? val_curr.width : 1);
    }
    preponed_override = saved_override;

    if (func->func_type == SysSampledFuncType::ROSE) {
        bool cur_is_one = ((val_curr.aval & 1ULL) != 0 && (val_curr.bval & 1ULL) == 0);
        bool prev_is_one = ((val_prev.aval & 1ULL) != 0 && (val_prev.bval & 1ULL) == 0);
        bool rose = (!prev_is_one && cur_is_one);
        return LogicVal(1, rose ? 1 : 0, 0);
    }

    if (func->func_type == SysSampledFuncType::FELL) {
        bool cur_is_one = ((val_curr.aval & 1ULL) != 0 && (val_curr.bval & 1ULL) == 0);
        bool prev_is_one = ((val_prev.aval & 1ULL) != 0 && (val_prev.bval & 1ULL) == 0);
        bool fell = (prev_is_one && !cur_is_one);
        return LogicVal(1, fell ? 1 : 0, 0);
    }

    if (func->func_type == SysSampledFuncType::STABLE) {
        bool stable = (hist_len >= 2 && val_curr == val_prev);
        return LogicVal(1, stable ? 1 : 0, 0);
    }

    return LogicVal(1, 0, 0);
}

static LogicVal invoke_c_symbol(void* sym, DpiImportNode* dpi, const std::vector<uint64_t>& c_args) {
    if (dpi->is_task || dpi->return_type == "void") {
        switch (c_args.size()) {
            case 0: reinterpret_cast<void(*)()>(sym)(); break;
            case 1: reinterpret_cast<void(*)(uint64_t)>(sym)(c_args[0]); break;
            case 2: reinterpret_cast<void(*)(uint64_t, uint64_t)>(sym)(c_args[0], c_args[1]); break;
            case 3: reinterpret_cast<void(*)(uint64_t, uint64_t, uint64_t)>(sym)(c_args[0], c_args[1], c_args[2]); break;
            case 4: reinterpret_cast<void(*)(uint64_t, uint64_t, uint64_t, uint64_t)>(sym)(c_args[0], c_args[1], c_args[2], c_args[3]); break;
            case 5: reinterpret_cast<void(*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t)>(sym)(c_args[0], c_args[1], c_args[2], c_args[3], c_args[4]); break;
            case 6: reinterpret_cast<void(*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t)>(sym)(c_args[0], c_args[1], c_args[2], c_args[3], c_args[4], c_args[5]); break;
            default:
                throw std::runtime_error("[CAS-DPI-ERR] Unsupported number of DPI-C arguments: " + std::to_string(c_args.size()));
        }
        return LogicVal(0, 0, 0);
    }

    if (dpi->return_type == "chandle") {
        void* ret_ptr = nullptr;
        switch (c_args.size()) {
            case 0: ret_ptr = reinterpret_cast<void*(*)()>(sym)(); break;
            case 1: ret_ptr = reinterpret_cast<void*(*)(uint64_t)>(sym)(c_args[0]); break;
            case 2: ret_ptr = reinterpret_cast<void*(*)(uint64_t, uint64_t)>(sym)(c_args[0], c_args[1]); break;
            case 3: ret_ptr = reinterpret_cast<void*(*)(uint64_t, uint64_t, uint64_t)>(sym)(c_args[0], c_args[1], c_args[2]); break;
            case 4: ret_ptr = reinterpret_cast<void*(*)(uint64_t, uint64_t, uint64_t, uint64_t)>(sym)(c_args[0], c_args[1], c_args[2], c_args[3]); break;
            case 5: ret_ptr = reinterpret_cast<void*(*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t)>(sym)(c_args[0], c_args[1], c_args[2], c_args[3], c_args[4]); break;
            case 6: ret_ptr = reinterpret_cast<void*(*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t)>(sym)(c_args[0], c_args[1], c_args[2], c_args[3], c_args[4], c_args[5]); break;
            default:
                throw std::runtime_error("[CAS-DPI-ERR] Unsupported number of DPI-C arguments: " + std::to_string(c_args.size()));
        }
        return LogicVal::make_known(64, reinterpret_cast<uintptr_t>(ret_ptr));
    }

    if (dpi->return_width > 32 || dpi->return_type == "longint") {
        uint64_t ret_val = 0;
        switch (c_args.size()) {
            case 0: ret_val = reinterpret_cast<uint64_t(*)()>(sym)(); break;
            case 1: ret_val = reinterpret_cast<uint64_t(*)(uint64_t)>(sym)(c_args[0]); break;
            case 2: ret_val = reinterpret_cast<uint64_t(*)(uint64_t, uint64_t)>(sym)(c_args[0], c_args[1]); break;
            case 3: ret_val = reinterpret_cast<uint64_t(*)(uint64_t, uint64_t, uint64_t)>(sym)(c_args[0], c_args[1], c_args[2]); break;
            case 4: ret_val = reinterpret_cast<uint64_t(*)(uint64_t, uint64_t, uint64_t, uint64_t)>(sym)(c_args[0], c_args[1], c_args[2], c_args[3]); break;
            case 5: ret_val = reinterpret_cast<uint64_t(*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t)>(sym)(c_args[0], c_args[1], c_args[2], c_args[3], c_args[4]); break;
            case 6: ret_val = reinterpret_cast<uint64_t(*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t)>(sym)(c_args[0], c_args[1], c_args[2], c_args[3], c_args[4], c_args[5]); break;
            default:
                throw std::runtime_error("[CAS-DPI-ERR] Unsupported number of DPI-C arguments: " + std::to_string(c_args.size()));
        }
        return LogicVal::make_known(dpi->return_width, ret_val);
    } else {
        int ret_val = 0;
        switch (c_args.size()) {
            case 0: ret_val = reinterpret_cast<int(*)()>(sym)(); break;
            case 1: ret_val = reinterpret_cast<int(*)(uint64_t)>(sym)(c_args[0]); break;
            case 2: ret_val = reinterpret_cast<int(*)(uint64_t, uint64_t)>(sym)(c_args[0], c_args[1]); break;
            case 3: ret_val = reinterpret_cast<int(*)(uint64_t, uint64_t, uint64_t)>(sym)(c_args[0], c_args[1], c_args[2]); break;
            case 4: ret_val = reinterpret_cast<int(*)(uint64_t, uint64_t, uint64_t, uint64_t)>(sym)(c_args[0], c_args[1], c_args[2], c_args[3]); break;
            case 5: ret_val = reinterpret_cast<int(*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t)>(sym)(c_args[0], c_args[1], c_args[2], c_args[3], c_args[4]); break;
            case 6: ret_val = reinterpret_cast<int(*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t)>(sym)(c_args[0], c_args[1], c_args[2], c_args[3], c_args[4], c_args[5]); break;
            default:
                throw std::runtime_error("[CAS-DPI-ERR] Unsupported number of DPI-C arguments: " + std::to_string(c_args.size()));
        }
        return LogicVal::make_known(dpi->return_width, static_cast<uint64_t>(static_cast<uint32_t>(ret_val)));
    }
}

LogicVal ExprEvaluator::eval_dpi_call(DpiImportNode* dpi, const std::vector<std::unique_ptr<ExpressionNode>>& args, const std::string& scope) {
    void* sym = engine.dpi_registry.get_symbol(dpi->c_name, engine.source_search_paths);
    if (!sym) {
        throw std::runtime_error("[CAS-DPI-ERR] Runtime error: DPI-C foreign function symbol '" + dpi->c_name + "' could not be resolved from shared library");
    }

    struct ContextScopeGuard {
        bool is_context;
        svScope prev_scope;
        void* prev_eng;
        ContextScopeGuard(bool ctx, const std::string& call_scope, Engine* eng) : is_context(ctx), prev_scope(nullptr), prev_eng(nullptr) {
            if (is_context) {
                prev_scope = DpiRegistry::set_active_scope_by_name(call_scope);
                prev_eng = DpiRegistry::get_active_engine();
                DpiRegistry::set_active_engine(eng);
            }
        }
        ~ContextScopeGuard() {
            if (is_context) {
                DpiRegistry::set_active_scope(prev_scope);
                DpiRegistry::set_active_engine(prev_eng);
            }
        }
    };

    std::string call_scope = !scope.empty() ? scope : ((current_bp && !current_bp->scope.empty()) ? current_bp->scope : "");
    if (call_scope.empty()) {
        call_scope = engine.active_scope_name;
    }
    ContextScopeGuard scope_guard(dpi->is_context, call_scope, &engine);

    std::vector<uint64_t> c_args;
    struct OpenArrayRecord {
        size_t arg_idx;
        std::unique_ptr<SvOpenArray> oa;
    };
    std::vector<OpenArrayRecord> open_records;

    for (size_t i = 0; i < args.size(); ++i) {
        bool is_open = (i < dpi->tf_ports.size() && dpi->tf_ports[i].is_open_array);
        if (is_open) {
            std::string arr_name = "";
            if (auto id = dynamic_cast<IdentifierNode*>(args[i].get())) {
                arr_name = id->name;
            }
            auto oa = std::make_unique<SvOpenArray>();
            std::string scoped_arr = scope.empty() ? arr_name : (scope + "." + arr_name);

            MemoryArray* target_mem = nullptr;
            if (engine.memory_map.find(scoped_arr) != engine.memory_map.end()) {
                target_mem = &engine.memory_map.at(scoped_arr);
            } else if (!engine.active_scope_name.empty() &&
                       engine.memory_map.find(engine.active_scope_name + "." + arr_name) != engine.memory_map.end()) {
                target_mem = &engine.memory_map.at(engine.active_scope_name + "." + arr_name);
            } else if (engine.memory_map.find(arr_name) != engine.memory_map.end()) {
                target_mem = &engine.memory_map.at(arr_name);
            }

            if (target_mem) {
                oa->size = static_cast<int>(target_mem->depth);
                oa->elem_width = static_cast<int>(target_mem->word_width);
                oa->source_mem = target_mem;
                oa->int32_elements.resize(target_mem->depth);
                oa->int64_elements.resize(target_mem->depth);
                for (size_t j = 0; j < target_mem->depth; ++j) {
                    int64_t sv_idx = target_mem->is_descending ? (target_mem->end_idx - static_cast<int64_t>(j)) : (target_mem->start_idx + static_cast<int64_t>(j));
                    LogicVal w_val = target_mem->get_word(sv_idx);
                    oa->int32_elements[j] = static_cast<int32_t>(w_val.aval);
                    oa->int64_elements[j] = static_cast<int64_t>(w_val.aval);
                }
            } else {
                DynamicArrayObject* target_dyn = nullptr;
                if (context.has_active_frame()) {
                    LocalVar* local = context.lookup_local(arr_name);
                    if (local && local->is_dynamic_array) {
                        target_dyn = &local->dyn_array_data;
                    }
                }
                if (!target_dyn && current_bp && !current_bp->call_stack.empty()) {
                    for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                        LocalVar* local = it->local_frame.get_var(arr_name);
                        if (local && local->is_dynamic_array) {
                            target_dyn = &local->dyn_array_data;
                            break;
                        }
                    }
                }
                if (!target_dyn) {
                    target_dyn = engine.get_dynamic_array(scope, arr_name);
                }
                if (!target_dyn && !engine.active_scope_name.empty()) {
                    target_dyn = engine.get_dynamic_array(engine.active_scope_name, arr_name);
                }
                if (target_dyn) {
                    oa->size = static_cast<int>(target_dyn->size());
                    oa->elem_width = static_cast<int>(target_dyn->elem_width);
                    oa->source_dyn = target_dyn;
                    oa->int32_elements.resize(oa->size);
                    oa->int64_elements.resize(oa->size);
                    for (size_t j = 0; j < static_cast<size_t>(oa->size); ++j) {
                        LogicVal elem = target_dyn->get_elem(static_cast<int64_t>(j));
                        oa->int32_elements[j] = static_cast<int32_t>(elem.aval);
                        oa->int64_elements[j] = static_cast<int64_t>(elem.aval);
                    }
                }
            }

            c_args.push_back(reinterpret_cast<uint64_t>(oa.get()));
            open_records.push_back(OpenArrayRecord{i, std::move(oa)});
        } else {
            LogicVal v = eval(args[i].get(), scope);
            if (v.is_real) {
                c_args.push_back(static_cast<uint64_t>(v.real_val));
            } else {
                c_args.push_back(v.aval);
            }
        }
    }

    LogicVal result = invoke_c_symbol(sym, dpi, c_args);

    // Write back any modified open array elements for output or inout ports
    for (const auto& rec : open_records) {
        if (rec.arg_idx < dpi->tf_ports.size() &&
            (dpi->tf_ports[rec.arg_idx].direction == "output" || dpi->tf_ports[rec.arg_idx].direction == "inout")) {
            if (rec.oa->source_mem) {
                for (size_t j = 0; j < static_cast<size_t>(rec.oa->size); ++j) {
                    int64_t sv_idx = rec.oa->source_mem->is_descending ? (rec.oa->source_mem->end_idx - static_cast<int64_t>(j)) : (rec.oa->source_mem->start_idx + static_cast<int64_t>(j));
                    uint64_t val = (rec.oa->elem_width <= 32) ? static_cast<uint32_t>(rec.oa->int32_elements[j]) : static_cast<uint64_t>(rec.oa->int64_elements[j]);
                    rec.oa->source_mem->set_word(sv_idx, LogicVal::make_known(rec.oa->elem_width, val));
                }
            } else if (rec.oa->source_dyn) {
                for (size_t j = 0; j < static_cast<size_t>(rec.oa->size); ++j) {
                    uint64_t val = (rec.oa->elem_width <= 32) ? static_cast<uint32_t>(rec.oa->int32_elements[j]) : static_cast<uint64_t>(rec.oa->int64_elements[j]);
                    rec.oa->source_dyn->set_elem(static_cast<int64_t>(j), LogicVal::make_known(rec.oa->elem_width, val));
                }
            }
        }
    }

    return result;
}

LogicVal ExprEvaluator::eval_dpi_call(DpiImportNode* dpi, const std::vector<LogicVal>& evaluated_args, const std::string& scope) {
    void* sym = engine.dpi_registry.get_symbol(dpi->c_name, engine.source_search_paths);
    if (!sym) {
        throw std::runtime_error("[CAS-DPI-ERR] Runtime error: DPI-C foreign function symbol '" + dpi->c_name + "' could not be resolved from shared library");
    }

    struct ContextScopeGuard {
        bool is_context;
        svScope prev_scope;
        void* prev_eng;
        ContextScopeGuard(bool ctx, const std::string& call_scope, Engine* eng) : is_context(ctx), prev_scope(nullptr), prev_eng(nullptr) {
            if (is_context) {
                prev_scope = DpiRegistry::set_active_scope_by_name(call_scope);
                prev_eng = DpiRegistry::get_active_engine();
                DpiRegistry::set_active_engine(eng);
            }
        }
        ~ContextScopeGuard() {
            if (is_context) {
                DpiRegistry::set_active_scope(prev_scope);
                DpiRegistry::set_active_engine(prev_eng);
            }
        }
    };

    std::string call_scope = !scope.empty() ? scope : ((current_bp && !current_bp->scope.empty()) ? current_bp->scope : "");
    if (call_scope.empty()) {
        call_scope = engine.active_scope_name;
    }
    ContextScopeGuard scope_guard(dpi->is_context, call_scope, &engine);

    std::vector<uint64_t> c_args;
    for (size_t i = 0; i < evaluated_args.size(); ++i) {
        const auto& v = evaluated_args[i];
        if (v.is_real) {
            c_args.push_back(static_cast<uint64_t>(v.real_val));
        } else {
            c_args.push_back(v.aval);
        }
    }

    return invoke_c_symbol(sym, dpi, c_args);
}

bool ExprEvaluator::get_array_elements(const std::string& var_name, const std::string& scope,
                                       std::vector<LogicVal>& out_elements, uint32_t& out_elem_width, bool& out_is_signed) {
    out_elements.clear();
    out_elem_width = 32;
    out_is_signed = false;

    // 1. Check local variable
    if (context.has_active_frame()) {
        LocalVar* loc = context.lookup_local(var_name);
        if (loc) {
            if (loc->is_dynamic_array) {
                out_elements = loc->dyn_array_data.elements;
                out_elem_width = loc->dyn_array_data.elem_width;
                out_is_signed = loc->dyn_array_data.is_signed;
                return true;
            }
            if (loc->is_queue) {
                out_elements.assign(loc->queue_data.elements.begin(), loc->queue_data.elements.end());
                out_elem_width = loc->queue_data.elem_width;
                out_is_signed = loc->queue_data.is_signed;
                return true;
            }
        }
    }

    // 2. Check call stack
    if (current_bp && !current_bp->call_stack.empty()) {
        for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
            LocalVar* loc = it->local_frame.get_var(var_name);
            if (loc) {
                if (loc->is_dynamic_array) {
                    out_elements = loc->dyn_array_data.elements;
                    out_elem_width = loc->dyn_array_data.elem_width;
                    out_is_signed = loc->dyn_array_data.is_signed;
                    return true;
                }
                if (loc->is_queue) {
                    out_elements.assign(loc->queue_data.elements.begin(), loc->queue_data.elements.end());
                    out_elem_width = loc->queue_data.elem_width;
                    out_is_signed = loc->queue_data.is_signed;
                    return true;
                }
            }
        }
    }

    // 3. Check engine dynamic array
    DynamicArrayObject* da = engine.get_dynamic_array(var_name, scope);
    if (da) {
        out_elements = da->elements;
        out_elem_width = da->elem_width;
        out_is_signed = da->is_signed;
        return true;
    }

    // 4. Check engine queue
    QueueObject* q = engine.get_queue(var_name, scope);
    if (q) {
        out_elements.assign(q->elements.begin(), q->elements.end());
        out_elem_width = q->elem_width;
        out_is_signed = q->is_signed;
        return true;
    }

    // 5. Check engine memory (unpacked array)
    std::string s_id = scope.empty() ? var_name : (scope + "." + var_name);
    std::string mem_key = "";
    if (engine.memory_map.find(s_id) != engine.memory_map.end()) mem_key = s_id;
    else if (engine.memory_map.find(var_name) != engine.memory_map.end()) mem_key = var_name;
    if (!mem_key.empty()) {
        auto& mem = engine.memory_map.at(mem_key);
        out_elements.reserve(mem.depth);
        for (size_t i = 0; i < mem.depth; ++i) {
            out_elements.push_back(mem.get_word(i));
        }
        out_elem_width = mem.word_width;
        out_is_signed = false;
        return true;
    }

    // 6. Check engine associative array
    AssociativeArrayObject* assoc = engine.get_assoc_array(var_name, scope);
    if (assoc) {
        for (const auto& pair : assoc->elements) {
            out_elements.push_back(pair.second);
        }
        for (const auto& pair : assoc->str_elements) {
            out_elements.push_back(pair.second);
        }
        out_elem_width = assoc->val_width;
        out_is_signed = assoc->is_signed;
        return true;
    }

    return false;
}

LogicVal ExprEvaluator::eval_array_reduction(
    const std::string& method,
    const std::vector<LogicVal>& elements,
    uint32_t elem_width,
    bool is_signed,
    ExpressionNode* with_expr,
    const std::string& iter_name,
    const std::string& scope) {

    if (elements.empty()) {
        return LogicVal::make_known(elem_width, 0);
    }

    uint64_t total = 0;
    uint64_t prod = 1;
    uint64_t bit_and = (elem_width >= 64) ? ~0ULL : ((1ULL << elem_width) - 1);
    uint64_t bit_or = 0;
    uint64_t bit_xor = 0;

    for (const auto& elem : elements) {
        LogicVal val = elem;
        if (with_expr) {
            context.push_frame("with_frame");
            context.declare_local(iter_name, elem_width, is_signed);
            context.set_local_value(iter_name, elem);
            val = eval(with_expr, scope);
            context.pop_frame();
        }

        uint64_t cur = val.aval;
        total += cur;
        prod *= cur;
        bit_and &= cur;
        bit_or |= cur;
        bit_xor ^= cur;
    }

    uint64_t mask = (elem_width >= 64) ? ~0ULL : ((1ULL << elem_width) - 1);

    if (method == "sum") {
        return LogicVal::make_known(elem_width, total & mask);
    } else if (method == "product") {
        return LogicVal::make_known(elem_width, prod & mask);
    } else if (method == "and") {
        return LogicVal::make_known(elem_width, bit_and & mask);
    } else if (method == "or") {
        return LogicVal::make_known(elem_width, bit_or & mask);
    } else if (method == "xor") {
        return LogicVal::make_known(elem_width, bit_xor & mask);
    }

    return LogicVal::make_known(elem_width, 0);
}

std::vector<LogicVal> ExprEvaluator::eval_array_locator(
    const std::string& method,
    const std::vector<LogicVal>& elements,
    uint32_t elem_width,
    bool is_signed,
    ExpressionNode* with_expr,
    const std::string& iter_name,
    const std::string& scope) {

    std::vector<LogicVal> result;
    if (elements.empty()) return result;

    if (method == "min" || method == "max") {
        bool is_min = (method == "min");
        size_t best_idx = 0;
        LogicVal best_key;

        for (size_t i = 0; i < elements.size(); ++i) {
            LogicVal key = elements[i];
            if (with_expr) {
                context.push_frame("with_frame");
                context.declare_local(iter_name, elem_width, is_signed);
                context.set_local_value(iter_name, elements[i]);
                key = eval(with_expr, scope);
                context.pop_frame();
            }

            if (i == 0) {
                best_idx = 0;
                best_key = key;
            } else {
                bool better = false;
                if (is_signed) {
                    int64_t s_cur = (int64_t)LogicOps::cast_width(key, 64, true).aval;
                    int64_t s_best = (int64_t)LogicOps::cast_width(best_key, 64, true).aval;
                    better = is_min ? (s_cur < s_best) : (s_cur > s_best);
                } else {
                    better = is_min ? (key.aval < best_key.aval) : (key.aval > best_key.aval);
                }
                if (better) {
                    best_idx = i;
                    best_key = key;
                }
            }
        }
        result.push_back(elements[best_idx]);
        return result;
    }

    if (method == "find" || method == "find_index" ||
        method == "find_first" || method == "find_last" ||
        method == "find_first_index" || method == "find_last_index") {

        for (size_t i = 0; i < elements.size(); ++i) {
            bool matches = true;
            if (with_expr) {
                context.push_frame("with_frame");
                context.declare_local(iter_name, elem_width, is_signed);
                context.set_local_value(iter_name, elements[i]);
                LogicVal cond = eval(with_expr, scope);
                context.pop_frame();
                matches = (!cond.is_unknown() && cond.aval != 0);
            }

            if (matches) {
                LogicVal item_val = (method == "find_index" || method == "find_first_index" || method == "find_last_index")
                                    ? LogicVal::make_known(32, i)
                                    : elements[i];
                if (method == "find_first" || method == "find_first_index") {
                    result.push_back(item_val);
                    return result;
                }
                if (method == "find_last" || method == "find_last_index") {
                    result.clear();
                    result.push_back(item_val);
                } else {
                    result.push_back(item_val);
                }
            }
        }
        return result;
    }

    return result;
}

std::vector<LogicVal> ExprEvaluator::eval_array_expr(ExpressionNode* expr, const std::string& scope) {
    if (!expr) return {};

    if (auto mc = dynamic_cast<MethodCallNode*>(expr)) {
        std::vector<LogicVal> elements;
        uint32_t elem_width = 32;
        bool is_signed = false;
        if (get_array_elements(mc->var_name, scope, elements, elem_width, is_signed)) {
            std::string iter_name = "item";
            if (!mc->args.empty()) {
                if (auto id = dynamic_cast<IdentifierNode*>(mc->args[0].get())) {
                    iter_name = id->name;
                }
            }
            if (mc->method_name == "min" || mc->method_name == "max" ||
                mc->method_name == "find" || mc->method_name == "find_index" ||
                mc->method_name == "find_first" || mc->method_name == "find_last" ||
                mc->method_name == "find_first_index" || mc->method_name == "find_last_index") {
                return eval_array_locator(mc->method_name, elements, elem_width, is_signed, mc->with_expr.get(), iter_name, scope);
            }
        }
    }

    if (auto idx = dynamic_cast<IndexSelectNode*>(expr)) {
        if (idx->is_part_select && idx->lsb_expr) {
            std::vector<LogicVal> elements;
            uint32_t elem_width = 32;
            bool is_signed = false;
            if (get_array_elements(idx->name, scope, elements, elem_width, is_signed)) {
                int64_t msb = (int64_t)eval(idx->msb_expr.get(), scope).aval;
                int64_t lsb = (int64_t)eval(idx->lsb_expr.get(), scope).aval;
                std::vector<LogicVal> slice;
                if (msb <= lsb) {
                    for (int64_t i = msb; i <= lsb; ++i) {
                        if (i >= 0 && (size_t)i < elements.size()) slice.push_back(elements[i]);
                        else slice.push_back(LogicVal::make_x(elem_width));
                    }
                } else {
                    for (int64_t i = msb; i >= lsb; --i) {
                        if (i >= 0 && (size_t)i < elements.size()) slice.push_back(elements[i]);
                        else slice.push_back(LogicVal::make_x(elem_width));
                    }
                }
                return slice;
            }
        }
    }

    if (auto id = dynamic_cast<IdentifierNode*>(expr)) {
        std::vector<LogicVal> elements;
        uint32_t elem_width = 32;
        bool is_signed = false;
        if (get_array_elements(id->name, scope, elements, elem_width, is_signed)) {
            return elements;
        }
    }

    return {};
}

bool ExprEvaluator::get_stream_bits(ExpressionNode* expr, const std::string& scope, std::vector<std::pair<bool, bool>>& out_bits) {
    if (!expr) return false;

    if (auto id = dynamic_cast<IdentifierNode*>(expr)) {
        // 1. Check local queue / dynamic array
        LocalVar* lv = nullptr;
        if (context.has_active_frame()) {
            lv = context.lookup_local(id->name);
        }
        if (!lv && current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                lv = it->local_frame.get_var(id->name);
                if (lv) break;
            }
        }
        if (lv) {
            if (lv->is_class_handle || lv->is_chandle || lv->is_virtual_interface) {
                throw std::runtime_error("[CAS-DTINPCIL] Non-bit-stream type in bit-stream cast per IEEE 1800-2023 section 6.24.3: '" + id->name + "'");
            }
            if (lv->is_unpacked_array) {
                for (const auto& elem : lv->unpacked_elements) {
                    for (int b = (int)elem.width - 1; b >= 0; --b) {
                        out_bits.push_back({ (bool)((elem.aval >> b) & 1), (bool)((elem.bval >> b) & 1) });
                    }
                }
                return true;
            }
            if (lv->is_queue) {
                for (const auto& elem : lv->queue_data.elements) {
                    for (int b = (int)elem.width - 1; b >= 0; --b) {
                        out_bits.push_back({ (bool)((elem.aval >> b) & 1), (bool)((elem.bval >> b) & 1) });
                    }
                }
                return true;
            }
            if (lv->is_dynamic_array) {
                for (const auto& elem : lv->dyn_array_data.elements) {
                    for (int b = (int)elem.width - 1; b >= 0; --b) {
                        out_bits.push_back({ (bool)((elem.aval >> b) & 1), (bool)((elem.bval >> b) & 1) });
                    }
                }
                return true;
            }
        }

        // 2. Check engine queue
        QueueObject* q = engine.get_queue(id->name, scope);
        if (q) {
            for (const auto& elem : q->elements) {
                for (int b = (int)elem.width - 1; b >= 0; --b) {
                    out_bits.push_back({ (bool)((elem.aval >> b) & 1), (bool)((elem.bval >> b) & 1) });
                }
            }
            return true;
        }

        // 3. Check engine dynamic array
        DynamicArrayObject* da = engine.get_dynamic_array(id->name, scope);
        if (da) {
            for (const auto& elem : da->elements) {
                for (int b = (int)elem.width - 1; b >= 0; --b) {
                    out_bits.push_back({ (bool)((elem.aval >> b) & 1), (bool)((elem.bval >> b) & 1) });
                }
            }
            return true;
        }

        // 4. Check unpacked memory
        std::string cur_s = scope;
        auto it_mem = engine.memory_map.find(scope.empty() ? id->name : (scope + "." + id->name));
        while (it_mem == engine.memory_map.end() && !cur_s.empty()) {
            it_mem = engine.memory_map.find(cur_s + "." + id->name);
            if (it_mem != engine.memory_map.end()) break;
            auto dot = cur_s.rfind('.');
            if (dot == std::string::npos) cur_s = "";
            else cur_s = cur_s.substr(0, dot);
        }
        if (it_mem == engine.memory_map.end()) it_mem = engine.memory_map.find(id->name);
        if (it_mem != engine.memory_map.end()) {
            const auto& mem = it_mem->second;
            for (size_t i = 0; i < mem.words.size(); ++i) {
                const LogicVal& w = mem.words[i];
                for (int b = (int)w.width - 1; b >= 0; --b) {
                    out_bits.push_back({ (bool)((w.aval >> b) & 1), (bool)((w.bval >> b) & 1) });
                }
            }
            return true;
        }

        if (engine.is_class_handle(id->name, scope) || engine.is_chandle(id->name, scope) || engine.is_virtual_interface(id->name, scope)) {
            throw std::runtime_error("[CAS-DTINPCIL] Non-bit-stream type in bit-stream cast per IEEE 1800-2023 section 6.24.3: '" + id->name + "'");
        }
    }

    LogicVal val = eval(expr, scope);
    if (val.is_unpacked) {
        for (const auto& elem : val.unpacked_elements) {
            for (int b = (int)elem.width - 1; b >= 0; --b) {
                out_bits.push_back({ (bool)((elem.aval >> b) & 1), (bool)((elem.bval >> b) & 1) });
            }
        }
        return true;
    }
    if (val.is_string) {
        for (char c : val.str_val) {
            for (int b = 7; b >= 0; --b) {
                out_bits.push_back({ (bool)((c >> b) & 1), false });
            }
        }
        return true;
    }

    for (int b = (int)val.width - 1; b >= 0; --b) {
        out_bits.push_back({ (bool)((val.aval >> b) & 1), (bool)((val.bval >> b) & 1) });
    }
    return true;
}

LogicVal ExprEvaluator::eval_streaming_concat(StreamingConcatNode* sc, const std::string& scope) {
    if (!sc) return LogicVal::make_x(1);

    int slice_sz = 1;
    if (sc->slice_size) {
        LogicVal sz_v = eval(sc->slice_size.get(), scope);
        slice_sz = (int)sz_v.to_signed();
        if (slice_sz <= 0) {
            throw std::runtime_error("[CAS-DTINPCIL] Invalid streaming slice size <= 0 (" + std::to_string(slice_sz) + ") per IEEE 1800-2023 section 11.4.14");
        }
    }

    std::vector<std::pair<bool, bool>> bits;
    for (const auto& item : sc->stream_items) {
        get_stream_bits(item.get(), scope, bits);
    }

    if (bits.empty()) {
        return LogicVal(0, 0, 0);
    }

    std::vector<std::pair<bool, bool>> result_bits;
    if (sc->direction == StreamDirection::LEFT_TO_RIGHT) {
        result_bits = std::move(bits);
    } else {
        // RIGHT_TO_LEFT (<<)
        int N = (int)bits.size();
        std::vector<std::vector<std::pair<bool, bool>>> slices;
        int cur = N;
        while (cur > 0) {
            int start = std::max(0, cur - slice_sz);
            std::vector<std::pair<bool, bool>> sl(bits.begin() + start, bits.begin() + cur);
            slices.push_back(sl);
            cur = start;
        }
        for (const auto& sl : slices) {
            result_bits.insert(result_bits.end(), sl.begin(), sl.end());
        }
    }

    uint32_t total_w = (uint32_t)result_bits.size();
    if (total_w <= 64) {
        uint64_t aval = 0;
        uint64_t bval = 0;
        for (size_t i = 0; i < result_bits.size(); ++i) {
            if (result_bits[i].first) aval |= (1ULL << (total_w - 1 - i));
            if (result_bits[i].second) bval |= (1ULL << (total_w - 1 - i));
        }
        return LogicVal(total_w, aval, bval);
    } else {
        uint64_t aval = 0;
        uint64_t bval = 0;
        for (size_t i = 0; i < 64 && i < result_bits.size(); ++i) {
            if (result_bits[i].first) aval |= (1ULL << (63 - i));
            if (result_bits[i].second) bval |= (1ULL << (63 - i));
        }
        return LogicVal(64, aval, bval);
    }
}
