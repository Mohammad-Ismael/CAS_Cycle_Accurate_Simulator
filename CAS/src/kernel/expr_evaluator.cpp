#include "../../include/kernel/expr_evaluator.hpp"
#include "../../include/engine.hpp"
#include "../../include/kernel/logic_ops.hpp"
#include "../../include/frontend/parser.hpp"
#include "../../include/kernel/trace_buffer.hpp"
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

    if (auto tnn = dynamic_cast<TypeNameNode*>(expr)) {
        return eval_typename(tnn, scope);
    }

    if (auto ton = dynamic_cast<TypeOpNode*>(expr)) {
        return eval_typeop(ton, scope);
    }

    if (auto vin = dynamic_cast<VectorIntrinsicNode*>(expr)) {
        return eval_vector_intrinsic(vin, scope);
    }
    if (auto clog = dynamic_cast<Clog2Node*>(expr)) {
        return eval_clog2(clog, scope);
    }
    if (auto cbn = dynamic_cast<CountBitsNode*>(expr)) {
        return eval_countbits(cbn, scope);
    }
    if (auto adqn = dynamic_cast<ArrayDimensionQueryNode*>(expr)) {
        return eval_array_dimension_query(adqn, scope);
    }
    if (auto abqn = dynamic_cast<ArrayBoundQueryNode*>(expr)) {
        return eval_array_bound_query(abqn, scope);
    }
    if (auto sfn = dynamic_cast<SFormatfNode*>(expr)) {
        return eval_sformatf(sfn, scope);
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

        // Check Parser::find_type_dim_metadata
        if (!found && !var_name.empty()) {
            const VariableDimMetadata* tmeta = Parser::find_type_dim_metadata(var_name);
            if (tmeta && !tmeta->dimensions.empty()) {
                meta = *tmeta;
                found = true;
            }
        }

        // Diagnostic enforcement: non-array / non-vector variables
        if (!found || !meta.is_vector_or_array || meta.dimensions.empty()) {
            throw std::runtime_error("[CAS-DTINPCIL] Illegal first argument to array query system function; please pass an array identifier or array data type");
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
        if (!cast_call->dest_expr) {
            throw std::runtime_error("[CAS-DTINPCIL] Destination argument of $cast must be a singular variable lvalue per IEEE 1800-2023 section 6.24.2");
        }

        std::string dest_name;
        std::string obj_name;
        bool is_member = false;
        bool is_index = false;

        if (auto id = dynamic_cast<IdentifierNode*>(cast_call->dest_expr.get())) {
            dest_name = id->name;
        } else if (auto mem = dynamic_cast<MemberAccessNode*>(cast_call->dest_expr.get())) {
            dest_name = mem->member_name;
            obj_name = mem->var_name;
            is_member = true;
        } else if (auto idx = dynamic_cast<IndexSelectNode*>(cast_call->dest_expr.get())) {
            dest_name = idx->name;
            is_index = true;
        } else {
            throw std::runtime_error("[CAS-DTINPCIL] Destination argument of $cast must be a singular variable lvalue per IEEE 1800-2023 section 6.24.2");
        }

        // Check if destination is a parameter or constant
        std::string param_key = scope.empty() ? dest_name : (scope + "." + dest_name);
        if (engine.param_map.count(param_key) || engine.param_map.count(dest_name)) {
            throw std::runtime_error("[CAS-DTINPCIL] Destination argument of $cast must be a variable, not a constant/parameter per IEEE 1800-2023 section 6.24.2: '" + dest_name + "'");
        }

        // Check if destination is a Net (wire, wand, wor, tri) rather than a variable
        size_t dest_nid = engine.get_net_id(scope, dest_name);
        if (dest_nid != (size_t)-1 && !is_member && !is_index) {
            if (engine.netlist[dest_nid].kind != NetKind::VAR) {
                throw std::runtime_error("[CAS-DTINPCIL] Destination argument of $cast must be a variable, not a net per IEEE 1800-2023 section 6.24.2: '" + dest_name + "'");
            }
        }

        LocalVar* dest_local = nullptr;
        if (!is_member) {
            if (context.has_active_frame()) {
                dest_local = context.lookup_local(dest_name);
            }
            if (!dest_local && current_bp && !current_bp->call_stack.empty()) {
                for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                    dest_local = it->local_frame.get_var(dest_name);
                    if (dest_local) break;
                }
            }
        }

        // 1. Check if destination is a Class Handle
        bool is_class_dest = false;
        std::string dest_class_type;
        if (dest_local) {
            if (dest_local->is_class_handle || engine.is_class(dest_local->class_type_name) || engine.is_class(dest_local->type_name)) {
                is_class_dest = true;
                dest_class_type = !dest_local->class_type_name.empty() ? dest_local->class_type_name : dest_local->type_name;
            }
        } else if (dest_nid != (size_t)-1) {
            if (engine.netlist[dest_nid].is_class_handle || engine.is_class(engine.netlist[dest_nid].type_name)) {
                is_class_dest = true;
                dest_class_type = !engine.netlist[dest_nid].type_name.empty() ? engine.netlist[dest_nid].type_name : engine.get_class_type(dest_name, scope);
            }
        } else if (current_this && current_this->has_property(dest_name)) {
            if (current_this->class_def) {
                auto mv = current_this->class_def->find_member_var(dest_name);
                if (mv && (mv->is_class_handle || engine.is_class(mv->class_type_name))) {
                    is_class_dest = true;
                    dest_class_type = mv->class_type_name;
                }
            }
        }

        if (is_class_dest) {
            if (dynamic_cast<NullNode*>(cast_call->src_expr.get())) {
                if (dest_local) dest_local->value = LogicVal(64, 0, 0);
                else if (dest_nid != (size_t)-1) engine.netlist[dest_nid].set(LogicVal(64, 0, 0));
                else if (current_this && current_this->has_property(dest_name)) current_this->set_property(dest_name, LogicVal(64, 0, 0));
                return LogicVal::make_known(32, 1);
            }

            // Incompatible type validation
            if (auto src_id = dynamic_cast<IdentifierNode*>(cast_call->src_expr.get())) {
                LocalVar* sloc = nullptr;
                if (context.has_active_frame()) sloc = context.lookup_local(src_id->name);
                if (!sloc && current_bp && !current_bp->call_stack.empty()) {
                    for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                        sloc = it->local_frame.get_var(src_id->name);
                        if (sloc) break;
                    }
                }
                size_t snid = engine.get_net_id(scope, src_id->name);
                if (sloc && !sloc->is_class_handle && !engine.is_class(sloc->type_name) && sloc->type_name != "chandle") {
                    throw std::runtime_error("[CAS-DTINPCIL] Incompatible source type in $cast to class handle per IEEE 1800-2023 section 6.24.2: '" + src_id->name + "'");
                }
                if (!sloc && snid != (size_t)-1 && !engine.netlist[snid].is_class_handle && !engine.is_class(engine.netlist[snid].type_name)) {
                    throw std::runtime_error("[CAS-DTINPCIL] Incompatible source type in $cast to class handle per IEEE 1800-2023 section 6.24.2: '" + src_id->name + "'");
                }
            }

            LogicVal src_val = eval(cast_call->src_expr.get(), scope);
            if (src_val.is_string || src_val.is_real) {
                throw std::runtime_error("[CAS-DTINPCIL] Incompatible source type in $cast to class handle per IEEE 1800-2023 section 6.24.2");
            }

            uint64_t src_handle = src_val.aval;
            if (src_handle == 0) {
                if (dest_local) dest_local->value = LogicVal(64, 0, 0);
                else if (dest_nid != (size_t)-1) engine.netlist[dest_nid].set(LogicVal(64, 0, 0));
                else if (current_this && current_this->has_property(dest_name)) current_this->set_property(dest_name, LogicVal(64, 0, 0));
                return LogicVal::make_known(32, 1);
            }

            auto src_obj = engine.get_class_object(src_handle);
            if (!src_obj) {
                return LogicVal::make_known(32, 0);
            }

            if (src_obj->is_instance_of(dest_class_type)) {
                if (dest_local) dest_local->value = LogicVal(64, src_handle, 0);
                else if (dest_nid != (size_t)-1) engine.netlist[dest_nid].set(LogicVal(64, src_handle, 0));
                else if (current_this && current_this->has_property(dest_name)) current_this->set_property(dest_name, LogicVal(64, src_handle, 0));
                return LogicVal::make_known(32, 1);
            } else {
                return LogicVal::make_known(32, 0);
            }
        }

        // 2. Check if destination is an Enum variable
        const EnumType* dest_enum_type = nullptr;
        std::string dest_tname = dest_local ? dest_local->type_name : (dest_nid != (size_t)-1 ? engine.netlist[dest_nid].type_name : "");
        if (!dest_tname.empty()) {
            std::string scoped_et = scope.empty() ? dest_tname : (scope + "." + dest_tname);
            if (engine.enum_types.find(scoped_et) != engine.enum_types.end()) {
                dest_enum_type = &engine.enum_types.at(scoped_et);
            } else if (engine.enum_types.find(dest_tname) != engine.enum_types.end()) {
                dest_enum_type = &engine.enum_types.at(dest_tname);
            } else {
                for (const auto& pkg_pair : engine.package_defs) {
                    std::string qname = pkg_pair.first + "::" + dest_tname;
                    if (engine.enum_types.find(qname) != engine.enum_types.end()) {
                        dest_enum_type = &engine.enum_types.at(qname);
                        break;
                    }
                }
            }
        }
        if (!dest_enum_type) {
            std::string scoped_var = scope.empty() ? dest_name : (scope + "." + dest_name);
            if (engine.var_to_enum_type.find(scoped_var) != engine.var_to_enum_type.end()) {
                std::string et_name = engine.var_to_enum_type.at(scoped_var);
                if (engine.enum_types.find(et_name) != engine.enum_types.end()) {
                    dest_enum_type = &engine.enum_types.at(et_name);
                }
            } else if (engine.var_to_enum_type.find(dest_name) != engine.var_to_enum_type.end()) {
                std::string et_name = engine.var_to_enum_type.at(dest_name);
                if (engine.enum_types.find(et_name) != engine.enum_types.end()) {
                    dest_enum_type = &engine.enum_types.at(et_name);
                }
            }
        }

        if (dest_enum_type) {
            if (dynamic_cast<NullNode*>(cast_call->src_expr.get())) {
                throw std::runtime_error("[CAS-DTINPCIL] Incompatible source type in $cast to enum per IEEE 1800-2023 section 6.24.2");
            }
            if (auto src_id = dynamic_cast<IdentifierNode*>(cast_call->src_expr.get())) {
                LocalVar* sloc = nullptr;
                if (context.has_active_frame()) sloc = context.lookup_local(src_id->name);
                if (!sloc && current_bp && !current_bp->call_stack.empty()) {
                    for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                        sloc = it->local_frame.get_var(src_id->name);
                        if (sloc) break;
                    }
                }
                size_t snid = engine.get_net_id(scope, src_id->name);
                if (sloc && (sloc->is_class_handle || sloc->is_chandle || sloc->is_virtual_interface)) {
                    throw std::runtime_error("[CAS-DTINPCIL] Incompatible source type in $cast to enum per IEEE 1800-2023 section 6.24.2: '" + src_id->name + "'");
                }
                if (!sloc && snid != (size_t)-1 && (engine.netlist[snid].is_class_handle || engine.netlist[snid].is_chandle || engine.netlist[snid].is_virtual_interface)) {
                    throw std::runtime_error("[CAS-DTINPCIL] Incompatible source type in $cast to enum per IEEE 1800-2023 section 6.24.2: '" + src_id->name + "'");
                }
            }

            LogicVal src_val = eval(cast_call->src_expr.get(), scope);
            if (src_val.is_string || src_val.is_real) {
                throw std::runtime_error("[CAS-DTINPCIL] Incompatible source type in $cast to enum per IEEE 1800-2023 section 6.24.2");
            }

            bool match_found = false;
            LogicVal matched_val;
            uint32_t ew = dest_enum_type->width;
            uint64_t mask = (ew >= 64) ? ~0ULL : ((1ULL << ew) - 1);

            bool out_of_width = false;
            if (!dest_enum_type->is_signed && src_val.width > ew) {
                if ((src_val.aval & ~mask) != 0 || (src_val.bval & ~mask) != 0) {
                    out_of_width = true;
                }
            }

            if (!out_of_width) {
                uint64_t masked_a = src_val.aval & mask;
                uint64_t masked_b = src_val.bval & mask;
                for (const auto& m : dest_enum_type->members) {
                    uint64_t m_mask = (m.val.width >= 64) ? ~0ULL : ((1ULL << m.val.width) - 1);
                    if ((m.val.aval & m_mask) == masked_a && (m.val.bval & m_mask) == masked_b) {
                        match_found = true;
                        matched_val = LogicVal(ew, masked_a, masked_b, dest_enum_type->is_signed);
                        break;
                    }
                }
            }

            if (match_found) {
                if (dest_local) dest_local->value = matched_val;
                else if (dest_nid != (size_t)-1) engine.netlist[dest_nid].set(matched_val);
                else if (current_this && current_this->has_property(dest_name)) current_this->set_property(dest_name, matched_val);
                return LogicVal::make_known(32, 1);
            } else {
                return LogicVal::make_known(32, 0);
            }
        }

        // 3. Singular Integral Destination
        if (dest_local || dest_nid != (size_t)-1 || (current_this && current_this->has_property(dest_name))) {
            if (dynamic_cast<NullNode*>(cast_call->src_expr.get())) {
                throw std::runtime_error("[CAS-DTINPCIL] Incompatible source type in $cast per IEEE 1800-2023 section 6.24.2");
            }
            if (auto src_id = dynamic_cast<IdentifierNode*>(cast_call->src_expr.get())) {
                LocalVar* sloc = nullptr;
                if (context.has_active_frame()) sloc = context.lookup_local(src_id->name);
                if (!sloc && current_bp && !current_bp->call_stack.empty()) {
                    for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                        sloc = it->local_frame.get_var(src_id->name);
                        if (sloc) break;
                    }
                }
                size_t snid = engine.get_net_id(scope, src_id->name);
                if (sloc && (sloc->is_class_handle || sloc->is_chandle || sloc->is_virtual_interface)) {
                    throw std::runtime_error("[CAS-DTINPCIL] Incompatible source type in $cast per IEEE 1800-2023 section 6.24.2: '" + src_id->name + "'");
                }
                if (!sloc && snid != (size_t)-1 && (engine.netlist[snid].is_class_handle || engine.netlist[snid].is_chandle || engine.netlist[snid].is_virtual_interface)) {
                    throw std::runtime_error("[CAS-DTINPCIL] Incompatible source type in $cast per IEEE 1800-2023 section 6.24.2: '" + src_id->name + "'");
                }
            }

            LogicVal src_val = eval(cast_call->src_expr.get(), scope);
            if (src_val.is_string || src_val.is_real) {
                throw std::runtime_error("[CAS-DTINPCIL] Incompatible source type in $cast per IEEE 1800-2023 section 6.24.2");
            }

            uint32_t dest_w = dest_local ? dest_local->width : (dest_nid != (size_t)-1 ? engine.netlist[dest_nid].width : 32);
            bool dest_s = dest_local ? dest_local->is_signed : (dest_nid != (size_t)-1 ? engine.netlist[dest_nid].is_signed : false);
            LogicVal casted = LogicOps::cast_width(src_val, dest_w, dest_s);
            if (dest_local) dest_local->value = casted;
            else if (dest_nid != (size_t)-1) engine.netlist[dest_nid].set(casted);
            else if (current_this && current_this->has_property(dest_name)) current_this->set_property(dest_name, casted);
            return LogicVal::make_known(32, 1);
        }

        throw std::runtime_error("[CAS-DTINPCIL] Undefined destination variable in $cast per IEEE 1800-2023 section 6.24.2: '" + dest_name + "'");
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
        bool has_unknown = false;
        for (const auto& r : inside_node->ranges) {
            if (r.high) {
                LogicVal lo = eval(r.low.get(), scope);
                LogicVal hi = eval(r.high.get(), scope);
                if (lo.is_string || lo.is_real || hi.is_string || hi.is_real || target_val.is_string || target_val.is_real) {
                    throw std::runtime_error("[CAS-DTINPCIL] Inside operator bounded range bounds must be integral values per IEEE 1800-2023 section 11.4.13");
                }
                if (target_val.is_unknown() || lo.is_unknown() || hi.is_unknown()) {
                    has_unknown = true;
                    continue;
                }
                bool is_signed = target_val.is_signed && lo.is_signed && hi.is_signed;
                if (is_signed) {
                    int64_t t = target_val.to_signed();
                    int64_t l = lo.to_signed();
                    int64_t h = hi.to_signed();
                    if ((l <= t && t <= h) || (h <= t && t <= l)) {
                        return LogicVal::make_known(1, 1);
                    }
                } else {
                    uint64_t t = target_val.aval;
                    uint64_t l = lo.aval;
                    uint64_t h = hi.aval;
                    if ((l <= t && t <= h) || (h <= t && t <= l)) {
                        return LogicVal::make_known(1, 1);
                    }
                }
            } else {
                LogicVal v = eval(r.low.get(), scope);
                LogicVal cmp = LogicOps::wildcard_eq(target_val, v);
                if (cmp.is_unknown()) {
                    has_unknown = true;
                } else if (cmp.aval == 1) {
                    return LogicVal::make_known(1, 1);
                }
            }
        }
        if (has_unknown) {
            return LogicVal::make_x(1);
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
        LogicVal lv(num->width, num->aval, num->bval, num->is_signed);
        if (!num->wide_aval.empty()) {
            lv.wide_aval = num->wide_aval;
            lv.wide_bval = num->wide_bval;
            lv.mask();
        }
        return lv;
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
                return LogicVal::make_known(32, static_cast<uint64_t>(std::round(val.real_val)), true);
            }
            LogicVal res = LogicOps::cast_width(val, 32, val.is_signed);
            res.is_signed = true;
            return res;
        }
        if (cast_node->target_type == "shortint") {
            if (val.is_real) {
                return LogicVal::make_known(16, static_cast<uint64_t>(std::round(val.real_val)), true);
            }
            LogicVal res = LogicOps::cast_width(val, 16, val.is_signed);
            res.is_signed = true;
            return res;
        }
        if (cast_node->target_type == "byte") {
            if (val.is_real) {
                return LogicVal::make_known(8, static_cast<uint64_t>(std::round(val.real_val)), true);
            }
            LogicVal res = LogicOps::cast_width(val, 8, val.is_signed);
            res.is_signed = true;
            return res;
        }
        if (cast_node->target_type == "longint") {
            if (val.is_real) {
                return LogicVal::make_known(64, static_cast<uint64_t>(std::round(val.real_val)), true);
            }
            LogicVal res = LogicOps::cast_width(val, 64, val.is_signed);
            res.is_signed = true;
            return res;
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
            uint32_t width = scast->target_width;
            if (!scast->target_type_name.empty()) {
                bool found = false;
                std::string cur_scope = scope;
                while (!cur_scope.empty()) {
                    std::string scoped_name = cur_scope + "." + scast->target_type_name;
                    if (engine.param_map.count(scoped_name)) {
                        width = static_cast<uint32_t>(engine.param_map.at(scoped_name));
                        found = true;
                        break;
                    }
                    auto dot = cur_scope.rfind('.');
                    if (dot == std::string::npos) break;
                    cur_scope = cur_scope.substr(0, dot);
                }
                if (!found && engine.param_map.count(scast->target_type_name)) {
                    width = static_cast<uint32_t>(engine.param_map.at(scast->target_type_name));
                    found = true;
                }
                if (!found) {
                    for (const auto& pkg : engine.package_defs) {
                        if (pkg.second->param_constants.count(scast->target_type_name)) {
                            width = static_cast<uint32_t>(pkg.second->param_constants.at(scast->target_type_name));
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    std::string memb = scast->target_type_name;
                    if (memb.rfind('.') != std::string::npos) memb = memb.substr(memb.rfind('.') + 1);
                    if (Lexer::global_defines.count("RV_" + memb)) {
                        try {
                            width = static_cast<uint32_t>(std::stoul(Lexer::global_defines["RV_" + memb], nullptr, 0));
                            if (width > 0) found = true;
                        } catch (...) {}
                    } else if (Lexer::global_defines.count(memb)) {
                        try {
                            width = static_cast<uint32_t>(std::stoul(Lexer::global_defines[memb], nullptr, 0));
                            if (width > 0) found = true;
                        } catch (...) {}
                    } else if (Lexer::global_defines.count("RV_" + scast->target_type_name)) {
                        try {
                            width = static_cast<uint32_t>(std::stoul(Lexer::global_defines["RV_" + scast->target_type_name], nullptr, 0));
                            if (width > 0) found = true;
                        } catch (...) {}
                    } else if (Lexer::global_defines.count(scast->target_type_name)) {
                        try {
                            width = static_cast<uint32_t>(std::stoul(Lexer::global_defines[scast->target_type_name], nullptr, 0));
                            if (width > 0) found = true;
                        } catch (...) {}
                    }
                }
            }
            LogicVal val = eval(scast->expr.get(), scope);
            if (width == 0) {
                std::string debug = "scope=" + scope + ", target_type_name=" + scast->target_type_name + "; matching params: ";
                for (const auto& kv : engine.param_map) {
                    if (kv.first.find(scast->target_type_name) != std::string::npos) {
                        debug += kv.first + "=" + std::to_string(kv.second) + "; ";
                    }
                }
                throw std::runtime_error("[CAS-DTINPCIL] Size cast bit width must be positive integer per IEEE 1800-2023 section 6.24.1: " + debug);
            }
            return LogicOps::cast_width(val, width, val.is_signed);
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
            if (auto ucat = dynamic_cast<UnpackedConcatNode*>(scast->expr.get())) {
                const PackedStructType* st_cast = find_struct_type(scast->target_type_name, scope);
                if (st_cast) {
                    return eval_struct_pattern(ucat, *st_cast, scope);
                }
            }
            LogicVal val = eval(scast->expr.get(), scope);
            std::string target_type = scast->target_type_name;
            if (target_type == "bit" || target_type == "logic" || target_type == "reg") {
                return LogicOps::cast_width(val, 1, false);
            }
            if (target_type == "int" || target_type == "integer") {
                if (val.is_real) {
                    return LogicVal::make_known(32, static_cast<uint64_t>(std::round(val.real_val)), true);
                }
                LogicVal res = LogicOps::cast_width(val, 32, val.is_signed);
                res.is_signed = true;
                return res;
            }
            if (target_type == "shortint") {
                if (val.is_real) {
                    return LogicVal::make_known(16, static_cast<uint64_t>(std::round(val.real_val)), true);
                }
                LogicVal res = LogicOps::cast_width(val, 16, val.is_signed);
                res.is_signed = true;
                return res;
            }
            if (target_type == "byte") {
                if (val.is_real) {
                    return LogicVal::make_known(8, static_cast<uint64_t>(std::round(val.real_val)), true);
                }
                LogicVal res = LogicOps::cast_width(val, 8, val.is_signed);
                res.is_signed = true;
                return res;
            }
            if (target_type == "longint") {
                if (val.is_real) {
                    return LogicVal::make_known(64, static_cast<uint64_t>(std::round(val.real_val)), true);
                }
                LogicVal res = LogicOps::cast_width(val, 64, val.is_signed);
                res.is_signed = true;
                return res;
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
                uint32_t param_w = 0;
                if (engine.param_map.count(target_type)) param_w = static_cast<uint32_t>(engine.param_map.at(target_type));
                else if (!scope.empty() && engine.param_map.count(scope + "." + target_type)) param_w = static_cast<uint32_t>(engine.param_map.at(scope + "." + target_type));
                else {
                    for (const auto& pkg : engine.package_defs) {
                        if (pkg.second->param_constants.count(target_type)) {
                            param_w = static_cast<uint32_t>(pkg.second->param_constants.at(target_type));
                            break;
                        }
                    }
                }
                if (param_w > 0) {
                    return LogicOps::cast_width(val, param_w, val.is_signed);
                }
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
                if (val.is_unpacked) {
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
                } else {
                    return LogicOps::cast_width(val, st->total_width, st->is_signed);
                }
            }

            // Destination 3: Simple Type / Vector Type
            if (si) {
                if (val.is_unpacked) {
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
                } else {
                    return LogicOps::cast_width(val, si->width, si->is_signed);
                }
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
        bool is_array = get_array_elements(var_name, scope, array_elems, a_elem_width, a_is_signed);
        std::string method = method_node->method_name;

        if (method == "sum" || method == "product" || method == "and" || method == "or" || method == "xor") {
            if (method_node->args.size() > 1) {
                throw std::runtime_error("[CAS-DTINPCIL] Array reduction method '" + method + "' accepts at most one iterator argument per IEEE 1800-2023 section 7.12");
            }
            if (!is_array) {
                throw std::runtime_error("[CAS-DTINPCIL] Array reduction method '" + method + "' called on non-array target '" + var_name + "' per IEEE 1800-2023 section 7.12");
            }
            std::string iter_name = "item";
            if (!method_node->args.empty()) {
                if (auto id = dynamic_cast<IdentifierNode*>(method_node->args[0].get())) {
                    iter_name = id->name;
                }
            }
            return eval_array_reduction(method, array_elems, a_elem_width, a_is_signed, method_node->with_expr.get(), iter_name, scope);
        }

        if (is_array) {
            std::string iter_name = "item";
            if (!method_node->args.empty()) {
                if (auto id = dynamic_cast<IdentifierNode*>(method_node->args[0].get())) {
                    iter_name = id->name;
                }
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

        // IEEE 1800-2023 Â§6.16: String Built-in Methods
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
        if (member_node->index_expr) {
            LogicVal idx_v = eval(member_node->index_expr.get(), scope);
            if (!idx_v.is_unknown()) {
                int64_t addr = (int64_t)idx_v.aval;
                LocalVar* local_arr = nullptr;
                if (context.has_active_frame()) local_arr = context.lookup_local(member_node->var_name);
                if (!local_arr && current_bp && !current_bp->call_stack.empty()) {
                    for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                        local_arr = it->local_frame.get_var(member_node->var_name);
                        if (local_arr) break;
                    }
                }
                if (local_arr) {
                    uint64_t handle_id = 0;
                    if (local_arr->is_unpacked_array) {
                        int64_t rel_idx = addr - local_arr->arr_start_idx;
                        if (rel_idx >= 0 && (size_t)rel_idx < local_arr->unpacked_elements.size()) {
                            handle_id = local_arr->unpacked_elements[rel_idx].aval;
                        }
                    } else if (local_arr->is_dynamic_array) {
                        if (addr >= 0 && (size_t)addr < local_arr->dyn_array_data.elements.size()) {
                            handle_id = local_arr->dyn_array_data.elements[addr].aval;
                        }
                    } else if (local_arr->is_queue) {
                        if (addr >= 0 && (size_t)addr < local_arr->queue_data.elements.size()) {
                            handle_id = local_arr->queue_data.elements[addr].aval;
                        }
                    }
                    if (handle_id == 0) {
                        std::string elem_vname = member_node->var_name + "[" + std::to_string(addr) + "]";
                        LocalVar* elem_lv = context.has_active_frame() ? context.lookup_local(elem_vname) : nullptr;
                        if (!elem_lv && current_bp && !current_bp->call_stack.empty()) {
                            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                                elem_lv = it->local_frame.get_var(elem_vname);
                                if (elem_lv) break;
                            }
                        }
                        if (elem_lv && elem_lv->is_class_handle) {
                            handle_id = elem_lv->value.aval;
                        }
                    }
                    if (handle_id != 0) {
                        auto cl_obj = engine.get_class_object(handle_id);
                        if (cl_obj) {
                            if (cl_obj->class_def && cl_obj->class_def->has_static_member(member_node->member_name)) {
                                return engine.get_class_static_var(cl_obj->class_type_name, member_node->member_name);
                            }
                            return cl_obj->get_property(member_node->member_name);
                        }
                    }
                }
                std::string scoped_mem = scope.empty() ? member_node->var_name : (scope + "." + member_node->var_name);
                auto it = engine.memory_map.find(scoped_mem);
                if (it == engine.memory_map.end()) {
                    std::string cur_s = scope;
                    while (!cur_s.empty() && engine.memory_map.find(cur_s + "." + member_node->var_name) == engine.memory_map.end()) {
                        auto dot = cur_s.rfind('.');
                        if (dot == std::string::npos) cur_s = "";
                        else cur_s = cur_s.substr(0, dot);
                    }
                    if (!cur_s.empty()) it = engine.memory_map.find(cur_s + "." + member_node->var_name);
                    else if (engine.memory_map.find(member_node->var_name) != engine.memory_map.end()) {
                        it = engine.memory_map.find(member_node->var_name);
                    }
                }
                if (it != engine.memory_map.end()) {
                    LogicVal cur_word = it->second.get_word(addr);
                    std::string type_name = "";
                    if (engine.var_to_struct_type.find(it->first) != engine.var_to_struct_type.end()) {
                        type_name = engine.var_to_struct_type.at(it->first);
                    } else if (engine.var_to_struct_type.find(member_node->var_name) != engine.var_to_struct_type.end()) {
                        type_name = engine.var_to_struct_type.at(member_node->var_name);
                    }
                    const PackedStructType* st = find_struct_type_by_member(type_name, member_node->member_name, cur_word.width, scope);
                    if (st) {
                        const StructMember* sm = st->get_member(member_node->member_name);
                        if (sm) {
                            uint32_t m_offset = sm->offset;
                            uint32_t m_width = sm->width;
                            if (member_node->has_member_select) {
                                if (member_node->is_part_select) {
                                    uint64_t msb = eval(member_node->member_msb_expr.get(), scope).aval;
                                    uint64_t lsb = eval(member_node->member_lsb_expr.get(), scope).aval;
                                    uint32_t eff_lsb = (uint32_t)std::min(msb, lsb);
                                    uint32_t w = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
                                    m_offset += eff_lsb;
                                    m_width = w;
                                } else {
                                    uint64_t b_idx = eval(member_node->member_msb_expr.get(), scope).aval;
                                    m_offset += (uint32_t)b_idx;
                                    m_width = 1;
                                }
                            }
                            return cur_word.extract_slice(m_offset, m_width);
                        }
                    }
                }
                size_t nid = engine.get_net_id(scope, member_node->var_name);
                if (nid != (size_t)-1) {
                    LogicVal net_val = engine.netlist[nid].value;
                    std::string full_name = engine.netlist[nid].name;
                    auto it_vdm = engine.var_dimensions.find(full_name);
                    if (it_vdm == engine.var_dimensions.end()) it_vdm = engine.var_dimensions.find(scope.empty() ? member_node->var_name : (scope + "." + member_node->var_name));
                    if (it_vdm == engine.var_dimensions.end()) it_vdm = engine.var_dimensions.find(member_node->var_name);

                    int64_t base_low = 0;
                    uint32_t elem_w = 1;
                    if (it_vdm != engine.var_dimensions.end() && !it_vdm->second.dimensions.empty()) {
                        base_low = it_vdm->second.dimensions[0].low();
                        if (it_vdm->second.dimensions.size() > 1) {
                            uint32_t stride0 = 1;
                            for (size_t k = 1; k < it_vdm->second.dimensions.size(); ++k) {
                                stride0 *= (uint32_t)it_vdm->second.dimensions[k].size();
                            }
                            elem_w = stride0;
                        } else if (it_vdm->second.dimensions[0].size() > 0 && net_val.width > (uint32_t)it_vdm->second.dimensions[0].size()) {
                            elem_w = (uint32_t)(net_val.width / it_vdm->second.dimensions[0].size());
                        }
                    }

                    int64_t elem_idx = addr;
                    if (base_low > 0 && elem_idx >= base_low) elem_idx -= base_low;
                    if (elem_idx >= 0 && (elem_idx * elem_w) < net_val.width) {
                        LogicVal cur_word = net_val.extract_slice((uint32_t)(elem_idx * elem_w), elem_w);
                        std::string type_name = engine.netlist[nid].type_name;
                        if (type_name.empty()) {
                            if (engine.var_to_struct_type.count(full_name)) {
                                type_name = engine.var_to_struct_type.at(full_name);
                            } else if (engine.var_to_struct_type.count(member_node->var_name)) {
                                type_name = engine.var_to_struct_type.at(member_node->var_name);
                            }
                        }
                        const PackedStructType* st = find_struct_type_by_member(type_name, member_node->member_name, cur_word.width, scope);
                        if (st) {
                            const StructMember* sm = st->get_member(member_node->member_name);
                            if (sm) {
                                uint32_t m_offset = sm->offset;
                                uint32_t m_width = sm->width;
                                if (member_node->has_member_select) {
                                    if (member_node->is_part_select) {
                                        uint64_t msb = eval(member_node->member_msb_expr.get(), scope).aval;
                                        uint64_t lsb = eval(member_node->member_lsb_expr.get(), scope).aval;
                                        uint32_t eff_lsb = (uint32_t)std::min(msb, lsb);
                                        uint32_t w = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
                                        m_offset += eff_lsb;
                                        m_width = w;
                                    } else {
                                        uint64_t b_idx = eval(member_node->member_msb_expr.get(), scope).aval;
                                        m_offset += (uint32_t)b_idx;
                                        m_width = 1;
                                    }
                                }
                                return cur_word.extract_slice(m_offset, m_width);
                            }
                        }
                    }
                }
            }
        }
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

        size_t iface_nid = engine.get_net_id(scope, var_name + "." + member_name);
        if (iface_nid != (size_t)-1) {
            LogicVal val = (preponed_override && iface_nid < preponed_override->size()) ? (*preponed_override)[iface_nid] : engine.netlist[iface_nid].value;
            if (member_node->has_member_select) {
                if (member_node->is_part_select) {
                    uint64_t msb = eval(member_node->member_msb_expr.get(), scope).aval;
                    uint64_t lsb = eval(member_node->member_lsb_expr.get(), scope).aval;
                    uint32_t eff_lsb = (uint32_t)std::min(msb, lsb);
                    uint32_t w = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
                    return val.extract_slice(eff_lsb, w);
                } else {
                    uint64_t b_idx = eval(member_node->member_msb_expr.get(), scope).aval;
                    return val.extract_slice((uint32_t)b_idx, 1);
                }
            }
            return val;
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
            if (local && local->is_class_handle && local->value.aval != 0) {
                cl_obj = engine.get_class_object(local->value.aval).get();
            }
        }
        if (!cl_obj && current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                LocalVar* local = it->local_frame.get_var(var_name);
                if (local && local->is_class_handle && local->value.aval != 0) {
                    cl_obj = engine.get_class_object(local->value.aval).get();
                    break;
                }
            }
        }
        if (!cl_obj) {
            size_t cl_nid = engine.get_net_id(scope, var_name);
            if (cl_nid != (size_t)-1 && engine.netlist[cl_nid].is_class_handle && engine.netlist[cl_nid].value.aval != 0) {
                cl_obj = engine.get_class_object(engine.netlist[cl_nid].value.aval).get();
            }
        }
        if (!cl_obj && var_name.find('[') != std::string::npos) {
            auto b_open = var_name.find('[');
            auto b_close = var_name.find(']', b_open);
            if (b_close != std::string::npos) {
                std::string arr_name = var_name.substr(0, b_open);
                int idx = std::stoi(var_name.substr(b_open + 1, b_close - b_open - 1));
                LocalVar* loc = nullptr;
                if (context.has_active_frame()) loc = context.lookup_local(arr_name);
                if (!loc && current_bp && !current_bp->call_stack.empty()) {
                    for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                        loc = it->local_frame.get_var(arr_name);
                        if (loc) break;
                    }
                }
                if (loc) {
                    if (loc->is_unpacked_array) {
                        if (idx >= 0 && idx < (int)loc->unpacked_elements.size()) {
                            cl_obj = engine.get_class_object(loc->unpacked_elements[idx].aval).get();
                        }
                    } else if (loc->is_dynamic_array) {
                        if (idx >= 0 && idx < (int)loc->dyn_array_data.elements.size()) {
                            cl_obj = engine.get_class_object(loc->dyn_array_data.elements[idx].aval).get();
                        }
                    } else if (loc->is_queue) {
                        if (idx >= 0 && idx < (int)loc->queue_data.elements.size()) {
                            cl_obj = engine.get_class_object(loc->queue_data.elements[idx].aval).get();
                        }
                    }
                }
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

        const PackedStructType* st = find_struct_type_by_member(type_name, member_name, var_val.width, scope);
        if (!st && !scoped_var.empty()) {
            st = find_struct_type_by_member(scoped_var, member_name, var_val.width, scope);
        }
        if (!st && !var_name.empty()) {
            st = find_struct_type_by_member(var_name, member_name, var_val.width, scope);
        }

        if (st) {
            const StructMember* sm = st->get_member(member_name);
            if (sm) {
                uint32_t m_offset = sm->offset;
                uint32_t m_width = sm->width;
                if (member_node->has_member_select) {
                    if (member_node->is_part_select) {
                        uint64_t msb = eval(member_node->member_msb_expr.get(), scope).aval;
                        uint64_t lsb = eval(member_node->member_lsb_expr.get(), scope).aval;
                        uint32_t eff_lsb = (uint32_t)std::min(msb, lsb);
                        uint32_t w = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
                        m_offset += eff_lsb;
                        m_width = w;
                    } else {
                        uint64_t b_idx = eval(member_node->member_msb_expr.get(), scope).aval;
                        m_offset += (uint32_t)b_idx;
                        m_width = 1;
                    }
                }
                return var_val.extract_slice(m_offset, m_width);
            }
        }

        // Upward scope search in param_val_map and param_map
        std::string cur_s = scope;
        while (!cur_s.empty()) {
            std::string scoped_id = cur_s + "." + var_name + "." + member_name;
            if (engine.param_val_map.count(scoped_id)) {
                return engine.param_val_map.at(scoped_id);
            }
            if (engine.param_map.count(scoped_id)) {
                return LogicVal::make_known(32, engine.param_map.at(scoped_id));
            }
            auto dot = cur_s.rfind('.');
            if (dot == std::string::npos) cur_s = "";
            else cur_s = cur_s.substr(0, dot);
        }
        std::string plain_id = var_name + "." + member_name;
        if (engine.param_val_map.count(plain_id)) {
            return engine.param_val_map.at(plain_id);
        }
        if (engine.param_map.count(plain_id)) {
            return LogicVal::make_known(32, engine.param_map.at(plain_id));
        }
        if (engine.param_val_map.count(member_name)) {
            return engine.param_val_map.at(member_name);
        }
        if (engine.param_map.count(member_name)) {
            return LogicVal::make_known(32, engine.param_map.at(member_name));
        }
        if (Lexer::global_defines.count("RV_" + member_name)) {
            try {
                uint64_t v = std::stoull(Lexer::global_defines.at("RV_" + member_name), nullptr, 0);
                return LogicVal::make_known(32, v);
            } catch (...) {}
        }
        if (Lexer::global_defines.count(member_name)) {
            try {
                uint64_t v = std::stoull(Lexer::global_defines.at(member_name), nullptr, 0);
                return LogicVal::make_known(32, v);
            } catch (...) {}
        }

        return LogicVal::make_x(1);
    }

    if (auto fn_call = dynamic_cast<FunctionCallNode*>(expr)) {
        if (fn_call->func_name == "$unsigned") {
            if (fn_call->args.empty()) return LogicVal::make_x(32);
            LogicVal v = eval(fn_call->args[0].get(), scope);
            v.is_signed = false;
            return v;
        }
        if (fn_call->func_name == "$signed") {
            if (fn_call->args.empty()) return LogicVal::make_x(32);
            LogicVal v = eval(fn_call->args[0].get(), scope);
            v.is_signed = true;
            return v;
        }
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
                    if (spec == 'm') {
                        ss << scope;
                        continue;
                    }

                    if (arg_idx < fn_call->args.size()) {
                        LogicVal val = eval(fn_call->args[arg_idx++].get(), scope);
                        if (spec == 'c') {
                            char ch = static_cast<char>(val.aval & 0xFF);
                            ss << ch;
                        } else if (spec == 's') {
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
        if (fn_call->func_name == "$bits") {
            if (fn_call->args.size() != 1) {
                throw std::runtime_error("[CAS-DTINPCIL] $bits requires exactly one argument per IEEE 1800-2023 section 20.6.1");
            }
            auto* arg = fn_call->args[0].get();
            if (auto id = dynamic_cast<IdentifierNode*>(arg)) {
                std::string name = id->name;
                if (name == "bit" || name == "logic" || name == "reg") return LogicVal::make_known(32, 1);
                if (name == "byte") return LogicVal::make_known(32, 8);
                if (name == "shortint") return LogicVal::make_known(32, 16);
                if (name == "int" || name == "integer") return LogicVal::make_known(32, 32);
                if (name == "longint" || name == "time") return LogicVal::make_known(32, 64);

                std::string cur_s = scope;
                while (!cur_s.empty()) {
                    std::string cand = cur_s + "." + name;
                    auto it_e = engine.enum_types.find(cand);
                    if (it_e != engine.enum_types.end()) return LogicVal::make_known(32, it_e->second.width);
                    auto it_s = engine.struct_types.find(cand);
                    if (it_s != engine.struct_types.end()) return LogicVal::make_known(32, it_s->second.total_width);
                    auto it_sim = engine.simple_types.find(cand);
                    if (it_sim != engine.simple_types.end()) return LogicVal::make_known(32, it_sim->second.width);
                    auto dot = cur_s.rfind('.');
                    if (dot == std::string::npos) cur_s = "";
                    else cur_s = cur_s.substr(0, dot);
                }
                auto it_e = engine.enum_types.find(name);
                if (it_e != engine.enum_types.end()) return LogicVal::make_known(32, it_e->second.width);
                auto it_s = engine.struct_types.find(name);
                if (it_s != engine.struct_types.end()) return LogicVal::make_known(32, it_s->second.total_width);
                auto it_sim = engine.simple_types.find(name);
                if (it_sim != engine.simple_types.end()) return LogicVal::make_known(32, it_sim->second.width);

                const VariableDimMetadata* tmeta = Parser::find_type_dim_metadata(name);
                if (tmeta && !tmeta->dimensions.empty()) {
                    uint64_t total = 1;
                    for (const auto& d : tmeta->dimensions) total *= (uint64_t)d.size();
                    if (total > 0) return LogicVal::make_known(32, total);
                }
                for (const auto& kv : Parser::global_packages) {
                    auto sit = kv.second->struct_typedefs.find(name);
                    if (sit != kv.second->struct_typedefs.end()) {
                        return LogicVal::make_known(32, sit->second->total_width);
                    }
                    std::string qname = kv.first + "::" + name;
                    const VariableDimMetadata* qmeta = Parser::find_type_dim_metadata(qname);
                    if (qmeta && !qmeta->dimensions.empty()) {
                        uint64_t total = 1;
                        for (const auto& d : qmeta->dimensions) total *= (uint64_t)d.size();
                        if (total > 0) return LogicVal::make_known(32, total);
                    }
                }
            } else if (auto sma = dynamic_cast<StaticMemberAccessNode*>(arg)) {
                std::string qname = sma->class_type_name + "::" + sma->member_name;
                const VariableDimMetadata* tmeta = Parser::find_type_dim_metadata(qname);
                if (tmeta && !tmeta->dimensions.empty()) {
                    uint64_t total = 1;
                    for (const auto& d : tmeta->dimensions) total *= (uint64_t)d.size();
                    if (total > 0) return LogicVal::make_known(32, total);
                }
                auto pit = Parser::global_packages.find(sma->class_type_name);
                if (pit != Parser::global_packages.end()) {
                    auto sit = pit->second->struct_typedefs.find(sma->member_name);
                    if (sit != pit->second->struct_typedefs.end()) {
                        return LogicVal::make_known(32, sit->second->total_width);
                    }
                }
            } else if (auto cat = dynamic_cast<ConcatNode*>(arg)) {
                uint64_t total = 0;
                for (auto& elem : cat->elements) {
                    LogicVal el_val = eval(elem.get(), scope);
                    total += el_val.width;
                }
                return LogicVal::make_known(32, total);
            }
            LogicVal arg_val = eval(fn_call->args[0].get(), scope);
            return LogicVal::make_known(32, static_cast<uint64_t>(arg_val.width));
        }
        if (fn_call->func_name == "$fopen") {
            if (fn_call->args.empty()) return LogicVal::make_known(32, 0);
            LogicVal fn_val = eval(fn_call->args[0].get(), scope);
            std::string filename = fn_val.is_string ? fn_val.str_val : "";
            std::string mode = "w";
            if (fn_call->args.size() > 1) {
                LogicVal m_val = eval(fn_call->args[1].get(), scope);
                if (m_val.is_string) mode = m_val.str_val;
            }
            int fd = const_cast<Engine&>(engine).next_file_descriptor++;
            auto strm = std::make_shared<std::ofstream>();
            std::ios_base::openmode om = std::ios::out;
            if (mode.find('w') != std::string::npos) om |= std::ios::trunc;
            if (mode.find('a') != std::string::npos) om |= std::ios::app;
            if (mode.find('b') != std::string::npos) om |= std::ios::binary;
            strm->open(filename, om);
            const_cast<Engine&>(engine).open_files[fd] = {strm, filename};
            AsyncTraceBuffer::instance().register_fd(fd, strm);
            return LogicVal::make_known(32, static_cast<uint64_t>(fd));
        }
        if (fn_call->func_name == "$value$plusargs") {
            if (fn_call->args.size() < 2) return LogicVal::make_known(32, 0);
            LogicVal user_fmt_val = eval(fn_call->args[0].get(), scope);
            std::string user_fmt = user_fmt_val.is_string ? user_fmt_val.str_val : "";
            size_t eq_pos = user_fmt.find('=');
            std::string prefix = (eq_pos != std::string::npos) ? user_fmt.substr(0, eq_pos + 1) : user_fmt;
            std::string fmt_spec = (eq_pos != std::string::npos) ? user_fmt.substr(eq_pos + 1) : "";
            for (const auto& pa : engine.plusargs) {
                std::string stripped_pa = (pa.front() == '+') ? pa.substr(1) : pa;
                if (stripped_pa.rfind(prefix, 0) == 0) {
                    std::string val_str = stripped_pa.substr(prefix.size());
                    uint64_t num_val = 0;
                    if (fmt_spec.find('b') != std::string::npos) num_val = std::strtoull(val_str.c_str(), nullptr, 2);
                    else if (fmt_spec.find('h') != std::string::npos || fmt_spec.find('x') != std::string::npos) num_val = std::strtoull(val_str.c_str(), nullptr, 16);
                    else num_val = std::strtoull(val_str.c_str(), nullptr, 10);
                    if (auto id = dynamic_cast<IdentifierNode*>(fn_call->args[1].get())) {
                        size_t nid = engine.get_net_id(scope, id->name);
                        if (nid != (size_t)-1) {
                            const_cast<Engine&>(engine).netlist[nid].set(LogicVal::make_known(const_cast<Engine&>(engine).netlist[nid].width, num_val));
                        }
                    }
                    return LogicVal::make_known(32, 1);
                }
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

        // FAST PATH: bound net identifier
        if (ident->is_bound && ident->cached_net_id != UINT32_MAX && ident->cached_net_id < engine.netlist.size()) {
            bool is_shadowed = false;
            if (current_sva_local_vars && current_sva_local_vars->count(ident->name)) {
                is_shadowed = true;
            } else if (context.has_active_frame() && context.lookup_local(ident->name)) {
                is_shadowed = true;
            } else if (current_bp && !current_bp->call_stack.empty()) {
                for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                    if (it->local_frame.get_var(ident->name)) { is_shadowed = true; break; }
                }
            } else if (current_this && (current_this->has_virtual_interface(ident->name) || current_this->has_property(ident->name))) {
                is_shadowed = true;
            }
            if (!is_shadowed) {
                if (preponed_override && ident->cached_net_id < preponed_override->size()) {
                    LogicVal res = (*preponed_override)[ident->cached_net_id];
                    res.is_signed = engine.netlist[ident->cached_net_id].is_signed;
                    return res;
                }
                LogicVal res = engine.netlist[ident->cached_net_id].value;
                res.is_signed = engine.netlist[ident->cached_net_id].is_signed;
                return res;
            }
        }

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
                if (local->is_unpacked_array) {
                    res.is_unpacked = true;
                    res.unpacked_elements = local->unpacked_elements;
                } else if (local->is_dynamic_array) {
                    res.is_unpacked = true;
                    res.unpacked_elements = local->dyn_array_data.elements;
                } else if (local->is_queue) {
                    res.is_unpacked = true;
                    res.unpacked_elements.assign(local->queue_data.elements.begin(), local->queue_data.elements.end());
                }
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
                    if (local->is_unpacked_array) {
                        res.is_unpacked = true;
                        res.unpacked_elements = local->unpacked_elements;
                    } else if (local->is_dynamic_array) {
                        res.is_unpacked = true;
                        res.unpacked_elements = local->dyn_array_data.elements;
                    } else if (local->is_queue) {
                        res.is_unpacked = true;
                        res.unpacked_elements.assign(local->queue_data.elements.begin(), local->queue_data.elements.end());
                    }
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
        if (engine.param_val_map.find(scoped_ident) != engine.param_val_map.end()) {
            return engine.param_val_map.at(scoped_ident);
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
        if (engine.param_val_map.find(ident->name) != engine.param_val_map.end()) {
            return engine.param_val_map.at(ident->name);
        }
        if (engine.param_map.find(ident->name) != engine.param_map.end()) {
            return LogicVal::make_known(32, engine.param_map.at(ident->name));
        }
        {
            std::string cur_s = scope;
            while (!cur_s.empty()) {
                std::string s_id = cur_s + "." + ident->name;
                if (engine.param_val_map.count(s_id)) {
                    return engine.param_val_map.at(s_id);
                }
                if (engine.param_map.count(s_id)) {
                    return LogicVal::make_known(32, engine.param_map.at(s_id));
                }
                auto d = cur_s.rfind('.');
                if (d == std::string::npos) break;
                cur_s = cur_s.substr(0, d);
            }
            for (const auto& pkg : engine.package_defs) {
                if (pkg.second->param_constants.count(ident->name)) {
                    return LogicVal::make_known(32, pkg.second->param_constants.at(ident->name));
                }
            }
        }
        auto dot_pos = ident->name.rfind('.');
        if (dot_pos != std::string::npos) {
            std::string base_var = ident->name.substr(0, dot_pos);
            std::string mem_name = ident->name.substr(dot_pos + 1);
            MemberAccessNode man(base_var, mem_name);
            return eval(&man, scope);
        }
        DynamicArrayObject* da = engine.get_dynamic_array(ident->name, scope);
        if (da) {
            LogicVal res = LogicVal::make_known(32, (uint64_t)da->elements.size());
            res.is_unpacked = true;
            res.unpacked_elements = da->elements;
            return res;
        }
        QueueObject* q = engine.get_queue(ident->name, scope);
        if (q) {
            LogicVal res = LogicVal::make_known(32, (uint64_t)q->elements.size());
            res.is_unpacked = true;
            res.unpacked_elements.assign(q->elements.begin(), q->elements.end());
            return res;
        }
        std::string mem_key = "";
        if (engine.memory_map.find(scoped_ident) != engine.memory_map.end()) mem_key = scoped_ident;
        else if (engine.memory_map.find(ident->name) != engine.memory_map.end()) mem_key = ident->name;
        if (!mem_key.empty()) {
            auto& mem = engine.memory_map.at(mem_key);
            LogicVal res = LogicVal::make_known(32, (uint64_t)mem.depth);
            res.is_unpacked = true;
            res.unpacked_elements.reserve(mem.depth);
            for (size_t i = 0; i < mem.depth; ++i) {
                res.unpacked_elements.push_back(mem.get_word(i));
            }
            return res;
        }
        return LogicVal::make_x(1);
    }

    if (auto rep = dynamic_cast<ReplicateNode*>(expr)) {
        LogicVal count_val = eval(rep->count_expr.get(), scope);
        int64_t scount = (int64_t)count_val.aval;
        if (count_val.is_signed && count_val.width < 64) {
            if (count_val.aval & (1ULL << (count_val.width - 1))) {
                scount |= (~0ULL << count_val.width);
            }
        } else if (count_val.width <= 32 && (count_val.aval & 0x80000000ULL)) {
            scount = (int32_t)count_val.aval;
        } else if (count_val.width == 33 && (count_val.aval & 0x100000000ULL)) {
            scount = (int64_t)(count_val.aval | (~0ULL << 33));
        }
        if (count_val.is_unknown() || scount <= 0) return LogicVal(1, 0, 0);
        uint64_t count = (uint64_t)scount;
        if (count > 65536) return LogicVal(1, 0, 0);

        LogicVal t_val = eval(rep->target_expr.get(), scope);

        if (count == 0 || t_val.width == 0) return LogicVal(1, 0, 0);
        uint64_t total_w_64 = (uint64_t)t_val.width * count;
        if (total_w_64 > 65536) return LogicVal(1, 0, 0);
        uint32_t total_w = (uint32_t)total_w_64;
        if (total_w <= 64) {
            uint64_t new_a = 0;
            uint64_t new_b = 0;
            for (uint64_t i = 0; i < count; ++i) {
                new_a = (new_a << t_val.width) | t_val.aval;
                new_b = (new_b << t_val.width) | t_val.bval;
            }
            return LogicVal(total_w, new_a, new_b);
        }
        LogicVal res(total_w, 0, 0);
        for (uint64_t i = 0; i < count; ++i) {
            res.deposit_slice(i * t_val.width, t_val.width, t_val);
        }
        return res;
    }

    if (auto ucat = dynamic_cast<UnpackedConcatNode*>(expr)) {
        if (const PackedStructType* st = infer_struct_for_pattern(ucat, scope)) {
            return eval_struct_pattern(ucat, *st, scope);
        }
        auto arr_elems = eval_array_expr(ucat, scope);
        LogicVal res = LogicVal::make_known(32, (uint64_t)arr_elems.size());
        res.is_unpacked = true;
        res.unpacked_elements = std::move(arr_elems);
        return res;
    }

    if (auto pm = dynamic_cast<PatternMemberNode*>(expr)) {
        return eval(pm->value.get(), scope);
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

        bool any_unpacked = false;
        for (const auto& v : vals) {
            if (v.is_unpacked) {
                any_unpacked = true;
                break;
            }
        }
        if (any_unpacked) {
            auto arr_elems = eval_array_expr(cat, scope);
            LogicVal res = LogicVal::make_known(32, (uint64_t)arr_elems.size());
            res.is_unpacked = true;
            res.unpacked_elements = std::move(arr_elems);
            return res;
        }

        if (total_w <= 64) {
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
        LogicVal res(total_w, 0, 0);
        uint32_t cur_offset = 0;
        for (int i = (int)vals.size() - 1; i >= 0; --i) {
            const auto& v = vals[i];
            res.deposit_slice(cur_offset, v.width, v);
            cur_offset += v.width;
        }
        return res;
    }

    if (auto ucat = dynamic_cast<UnpackedConcatNode*>(expr)) {
        if (const PackedStructType* st = infer_struct_for_pattern(ucat, scope)) {
            return eval_struct_pattern(ucat, *st, scope);
        }
        auto arr_elems = eval_array_expr(ucat, scope);
        LogicVal res = LogicVal::make_known(32, (uint64_t)arr_elems.size());
        res.is_unpacked = true;
        res.unpacked_elements = std::move(arr_elems);
        return res;
    }

    if (auto pm = dynamic_cast<PatternMemberNode*>(expr)) {
        return eval(pm->value.get(), scope);
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
                uint64_t b_lsb = 0;
                uint32_t w = 1;
                if (idx_node->is_plus_colon) {
                    uint64_t width = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : 1;
                    b_lsb = b_msb;
                    b_msb = b_lsb + width - 1;
                    w = static_cast<uint32_t>(width);
                } else if (idx_node->is_minus_colon) {
                    uint64_t width = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : 1;
                    b_lsb = (b_msb >= width - 1) ? (b_msb - width + 1) : 0;
                    w = static_cast<uint32_t>(width);
                } else {
                    b_lsb = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : b_msb;
                    w = b_msb >= b_lsb ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                }
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
                uint64_t b_lsb = 0;
                uint32_t w = 1;
                if (idx_node->is_plus_colon) {
                    uint64_t width = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : 1;
                    b_lsb = b_msb;
                    b_msb = b_lsb + width - 1;
                    w = static_cast<uint32_t>(width);
                } else if (idx_node->is_minus_colon) {
                    uint64_t width = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : 1;
                    b_lsb = (b_msb >= width - 1) ? (b_msb - width + 1) : 0;
                    w = static_cast<uint32_t>(width);
                } else {
                    b_lsb = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : b_msb;
                    w = b_msb >= b_lsb ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                }
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
            LogicVal elem;
            if (assoc->is_string_key) {
                elem = assoc->get(addr_val.str_val);
            } else {
                uint64_t key = addr_val.aval;
                if (assoc->key_width < 64 && assoc->key_width > 0) {
                    key &= ((1ULL << assoc->key_width) - 1);
                }
                elem = assoc->get(key);
            }
            if (idx_node->has_bit_select && idx_node->bit_msb_expr) {
                uint64_t b_msb = eval(idx_node->bit_msb_expr.get(), scope).aval;
                uint64_t b_lsb = 0;
                uint32_t w = 1;
                if (idx_node->is_plus_colon) {
                    uint64_t width = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : 1;
                    b_lsb = b_msb;
                    w = static_cast<uint32_t>(width);
                } else if (idx_node->is_minus_colon) {
                    uint64_t width = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : 1;
                    b_lsb = (b_msb >= width - 1) ? (b_msb - width + 1) : 0;
                    w = static_cast<uint32_t>(width);
                } else {
                    b_lsb = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : b_msb;
                    w = b_msb >= b_lsb ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                    b_lsb = std::min(b_msb, b_lsb);
                }
                return elem.extract_slice((uint32_t)b_lsb, w);
            }
            return elem;
        }

        LogicVal target_val = LogicVal::make_x(1);
        size_t nid = (size_t)-1;
        bool found_struct_member = false;
        int64_t struct_member_lsb = 0;

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
                    uint64_t b_lsb = 0;
                    uint32_t w = 1;
                    if (idx_node->is_plus_colon) {
                        uint64_t width = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : 1;
                        b_lsb = b_msb;
                        b_msb = b_lsb + width - 1;
                        w = static_cast<uint32_t>(width);
                    } else if (idx_node->is_minus_colon) {
                        uint64_t width = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : 1;
                        b_lsb = (b_msb >= width - 1) ? (b_msb - width + 1) : 0;
                        w = static_cast<uint32_t>(width);
                    } else if (!idx_node->bit_lsb_expr && mem.dimensions.size() > mem.unpacked_dims_count + 1) {
                        uint32_t stride = 1;
                        for (size_t d = mem.unpacked_dims_count + 1; d < mem.dimensions.size(); ++d) {
                            stride *= (uint32_t)mem.dimensions[d].size();
                        }
                        int64_t low = (mem.dimensions.size() > mem.unpacked_dims_count) ? mem.dimensions[mem.unpacked_dims_count].low() : 0;
                        uint32_t elem_idx = (b_msb >= (uint64_t)low) ? (uint32_t)(b_msb - (uint64_t)low) : (uint32_t)b_msb;
                        b_lsb = elem_idx * stride;
                        w = stride;
                    } else {
                        b_lsb = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : b_msb;
                        w = b_msb >= b_lsb ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                        if (b_lsb >= (uint64_t)mem.packed_lsb) {
                            b_lsb -= (uint64_t)mem.packed_lsb;
                        }
                    }
                    if (w <= 64) {
                        LogicVal slice = elem.extract_slice((uint32_t)b_lsb, w);
                        return LogicVal::make_known(w, slice.aval);
                    }
                    return elem.extract_slice((uint32_t)b_lsb, w);
                }
                return elem;
            }

            nid = (size_t)-1;
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
            if (nid == (size_t)-1 && dot_p != std::string::npos) {
                std::string iname = idx_node->name;
                size_t dpos = iname.rfind('.');
                std::string memb = "";
                size_t base_id = (size_t)-1;
                while (dpos != std::string::npos) {
                    std::string base = iname.substr(0, dpos);
                    memb = iname.substr(dpos + 1) + (memb.empty() ? "" : ("." + memb));
                    base_id = engine.get_net_id(scope, base);
                    if (base_id != (size_t)-1) break;
                    dpos = iname.rfind('.', dpos - 1);
                }
                if (base_id != (size_t)-1 && !memb.empty()) {
                    LogicVal base_val = (preponed_override && base_id < preponed_override->size()) ?
                                        (*preponed_override)[base_id] : engine.netlist[base_id].value;
                    std::string type_name = engine.netlist[base_id].type_name;
                    std::string base_name = engine.netlist[base_id].name;
                    if (type_name.empty()) {
                        if (engine.var_to_struct_type.count(base_name)) {
                            type_name = engine.var_to_struct_type.at(base_name);
                        }
                    }
                    const PackedStructType* st = find_struct_type_by_member(type_name, memb, engine.netlist[base_id].width, scope);
                    if (!st && !type_name.empty()) {
                        std::string scoped_type = scope.empty() ? type_name : (scope + "." + type_name);
                        st = find_struct_type_by_member(scoped_type, memb, engine.netlist[base_id].width, scope);
                    }
                    if (st) {
                        const StructMember* sm = st->get_member(memb);
                        if (sm) {
                            target_val = base_val.extract_slice(sm->offset, sm->width);
                            found_struct_member = true;
                            struct_member_lsb = sm->lsb;
                        }
                    }
                } else if (base_id == (size_t)-1 && !memb.empty()) {
                    LocalVar* local_base = nullptr;
                    if (context.has_active_frame()) local_base = context.lookup_local(iname.substr(0, iname.rfind('.')));
                    if (!local_base && current_bp && !current_bp->call_stack.empty()) {
                        for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                            local_base = it->local_frame.get_var(iname.substr(0, iname.rfind('.')));
                            if (local_base) break;
                        }
                    }
                    if (local_base) {
                        LogicVal base_val = local_base->value;
                        const PackedStructType* st = find_struct_type_by_member(local_base->type_name, memb, base_val.width, scope);
                        if (st) {
                            const StructMember* sm = st->get_member(memb);
                            if (sm) {
                                target_val = base_val.extract_slice(sm->offset, sm->width);
                                found_struct_member = true;
                                struct_member_lsb = sm->lsb;
                            }
                        }
                    } else {
                        std::string base = iname.substr(0, iname.rfind('.'));
                        LogicVal base_val;
                        bool found_param = false;
                        std::string scoped_base = scope.empty() ? base : (scope + "." + base);
                        if (engine.param_val_map.count(scoped_base)) {
                            base_val = engine.param_val_map.at(scoped_base);
                            found_param = true;
                        } else if (engine.param_val_map.count(base)) {
                            base_val = engine.param_val_map.at(base);
                            found_param = true;
                        } else {
                            std::string cur_s = scope;
                            while (!cur_s.empty()) {
                                std::string cand = cur_s + "." + base;
                                if (engine.param_val_map.count(cand)) {
                                    base_val = engine.param_val_map.at(cand);
                                    found_param = true;
                                    break;
                                }
                                auto dot = cur_s.rfind('.');
                                if (dot == std::string::npos) cur_s = "";
                                else cur_s = cur_s.substr(0, dot);
                            }
                        }
                        if (found_param) {
                            std::string tname = engine.var_to_struct_type.count(scoped_base) ? engine.var_to_struct_type.at(scoped_base) :
                                               (engine.var_to_struct_type.count(base) ? engine.var_to_struct_type.at(base) : "");
                            const PackedStructType* st = find_struct_type_by_member(tname, memb, base_val.width, scope);
                            if (st) {
                                const StructMember* sm = st->get_member(memb);
                                if (sm) {
                                    target_val = base_val.extract_slice(sm->offset, sm->width);
                                    found_struct_member = true;
                                    struct_member_lsb = sm->lsb;
                                }
                            }
                        }
                    }
                }

            }
            if (nid != (size_t)-1) {
                if (preponed_override && nid < preponed_override->size()) {
                    target_val = (*preponed_override)[nid];
                } else {
                    target_val = engine.netlist[nid].value;
                }
            } else if (found_struct_member) {
                // target_val already extracted above
            } else if (engine.param_val_map.count(idx_node->name)) {
                target_val = engine.param_val_map.at(idx_node->name);
            } else if (!scope.empty() && engine.param_val_map.count(scope + "." + idx_node->name)) {
                target_val = engine.param_val_map.at(scope + "." + idx_node->name);
            } else if (engine.param_map.count(idx_node->name)) {
                target_val = LogicVal::make_known(64, engine.param_map.at(idx_node->name));
            } else if (!scope.empty() && engine.param_map.count(scope + "." + idx_node->name)) {
                target_val = LogicVal::make_known(64, engine.param_map.at(scope + "." + idx_node->name));
            } else {
                auto colon = idx_node->name.find("::");
                if (colon != std::string::npos) {
                    std::string pkg = idx_node->name.substr(0, colon);
                    std::string item = idx_node->name.substr(colon + 2);
                    if (engine.package_defs.count(pkg) && engine.package_defs.at(pkg)->param_constants.count(item)) {
                        target_val = LogicVal::make_known(64, engine.package_defs.at(pkg)->param_constants.at(item));
                    } else {
                        return LogicVal::make_x(1);
                    }
                } else {
                    return LogicVal::make_x(1);
                }
            }
        }

        const VariableDimMetadata* vdm = nullptr;
        if (nid != (size_t)-1 && engine.var_dimensions.count(engine.netlist[nid].name)) {
            vdm = &engine.var_dimensions.at(engine.netlist[nid].name);
        } else if (engine.var_dimensions.count(idx_node->name)) {
            vdm = &engine.var_dimensions.at(idx_node->name);
        } else if (!scope.empty() && engine.var_dimensions.count(scope + "." + idx_node->name)) {
            vdm = &engine.var_dimensions.at(scope + "." + idx_node->name);
        }
        if (!vdm) {
            std::string cur_s = scope;
            while (!cur_s.empty()) {
                auto dot = cur_s.rfind('.');
                if (dot == std::string::npos) cur_s = "";
                else cur_s = cur_s.substr(0, dot);
                std::string cand = cur_s.empty() ? idx_node->name : (cur_s + "." + idx_node->name);
                if (engine.var_dimensions.count(cand)) {
                    vdm = &engine.var_dimensions.at(cand);
                    break;
                }
            }
        }
        int64_t base_low = found_struct_member ? struct_member_lsb : 0;
        uint32_t elem_w = 1;
        if (vdm && !vdm->dimensions.empty()) {
            base_low = vdm->dimensions[0].low();
            if (vdm->dimensions.size() > 1) {
                uint32_t stride0 = 1;
                for (size_t k = 1; k < vdm->dimensions.size(); ++k) {
                    stride0 *= (uint32_t)vdm->dimensions[k].size();
                }
                elem_w = stride0;
            } else {
                std::string tname = "";
                size_t tnid = engine.get_net_id(scope, idx_node->name);
                if (tnid != (size_t)-1) tname = engine.netlist[tnid].type_name;
                if (tname.empty() && engine.var_to_struct_type.count(idx_node->name)) {
                    tname = engine.var_to_struct_type.at(idx_node->name);
                }
                if (!tname.empty()) {
                    const PackedStructType* st = find_struct_type(tname, scope);
                    if (st && st->total_width > 0) {
                        elem_w = st->total_width;
                    }
                }
                if (elem_w == 1 && nid != (size_t)-1 && !vdm->dimensions.empty()) {
                    uint32_t total_w = engine.netlist[nid].width;
                    uint32_t d0_sz = (uint32_t)vdm->dimensions[0].size();
                    if (d0_sz > 0 && total_w > d0_sz && (total_w % d0_sz == 0)) {
                        elem_w = total_w / d0_sz;
                    }
                }
            }
        }

        if ((!idx_node->extra_indices.empty() || idx_node->has_bit_select) && vdm && vdm->dimensions.size() > 1) {
            std::vector<ExpressionNode*> all_indices;
            if (idx_node->msb_expr) all_indices.push_back(idx_node->msb_expr.get());
            if (idx_node->has_bit_select && idx_node->bit_msb_expr) all_indices.push_back(idx_node->bit_msb_expr.get());
            for (const auto& ei : idx_node->extra_indices) {
                if (ei) all_indices.push_back(ei.get());
            }
            size_t M = vdm->dimensions.size();
            size_t N = all_indices.size();
            std::vector<uint32_t> strides(M, 1);
            for (int k = (int)M - 2; k >= 0; --k) {
                strides[k] = strides[k + 1] * (uint32_t)vdm->dimensions[k + 1].size();
            }
            uint32_t total_off = 0;
            uint32_t part_w = 0;
            for (size_t d = 0; d < N && d < M; ++d) {
                if (d == 1 && idx_node->has_bit_select) {
                    if (idx_node->is_plus_colon || idx_node->is_minus_colon || idx_node->bit_lsb_expr) {
                        uint64_t b_msb = eval(idx_node->bit_msb_expr.get(), scope).aval;
                        uint64_t b_lsb = 0;
                        uint32_t w = 1;
                        if (idx_node->is_plus_colon) {
                            uint64_t width = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : 1;
                            b_lsb = b_msb;
                            w = static_cast<uint32_t>(width);
                        } else if (idx_node->is_minus_colon) {
                            uint64_t width = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : 1;
                            b_lsb = (b_msb >= width - 1) ? (b_msb - width + 1) : 0;
                            w = static_cast<uint32_t>(width);
                        } else {
                            b_lsb = eval(idx_node->bit_lsb_expr.get(), scope).aval;
                            w = (b_msb >= b_lsb) ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                            b_lsb = std::min(b_msb, b_lsb);
                        }
                        int64_t low = vdm->dimensions[d].low();
                        if (b_lsb >= (uint64_t)low) b_lsb -= (uint64_t)low;
                        total_off += (uint32_t)b_lsb * strides[d];
                        part_w = w;
                        continue;
                    }
                }
                if (auto part = dynamic_cast<IndexSelectNode*>(all_indices[d])) {
                    if (part->is_part_select) {
                        uint64_t msb = eval(part->msb_expr.get(), scope).aval;
                        uint64_t lsb = eval(part->lsb_expr.get(), scope).aval;
                        uint32_t w = 1;
                        uint32_t eff_lsb = 0;
                        if (part->is_plus_colon) {
                            w = (uint32_t)lsb;
                            eff_lsb = (uint32_t)msb;
                        } else if (part->is_minus_colon) {
                            w = (uint32_t)lsb;
                            eff_lsb = (msb >= w - 1) ? (uint32_t)(msb - w + 1) : 0;
                        } else {
                            w = (msb >= lsb) ? (uint32_t)(msb - lsb + 1) : (uint32_t)(lsb - msb + 1);
                            eff_lsb = std::min(msb, lsb);
                        }
                        int64_t low = vdm->dimensions[d].low();
                        if (eff_lsb >= (uint64_t)low) eff_lsb -= (uint64_t)low;
                        total_off += eff_lsb * strides[d];
                        part_w = w;
                        continue;
                    }
                }
                LogicVal iv = eval(all_indices[d], scope);
                if (iv.is_unknown()) return LogicVal::make_x(1);
                int64_t val = iv.is_signed ? iv.to_signed() : (int64_t)iv.aval;
                int64_t low = vdm->dimensions[d].low();
                if (val >= low) val -= low;
                total_off += (uint32_t)val * strides[d];
            }
            uint32_t out_w = part_w > 0 ? part_w : ((N <= M) ? (N == M ? 1 : strides[N - 1]) : 1);
            return target_val.extract_slice(total_off, out_w);
        }

        if (!idx_node->is_part_select) {
            LogicVal idx_val = eval(idx_node->msb_expr.get(), scope);
            if (idx_val.is_unknown()) return LogicVal::make_x(elem_w);
            int64_t raw_idx = idx_val.is_signed ? idx_val.to_signed() : (int64_t)idx_val.aval;
            if (base_low > 0 && raw_idx >= base_low) raw_idx -= base_low;
            if (raw_idx < 0 || (raw_idx * elem_w) >= target_val.width) return LogicVal::make_x(elem_w);
            uint32_t sh = (uint32_t)(raw_idx * elem_w);
            LogicVal elem = target_val.extract_slice(sh, elem_w);
            if (idx_node->has_bit_select && idx_node->bit_msb_expr) {
                uint64_t b_msb = eval(idx_node->bit_msb_expr.get(), scope).aval;
                uint64_t b_lsb = 0;
                uint32_t w = 1;
                if (idx_node->is_plus_colon) {
                    uint64_t width = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : 1;
                    b_lsb = b_msb;
                    b_msb = b_lsb + width - 1;
                    w = static_cast<uint32_t>(width);
                } else if (idx_node->is_minus_colon) {
                    uint64_t width = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : 1;
                    b_lsb = (b_msb >= width - 1) ? (b_msb - width + 1) : 0;
                    w = static_cast<uint32_t>(width);
                } else {
                    b_lsb = idx_node->bit_lsb_expr ? eval(idx_node->bit_lsb_expr.get(), scope).aval : b_msb;
                    w = b_msb >= b_lsb ? (uint32_t)(b_msb - b_lsb + 1) : (uint32_t)(b_lsb - b_msb + 1);
                }
                return elem.extract_slice((uint32_t)b_lsb, w);
            }
            return elem;
        } else {
            LogicVal base_v = eval(idx_node->msb_expr.get(), scope);
            LogicVal w_v = eval(idx_node->lsb_expr.get(), scope);
            if (base_v.is_unknown() || w_v.is_unknown()) {
                uint32_t w = w_v.is_unknown() ? 1 : w_v.aval;
                return LogicVal::make_x(w > 0 ? w : 1);
            }
            uint32_t lsb = 0, msb = 0, slice_w = 0;
            if (idx_node->is_plus_colon) {
                lsb = base_v.aval;
                slice_w = w_v.aval;
                msb = lsb + slice_w - 1;
                if (base_low > 0 && lsb >= (uint32_t)base_low) lsb -= (uint32_t)base_low;
            } else if (idx_node->is_minus_colon) {
                slice_w = w_v.aval;
                msb = base_v.aval;
                lsb = (msb >= slice_w - 1) ? (msb - (slice_w - 1)) : 0;
                if (base_low > 0 && lsb >= (uint32_t)base_low) lsb -= (uint32_t)base_low;
            } else {
                msb = base_v.aval;
                lsb = w_v.aval;
                if (msb < lsb) std::swap(msb, lsb);
                slice_w = msb - lsb + 1;
                if (base_low > 0 && lsb >= (uint32_t)base_low) lsb -= (uint32_t)base_low;
            }
            return target_val.extract_slice(lsb, slice_w);
        }
    }

    if (auto un = dynamic_cast<UnaryOpNode*>(expr)) {
        LogicVal val = eval(un->operand.get(), scope);

        // 2-State fast path (<= 64-bit, known)
        if (val.is_clean_2state_64()) {
            uint64_t a = val.get_raw_u64();
            uint64_t mask = (val.width >= 64) ? ~0ULL : ((1ULL << val.width) - 1);
            if (un->op == "!") {
                return LogicVal::from_u64_2state((a == 0) ? 1 : 0, 1);
            }
            if (un->op == "~") {
                return LogicVal::from_u64_2state((~a) & mask, val.width);
            }
            if (un->op == "+") {
                return val;
            }
            if (un->op == "-") {
                uint64_t neg_a = (~a + 1) & mask;
                return LogicVal::from_u64_2state(neg_a, val.width, true);
            }
            if (un->op == "&") {
                return LogicVal::from_u64_2state(((a & mask) == mask) ? 1 : 0, 1);
            }
            if (un->op == "~&") {
                return LogicVal::from_u64_2state(((a & mask) == mask) ? 0 : 1, 1);
            }
            if (un->op == "|") {
                return LogicVal::from_u64_2state((a != 0) ? 1 : 0, 1);
            }
            if (un->op == "~|") {
                return LogicVal::from_u64_2state((a == 0) ? 1 : 0, 1);
            }
            if (un->op == "^") {
                return LogicVal::from_u64_2state(__builtin_parityll(a & mask), 1);
            }
            if (un->op == "~^" || un->op == "^~") {
                return LogicVal::from_u64_2state(!__builtin_parityll(a & mask), 1);
            }
        }

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
        TypeOpNode* l_top = dynamic_cast<TypeOpNode*>(bin->lhs.get());
        TypeOpNode* r_top = dynamic_cast<TypeOpNode*>(bin->rhs.get());
        if (l_top || r_top) {
            if (!l_top || !r_top) {
                throw std::runtime_error("[CAS-DTINPCIL] Both operands of type comparison must be type() references per IEEE 1800-2023 section 6.23");
            }
            if (bin->op != "==" && bin->op != "!=" && bin->op != "===" && bin->op != "!==") {
                throw std::runtime_error("[CAS-DTINPCIL] Relational operator '" + bin->op + "' is invalid between type() references per IEEE 1800-2023 section 6.23");
            }
            std::string l_type = resolve_canonical_type(l_top->target_expr.get(), l_top->type_name_arg, scope, "type()");
            std::string r_type = resolve_canonical_type(r_top->target_expr.get(), r_top->type_name_arg, scope, "type()");
            bool eq = (l_type == r_type);
            if (bin->op == "==" || bin->op == "===") {
                return LogicVal::make_known(1, eq ? 1 : 0);
            } else {
                return LogicVal::make_known(1, eq ? 0 : 1);
            }
        }

        LogicVal l = eval(bin->lhs.get(), scope);
        LogicVal r = eval(bin->rhs.get(), scope);

        bool l_signed = l.is_signed;
        if (auto id = dynamic_cast<IdentifierNode*>(bin->lhs.get())) {
            if (id->is_bound && id->cached_net_id < engine.netlist.size()) {
                l_signed = engine.netlist[id->cached_net_id].is_signed;
            } else {
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
        } else if (auto num = dynamic_cast<NumberNode*>(bin->lhs.get())) {
            if (num->is_signed) l_signed = true;
        }
        bool r_signed = r.is_signed;
        if (auto id = dynamic_cast<IdentifierNode*>(bin->rhs.get())) {
            if (id->is_bound && id->cached_net_id < engine.netlist.size()) {
                r_signed = engine.netlist[id->cached_net_id].is_signed;
            } else {
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
        } else if (auto num = dynamic_cast<NumberNode*>(bin->rhs.get())) {
            if (num->is_signed) r_signed = true;
        }

        bool expr_signed = l_signed && r_signed;

        // 2-State fast path (<= 64-bit, known)
        if (l.is_clean_2state_64() && r.is_clean_2state_64()) {
            uint64_t a = l.get_raw_u64();
            uint64_t b = r.get_raw_u64();

            if (bin->op == "+") {
                uint32_t w = std::min(64U, std::max(l.width, r.width) + 1);
                uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
                if (expr_signed) {
                    int64_t sa = l.to_signed();
                    int64_t sb = r.to_signed();
                    return LogicVal::from_u64_2state(static_cast<uint64_t>(sa + sb) & mask, w, true);
                }
                return LogicVal::from_u64_2state((a + b) & mask, w, false);
            }
            if (bin->op == "-") {
                uint32_t w = std::min(64U, std::max(l.width, r.width) + 1);
                uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
                if (expr_signed) {
                    int64_t sa = l.to_signed();
                    int64_t sb = r.to_signed();
                    return LogicVal::from_u64_2state(static_cast<uint64_t>(sa - sb) & mask, w, true);
                }
                return LogicVal::from_u64_2state((a - b) & mask, w, false);
            }
            if (bin->op == "*") {
                uint32_t w = std::min(64U, l.width + r.width);
                uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
                if (expr_signed) {
                    int64_t sa = l.to_signed();
                    int64_t sb = r.to_signed();
                    return LogicVal::from_u64_2state(static_cast<uint64_t>(sa * sb) & mask, w, true);
                }
                return LogicVal::from_u64_2state((a * b) & mask, w, false);
            }
            if (bin->op == "/") {
                uint32_t w = l.width;
                if (b == 0) return LogicVal::make_x(w);
                if (expr_signed) {
                    int64_t sa = l.to_signed();
                    int64_t sb = r.to_signed();
                    if (sb == 0) return LogicVal::make_x(w);
                    if (sa == INT64_MIN && sb == -1) return LogicVal::from_u64_2state(static_cast<uint64_t>(sa), w, true);
                    return LogicVal::from_u64_2state(static_cast<uint64_t>(sa / sb), w, true);
                }
                return LogicVal::from_u64_2state(a / b, w, false);
            }
            if (bin->op == "%") {
                uint32_t w = l.width;
                if (b == 0) return LogicVal::make_x(w);
                if (expr_signed) {
                    int64_t sa = l.to_signed();
                    int64_t sb = r.to_signed();
                    if (sb == 0) return LogicVal::make_x(w);
                    if (sa == INT64_MIN && sb == -1) return LogicVal::from_u64_2state(0, w, true);
                    return LogicVal::from_u64_2state(static_cast<uint64_t>(sa % sb), w, true);
                }
                return LogicVal::from_u64_2state(a % b, w, false);
            }
            if (bin->op == "**") {
                uint32_t w = l.width;
                uint64_t base = a, exp = b, res = 1;
                while (exp > 0) {
                    if (exp & 1) res *= base;
                    base *= base;
                    exp >>= 1;
                }
                return LogicVal::from_u64_2state(res, w, expr_signed);
            }
            if (bin->op == "&") {
                uint32_t max_w = std::max(l.width, r.width);
                return LogicVal::from_u64_2state(a & b, max_w);
            }
            if (bin->op == "|") {
                uint32_t max_w = std::max(l.width, r.width);
                return LogicVal::from_u64_2state(a | b, max_w);
            }
            if (bin->op == "^") {
                uint32_t max_w = std::max(l.width, r.width);
                return LogicVal::from_u64_2state(a ^ b, max_w);
            }
            if (bin->op == "<<") {
                uint64_t res = (b >= l.width) ? 0ULL : (a << b);
                return LogicVal::from_u64_2state(res, l.width);
            }
            if (bin->op == ">>") {
                uint64_t res = (b >= l.width) ? 0ULL : (a >> b);
                return LogicVal::from_u64_2state(res, l.width);
            }
            if (bin->op == ">>>") {
                if (!l_signed) {
                    uint64_t res = (b >= l.width) ? 0ULL : (a >> b);
                    return LogicVal::from_u64_2state(res, l.width);
                }
                if (l.width == 0) return l;
                bool sign_bit = (a >> (l.width - 1)) & 1ULL;
                if (b >= l.width) {
                    uint64_t res = sign_bit ? ((l.width >= 64) ? ~0ULL : ((1ULL << l.width) - 1)) : 0ULL;
                    return LogicVal::from_u64_2state(res, l.width, true);
                }
                uint64_t res = a >> b;
                if (sign_bit) {
                    uint64_t fill_mask = (~0ULL) << (l.width - b);
                    uint64_t width_mask = (l.width >= 64) ? ~0ULL : ((1ULL << l.width) - 1);
                    res |= (fill_mask & width_mask);
                }
                return LogicVal::from_u64_2state(res, l.width, true);
            }
            if (bin->op == "==" || bin->op == "===") {
                if (l.width == r.width) {
                    return LogicVal::from_u64_2state((a == b) ? 1 : 0, 1);
                }
                if (expr_signed) {
                    return LogicVal::from_u64_2state((l.to_signed() == r.to_signed()) ? 1 : 0, 1);
                }
                return LogicVal::from_u64_2state((a == b) ? 1 : 0, 1);
            }
            if (bin->op == "!=" || bin->op == "!==") {
                if (l.width == r.width) {
                    return LogicVal::from_u64_2state((a != b) ? 1 : 0, 1);
                }
                if (expr_signed) {
                    return LogicVal::from_u64_2state((l.to_signed() != r.to_signed()) ? 1 : 0, 1);
                }
                return LogicVal::from_u64_2state((a != b) ? 1 : 0, 1);
            }
            if (bin->op == "<") {
                if (expr_signed) {
                    return LogicVal::from_u64_2state((l.to_signed() < r.to_signed()) ? 1 : 0, 1);
                }
                return LogicVal::from_u64_2state((a < b) ? 1 : 0, 1);
            }
            if (bin->op == "<=") {
                if (expr_signed) {
                    return LogicVal::from_u64_2state((l.to_signed() <= r.to_signed()) ? 1 : 0, 1);
                }
                return LogicVal::from_u64_2state((a <= b) ? 1 : 0, 1);
            }
            if (bin->op == ">") {
                if (expr_signed) {
                    return LogicVal::from_u64_2state((l.to_signed() > r.to_signed()) ? 1 : 0, 1);
                }
                return LogicVal::from_u64_2state((a > b) ? 1 : 0, 1);
            }
            if (bin->op == ">=") {
                if (expr_signed) {
                    return LogicVal::from_u64_2state((l.to_signed() >= r.to_signed()) ? 1 : 0, 1);
                }
                return LogicVal::from_u64_2state((a >= b) ? 1 : 0, 1);
            }
            if (bin->op == "&&") {
                return LogicVal::from_u64_2state((a != 0 && b != 0) ? 1 : 0, 1);
            }
            if (bin->op == "||") {
                return LogicVal::from_u64_2state((a != 0 || b != 0) ? 1 : 0, 1);
            }
        }

        if (bin->op == "+") {
            uint32_t w = std::min(64U, std::max(l.width, r.width) + 1);
            LogicVal ext_l = LogicOps::cast_width(l, w, expr_signed);
            LogicVal ext_r = LogicOps::cast_width(r, w, expr_signed);
            if (ext_l.is_unknown() || ext_r.is_unknown()) return LogicVal::make_x(w);
            return LogicVal::make_known(w, ext_l.aval + ext_r.aval, expr_signed);
        }
        if (bin->op == "-") {
            uint32_t w = std::min(64U, std::max(l.width, r.width) + 1);
            LogicVal ext_l = LogicOps::cast_width(l, w, expr_signed);
            LogicVal ext_r = LogicOps::cast_width(r, w, expr_signed);
            if (ext_l.is_unknown() || ext_r.is_unknown()) return LogicVal::make_x(w);
            return LogicVal::make_known(w, ext_l.aval - ext_r.aval, expr_signed);
        }
        if (bin->op == "*") {
            uint32_t w = std::min(64U, l.width + r.width);
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(w);
            return LogicVal::make_known(w, l.aval * r.aval, expr_signed);
        }
        if (bin->op == "/") {
            uint32_t w = l.width;
            if (l.is_unknown() || r.is_unknown() || r.aval == 0) return LogicVal::make_x(w);
            if (expr_signed) {
                int64_t sl = (int64_t)LogicOps::cast_width(l, 64, true).aval;
                int64_t sr = (int64_t)LogicOps::cast_width(r, 64, true).aval;
                if (sr == 0) return LogicVal::make_x(w);
                return LogicVal::make_known(w, static_cast<uint64_t>(sl / sr), true);
            }
            return LogicVal::make_known(w, l.aval / r.aval, false);
        }
        if (bin->op == "%") {
            uint32_t w = l.width;
            if (l.is_unknown() || r.is_unknown() || r.aval == 0) return LogicVal::make_x(w);
            if (expr_signed) {
                int64_t sl = (int64_t)LogicOps::cast_width(l, 64, true).aval;
                int64_t sr = (int64_t)LogicOps::cast_width(r, 64, true).aval;
                if (sr == 0) return LogicVal::make_x(w);
                return LogicVal::make_known(w, static_cast<uint64_t>(sl % sr), true);
            }
            return LogicVal::make_known(w, l.aval % r.aval, false);
        }
        if (bin->op == "**") {
            uint32_t w = l.width;
            if (l.is_unknown() || r.is_unknown()) return LogicVal::make_x(w);
            uint64_t base = l.aval, exp = r.aval, res = 1;
            while (exp > 0) {
                if (exp & 1) res *= base;
                base *= base;
                exp >>= 1;
            }
            return LogicVal::make_known(w, res, expr_signed);
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
        if (cond.is_clean_2state_64()) {
            return (cond.get_raw_u64() != 0) ? eval(tern->true_expr.get(), scope) : eval(tern->false_expr.get(), scope);
        }
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
            if (loc->is_unpacked_array) {
                out_elements = loc->unpacked_elements;
                out_elem_width = loc->elem_width;
                out_is_signed = loc->is_signed;
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
                if (loc->is_unpacked_array) {
                    out_elements = loc->unpacked_elements;
                    out_elem_width = loc->elem_width;
                    out_is_signed = loc->is_signed;
                    return true;
                }
            }
        }
    }

    // 2.5 Check current_this dynamic arrays
    if (current_this) {
        auto it = current_this->dynamic_arrays.find(var_name);
        if (it != current_this->dynamic_arrays.end()) {
            out_elements = it->second.elements;
            out_elem_width = it->second.elem_width;
            out_is_signed = it->second.is_signed;
            return true;
        }
    }

    // 3. Check engine dynamic array
    DynamicArrayObject* da = engine.get_dynamic_array(var_name, scope);
    if (!da) {
        std::string cur_s = scope;
        while (!cur_s.empty()) {
            auto pos = cur_s.rfind('.');
            if (pos != std::string::npos) {
                cur_s = cur_s.substr(0, pos);
                da = engine.get_dynamic_array(var_name, cur_s);
                if (da) break;
            } else {
                break;
            }
        }
    }
    if (da) {
        out_elements = da->elements;
        out_elem_width = da->elem_width;
        out_is_signed = da->is_signed;
        return true;
    }

    // 4. Check engine queue
    QueueObject* q = engine.get_queue(var_name, scope);
    if (!q) {
        std::string cur_s = scope;
        while (!cur_s.empty()) {
            auto pos = cur_s.rfind('.');
            if (pos != std::string::npos) {
                cur_s = cur_s.substr(0, pos);
                q = engine.get_queue(var_name, cur_s);
                if (q) break;
            } else {
                break;
            }
        }
    }
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
    else {
        std::string cur_s = scope;
        while (!cur_s.empty()) {
            auto pos = cur_s.rfind('.');
            if (pos != std::string::npos) {
                cur_s = cur_s.substr(0, pos);
                if (engine.memory_map.find(cur_s + "." + var_name) != engine.memory_map.end()) {
                    mem_key = cur_s + "." + var_name;
                    break;
                }
            } else {
                break;
            }
        }
    }
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
    if (!assoc) {
        std::string cur_s = scope;
        while (!cur_s.empty()) {
            auto pos = cur_s.rfind('.');
            if (pos != std::string::npos) {
                cur_s = cur_s.substr(0, pos);
                assoc = engine.get_assoc_array(var_name, cur_s);
                if (assoc) break;
            } else {
                break;
            }
        }
    }
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
        if (method == "product") {
            return LogicVal::make_known(elem_width, 1, is_signed);
        }
        return LogicVal::make_known(elem_width, 0, is_signed);
    }

    LogicVal acc;
    bool has_acc = false;

    for (const auto& elem : elements) {
        LogicVal val = elem;
        if (with_expr) {
            context.push_frame("with_frame");
            context.declare_local(iter_name, elem_width, is_signed);
            context.set_local_value(iter_name, elem);
            val = eval(with_expr, scope);
            context.pop_frame();
        }

        if (!has_acc) {
            acc = val;
            has_acc = true;
            continue;
        }

        uint32_t res_width = val.width;
        bool res_signed = val.is_signed;
        uint64_t mask = (res_width >= 64) ? ~0ULL : ((1ULL << res_width) - 1);

        if (method == "sum") {
            if (acc.is_unknown() || val.is_unknown()) {
                acc = LogicVal::make_x(res_width);
            } else {
                uint64_t total = (acc.aval + val.aval) & mask;
                acc = LogicVal::make_known(res_width, total, res_signed);
            }
        } else if (method == "product") {
            if (acc.is_unknown() || val.is_unknown()) {
                acc = LogicVal::make_x(res_width);
            } else {
                uint64_t prod = (acc.aval * val.aval) & mask;
                acc = LogicVal::make_known(res_width, prod, res_signed);
            }
        } else if (method == "and") {
            acc = LogicOps::bitwise_and(acc, val);
            acc.width = res_width;
            acc.mask();
        } else if (method == "or") {
            acc = LogicOps::bitwise_or(acc, val);
            acc.width = res_width;
            acc.mask();
        } else if (method == "xor") {
            acc = LogicOps::bitwise_xor(acc, val);
            acc.width = res_width;
            acc.mask();
        }
    }

    return acc;
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

const PackedStructType* ExprEvaluator::find_struct_type(const std::string& type_name, const std::string& scope) const {
    if (type_name.empty()) return nullptr;

    // 1. Check scoped name
    if (!scope.empty()) {
        std::string cur_s = scope;
        while (true) {
            std::string scoped = cur_s.empty() ? type_name : (cur_s + "." + type_name);
            auto it = engine.struct_types.find(scoped);
            if (it != engine.struct_types.end()) return &it->second;
            if (cur_s.empty()) break;
            auto dot = cur_s.rfind('.');
            if (dot == std::string::npos) cur_s = "";
            else cur_s = cur_s.substr(0, dot);
        }
    }

    // 2. Check direct type name
    auto it = engine.struct_types.find(type_name);
    if (it != engine.struct_types.end()) return &it->second;

    // 3. Suffix match (e.g. "top.dut...ds_rsp_t" matches "ds_rsp_t")
    for (const auto& pair : engine.struct_types) {
        if (pair.first.length() > type_name.length()) {
            if (pair.first.rfind("." + type_name) == pair.first.length() - (type_name.length() + 1) ||
                pair.first.rfind("::" + type_name) == pair.first.length() - (type_name.length() + 2)) {
                return &pair.second;
            }
        }
    }

    return nullptr;
}

const PackedStructType* ExprEvaluator::find_struct_type_by_member(
    const std::string& type_name,
    const std::string& member_name,
    uint32_t width,
    const std::string& scope
) const {
    const PackedStructType* st = find_struct_type(type_name, scope);
    if (st && (!member_name.empty() ? (st->get_member(member_name) != nullptr) : true)) {
        return st;
    }
    if (!member_name.empty()) {
        for (const auto& pair : engine.struct_types) {
            if ((width == 0 || pair.second.total_width == width) && pair.second.get_member(member_name)) {
                return &pair.second;
            }
        }
    }
    return st;
}

const PackedStructType* ExprEvaluator::infer_struct_for_pattern(UnpackedConcatNode* ucat, const std::string& scope) const {
    if (!ucat) return nullptr;

    std::vector<std::string> named_members;
    for (const auto& elem : ucat->elements) {
        if (auto pm = dynamic_cast<PatternMemberNode*>(elem.get())) {
            if (!pm->member_name.empty() && pm->member_name != "default") {
                named_members.push_back(pm->member_name);
            }
        }
    }

    if (named_members.empty()) return nullptr;

    auto matches_all = [&](const PackedStructType& st) -> bool {
        for (const auto& mname : named_members) {
            if (!st.get_member(mname)) return false;
        }
        return true;
    };

    // 1. First priority: search in scope
    if (!scope.empty()) {
        std::string cur_s = scope;
        while (!cur_s.empty()) {
            for (const auto& pair : engine.struct_types) {
                if (pair.first.rfind(cur_s, 0) == 0) {
                    if (matches_all(pair.second)) {
                        return &pair.second;
                    }
                }
            }
            auto dot = cur_s.rfind('.');
            if (dot == std::string::npos) break;
            cur_s = cur_s.substr(0, dot);
        }
    }

    // 2. Second priority: any struct_types entry that matches
    for (const auto& pair : engine.struct_types) {
        if (matches_all(pair.second)) {
            return &pair.second;
        }
    }

    return nullptr;
}

LogicVal ExprEvaluator::eval_struct_pattern(UnpackedConcatNode* ucat, const PackedStructType& st, const std::string& scope) {
    LogicVal res(st.total_width, 0, 0, st.is_signed);

    // 1. Process 'default:' member if present
    ExpressionNode* default_expr = nullptr;
    for (const auto& elem : ucat->elements) {
        if (auto pm = dynamic_cast<PatternMemberNode*>(elem.get())) {
            if (pm->member_name == "default") {
                default_expr = pm->value.get();
                break;
            }
        }
    }

    if (default_expr) {
        LogicVal def_val = eval(default_expr, scope);
        for (const auto& sm : st.members) {
            LogicVal casted = LogicOps::cast_width(def_val, sm.width, sm.is_signed);
            res.deposit_slice(sm.offset, sm.width, casted);
        }
    }

    // 2. Process all elements (by-name or positional)
    size_t pos_idx = 0;
    for (const auto& elem : ucat->elements) {
        if (auto pm = dynamic_cast<PatternMemberNode*>(elem.get())) {
            if (pm->member_name == "default") {
                continue;
            }
            if (!pm->member_name.empty()) {
                const StructMember* sm = st.get_member(pm->member_name);
                if (sm) {
                    LogicVal m_val = eval(pm->value.get(), scope);
                    m_val = LogicOps::cast_width(m_val, sm->width, sm->is_signed);
                    res.deposit_slice(sm->offset, sm->width, m_val);
                }
            } else {
                if (pos_idx < st.members.size()) {
                    const auto& sm = st.members[pos_idx++];
                    LogicVal m_val = eval(pm->value.get(), scope);
                    m_val = LogicOps::cast_width(m_val, sm.width, sm.is_signed);
                    res.deposit_slice(sm.offset, sm.width, m_val);
                }
            }
        } else {
            if (pos_idx < st.members.size()) {
                const auto& sm = st.members[pos_idx++];
                LogicVal m_val = eval(elem.get(), scope);
                m_val = LogicOps::cast_width(m_val, sm.width, sm.is_signed);
                res.deposit_slice(sm.offset, sm.width, m_val);
            }
        }
    }

    return res;
}

std::vector<LogicVal> ExprEvaluator::eval_array_expr(ExpressionNode* expr, const std::string& scope) {
    if (!expr) return {};

    if (auto ucat = dynamic_cast<UnpackedConcatNode*>(expr)) {
        std::vector<LogicVal> res;
        for (auto& elem : ucat->elements) {
            if (auto sub_ucat = dynamic_cast<UnpackedConcatNode*>(elem.get())) {
                auto sub = eval_array_expr(sub_ucat, scope);
                res.insert(res.end(), sub.begin(), sub.end());
            } else if (auto sub_cat = dynamic_cast<ConcatNode*>(elem.get())) {
                auto sub = eval_array_expr(sub_cat, scope);
                res.insert(res.end(), sub.begin(), sub.end());
            } else if (auto sub_rep = dynamic_cast<ReplicateNode*>(elem.get())) {
                auto sub = eval_array_expr(sub_rep, scope);
                res.insert(res.end(), sub.begin(), sub.end());
            } else if (auto id = dynamic_cast<IdentifierNode*>(elem.get())) {
                std::vector<LogicVal> id_elems;
                uint32_t ew = 32;
                bool sgn = false;
                if (get_array_elements(id->name, scope, id_elems, ew, sgn)) {
                    res.insert(res.end(), id_elems.begin(), id_elems.end());
                } else {
                    res.push_back(eval(elem.get(), scope));
                }
            } else {
                LogicVal v = eval(elem.get(), scope);
                if (v.is_unpacked) {
                    res.insert(res.end(), v.unpacked_elements.begin(), v.unpacked_elements.end());
                } else {
                    res.push_back(v);
                }
            }
        }
        return res;
    }

    if (auto cat = dynamic_cast<ConcatNode*>(expr)) {
        std::vector<LogicVal> res;
        for (auto& elem : cat->elements) {
            if (auto sub_ucat = dynamic_cast<UnpackedConcatNode*>(elem.get())) {
                auto sub = eval_array_expr(sub_ucat, scope);
                res.insert(res.end(), sub.begin(), sub.end());
            } else if (auto sub_cat = dynamic_cast<ConcatNode*>(elem.get())) {
                auto sub = eval_array_expr(sub_cat, scope);
                res.insert(res.end(), sub.begin(), sub.end());
            } else if (auto sub_rep = dynamic_cast<ReplicateNode*>(elem.get())) {
                auto sub = eval_array_expr(sub_rep, scope);
                res.insert(res.end(), sub.begin(), sub.end());
            } else if (auto id = dynamic_cast<IdentifierNode*>(elem.get())) {
                std::vector<LogicVal> id_elems;
                uint32_t ew = 32;
                bool sgn = false;
                if (get_array_elements(id->name, scope, id_elems, ew, sgn)) {
                    res.insert(res.end(), id_elems.begin(), id_elems.end());
                } else {
                    res.push_back(eval(elem.get(), scope));
                }
            } else {
                LogicVal v = eval(elem.get(), scope);
                if (v.is_unpacked) {
                    res.insert(res.end(), v.unpacked_elements.begin(), v.unpacked_elements.end());
                } else {
                    res.push_back(v);
                }
            }
        }
        return res;
    }

    if (auto rep = dynamic_cast<ReplicateNode*>(expr)) {
        LogicVal count_val = eval(rep->count_expr.get(), scope);
        int64_t count = count_val.is_unknown() ? 0 : (int64_t)count_val.aval;
        std::vector<LogicVal> inner_elems;
        if (auto ucat = dynamic_cast<UnpackedConcatNode*>(rep->target_expr.get())) {
            inner_elems = eval_array_expr(ucat, scope);
        } else if (auto cat = dynamic_cast<ConcatNode*>(rep->target_expr.get())) {
            inner_elems = eval_array_expr(cat, scope);
        } else if (auto id = dynamic_cast<IdentifierNode*>(rep->target_expr.get())) {
            uint32_t ew = 32;
            bool sgn = false;
            if (!get_array_elements(id->name, scope, inner_elems, ew, sgn)) {
                inner_elems.push_back(eval(id, scope));
            }
        } else {
            LogicVal v = eval(rep->target_expr.get(), scope);
            if (v.is_unpacked) {
                inner_elems = v.unpacked_elements;
            } else {
                inner_elems.push_back(v);
            }
        }
        std::vector<LogicVal> res;
        if (count > 0) {
            res.reserve(inner_elems.size() * (size_t)count);
            for (int64_t i = 0; i < count; ++i) {
                res.insert(res.end(), inner_elems.begin(), inner_elems.end());
            }
        }
        return res;
    }

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

std::string ExprEvaluator::resolve_canonical_type(ExpressionNode* target_expr, const std::string& explicit_type_str, const std::string& scope, const std::string& context_name) {
    std::string sec_ref = (context_name == "type()") ? "6.23" : "20.6";

    // 1. If explicit type keyword / dimension was parsed
    if (!explicit_type_str.empty()) {
        return explicit_type_str;
    }

    if (!target_expr) {
        if (context_name == "type()") {
            throw std::runtime_error("[CAS-DTINPCIL] type() requires an argument per IEEE 1800-2023 section 6.23");
        } else {
            throw std::runtime_error("[CAS-DTINPCIL] $typename requires exactly one argument per IEEE 1800-2023 section 20.6");
        }
    }

    auto format_udt = [&](const std::string& type_name) -> std::string {
        if (type_name.empty()) return "";
        std::string unqualified = type_name;
        std::string explicit_pkg = "";
        auto cpos = type_name.rfind("::");
        if (cpos != std::string::npos) {
            explicit_pkg = type_name.substr(0, cpos);
            unqualified = type_name.substr(cpos + 2);
        }

        // 1. Search in package_defs
        for (const auto& kv : engine.package_defs) {
            const auto& pkg = kv.second;
            if (!explicit_pkg.empty() && pkg->name != explicit_pkg) continue;

            if (pkg->enum_typedefs.count(unqualified)) {
                return "enum " + pkg->name + "::" + unqualified;
            }
            if (pkg->struct_typedefs.count(unqualified)) {
                return "struct " + pkg->name + "::" + unqualified;
            }
            if (pkg->class_map.count(unqualified) || engine.is_class(pkg->name + "::" + unqualified)) {
                return "class " + pkg->name + "::" + unqualified;
            }
            for (const auto& td : pkg->typedefs) {
                if (td->type_name == unqualified) {
                    if (td->enum_type) return "enum " + pkg->name + "::" + unqualified;
                    if (td->struct_type) return "struct " + pkg->name + "::" + unqualified;
                }
            }
        }

        // 2. Search class definitions
        if (engine.is_class(type_name) || engine.is_class(unqualified)) {
            const ClassNode* cdef = engine.get_class_def(type_name);
            if (!cdef) cdef = engine.get_class_def(unqualified);
            if (cdef) {
                for (const auto& kv : engine.package_defs) {
                    if (kv.second->class_map.count(cdef->name) || kv.second->class_map.count(unqualified)) {
                        return "class " + kv.second->name + "::" + unqualified;
                    }
                }
                return "class " + cdef->name;
            }
        }

        // 3. Search enum_types / struct_types in engine
        if (engine.enum_types.count(type_name) || engine.enum_types.count(unqualified)) {
            return "enum " + (explicit_pkg.empty() ? unqualified : type_name);
        }
        if (engine.struct_types.count(type_name) || engine.struct_types.count(unqualified)) {
            return "struct " + (explicit_pkg.empty() ? unqualified : type_name);
        }

        return "";
    };

    // If target is StaticMemberAccessNode (e.g. v3_48_0_0_pkg::state_e)
    if (auto sma = dynamic_cast<StaticMemberAccessNode*>(target_expr)) {
        std::string full = sma->class_type_name + "::" + sma->member_name;
        std::string res = format_udt(full);
        if (!res.empty()) return res;
        throw std::runtime_error("[CAS-DTINPCIL] Argument of " + context_name + " cannot be statically resolved to a known data type or netlist signal per IEEE 1800-2023 section " + sec_ref + ": '" + full + "'");
    }

    // If target is IndexSelectNode (e.g. arr[0])
    if (auto idx = dynamic_cast<IndexSelectNode*>(target_expr)) {
        std::string arr_name = idx->name;
        LocalVar* loc = nullptr;
        if (context.has_active_frame()) loc = context.lookup_local(arr_name);
        if (loc) {
            if (loc->is_unpacked_array) {
                if (!loc->type_name.empty()) {
                    std::string fres = format_udt(loc->type_name);
                    if (!fres.empty()) return fres;
                    if (loc->type_name == "int" || loc->type_name == "byte" || loc->type_name == "string" || loc->type_name == "real") {
                        return loc->type_name;
                    }
                }
                if (loc->elem_width == 32) return "int";
                if (loc->elem_width == 8) return "byte";
                if (loc->elem_width > 1) return "logic[" + std::to_string(loc->elem_width - 1) + ":0]";
                return "logic";
            }
            return "logic";
        }
        std::string scoped_mem = scope.empty() ? arr_name : (scope + "." + arr_name);
        if (engine.memory_map.count(scoped_mem)) {
            const auto& mem = engine.memory_map.at(scoped_mem);
            if (mem.word_width == 32) return "int";
            if (mem.word_width == 8) return "byte";
            if (mem.word_width > 1) return "logic[" + std::to_string(mem.word_width - 1) + ":0]";
            return "logic";
        }
        return "logic";
    }

    // If target is MemberAccessNode (e.g. pkt.id)
    if (auto ma = dynamic_cast<MemberAccessNode*>(target_expr)) {
        LogicVal val = eval(ma, scope);
        if (val.is_string) return "string";
        if (val.is_real) return "real";
        if (val.width == 32) return "int";
        if (val.width == 8) return "byte";
        if (val.width > 1) return "logic[" + std::to_string(val.width - 1) + ":0]";
        return "logic";
    }

    // If target is IdentifierNode
    if (auto id = dynamic_cast<IdentifierNode*>(target_expr)) {
        const std::string& name = id->name;

        // 1. Check if name is directly a UDT (enum, struct, class)
        std::string udt_res = format_udt(name);
        if (!udt_res.empty()) {
            return udt_res;
        }

        // 2. Check primitive keyword names
        if (name == "int" || name == "byte" || name == "shortint" || name == "longint" ||
            name == "integer" || name == "time" || name == "string" || name == "real" ||
            name == "shortreal" || name == "realtime" || name == "chandle" || name == "void") {
            return name;
        }
        if (name == "logic" || name == "bit" || name == "reg") {
            return name;
        }

        // 3. Check local variable
        LocalVar* local = nullptr;
        if (context.has_active_frame()) {
            local = context.lookup_local(name);
        }
        if (!local && current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                local = it->local_frame.get_var(name);
                if (local) break;
            }
        }

        if (local) {
            if (local->is_class_handle || !local->class_type_name.empty()) {
                std::string cname = local->class_type_name;
                if (local->value.aval != 0) {
                    auto obj = engine.get_class_object(local->value.aval);
                    if (obj && !obj->class_type_name.empty()) cname = obj->class_type_name;
                }
                std::string fres = format_udt(cname);
                if (!fres.empty()) return fres;
                return "class " + cname;
            }
            if (!local->type_name.empty()) {
                std::string fres = format_udt(local->type_name);
                if (!fres.empty()) return fres;
                if (local->type_name == "int" || local->type_name == "byte" || local->type_name == "string" || local->type_name == "real") {
                    return local->type_name;
                }
            }
            if (!local->dimensions.empty()) {
                auto& d = local->dimensions[0];
                return "logic[" + std::to_string(d.left) + ":" + std::to_string(d.right) + "]";
            }
            if (local->width > 1) {
                return "logic[" + std::to_string(local->width - 1) + ":0]";
            }
            return "logic";
        }

        // 4. Check class member if in class method context
        if (current_this) {
            if (current_this->has_property(name)) {
                LogicVal mem = current_this->get_property(name);
                if (mem.is_string) return "string";
                if (mem.is_real) return "real";
                if (current_this->class_def) {
                    auto mdef = current_this->class_def->find_member_var(name);
                    if (mdef && !mdef->class_type_name.empty()) {
                        std::string fres = format_udt(mdef->class_type_name);
                        if (!fres.empty()) return fres;
                        return "class " + mdef->class_type_name;
                    }
                }
                if (mem.width == 32) return "int";
                if (mem.width > 1) return "logic[" + std::to_string(mem.width - 1) + ":0]";
                return "logic";
            }
        }

        // 5. Check netlist signals / ports
        std::string scoped_net = scope.empty() ? name : (scope + "." + name);
        size_t nid = (size_t)-1;
        if (engine.net_map.count(scoped_net)) nid = engine.net_map.at(scoped_net);
        else if (engine.net_map.count(name)) nid = engine.net_map.at(name);

        if (nid != (size_t)-1) {
            const Net& net = engine.netlist[nid];

            if (engine.var_to_enum_type.count(scoped_net)) {
                std::string fres = format_udt(engine.var_to_enum_type.at(scoped_net));
                if (!fres.empty()) return fres;
            } else if (engine.var_to_enum_type.count(name)) {
                std::string fres = format_udt(engine.var_to_enum_type.at(name));
                if (!fres.empty()) return fres;
            }

            if (engine.var_to_struct_type.count(scoped_net)) {
                std::string fres = format_udt(engine.var_to_struct_type.at(scoped_net));
                if (!fres.empty()) return fres;
            } else if (engine.var_to_struct_type.count(name)) {
                std::string fres = format_udt(engine.var_to_struct_type.at(name));
                if (!fres.empty()) return fres;
            }

            if (engine.var_to_class_type.count(scoped_net)) {
                std::string fres = format_udt(engine.var_to_class_type.at(scoped_net));
                if (!fres.empty()) return fres;
            } else if (engine.var_to_class_type.count(name)) {
                std::string fres = format_udt(engine.var_to_class_type.at(name));
                if (!fres.empty()) return fres;
            }

            if (!net.type_name.empty()) {
                std::string fres = format_udt(net.type_name);
                if (!fres.empty()) return fres;
                if (net.type_name == "int" || net.type_name == "byte" || net.type_name == "string" || net.type_name == "real") {
                    return net.type_name;
                }
            }

            if (engine.var_dimensions.count(scoped_net)) {
                auto& vdm = engine.var_dimensions.at(scoped_net);
                if (!vdm.dimensions.empty()) {
                    auto& d = vdm.dimensions[0];
                    std::string base = (net.type_name == "bit") ? "bit" : "logic";
                    return base + "[" + std::to_string(d.left) + ":" + std::to_string(d.right) + "]";
                }
            } else if (engine.var_dimensions.count(name)) {
                auto& vdm = engine.var_dimensions.at(name);
                if (!vdm.dimensions.empty()) {
                    auto& d = vdm.dimensions[0];
                    std::string base = (net.type_name == "bit") ? "bit" : "logic";
                    return base + "[" + std::to_string(d.left) + ":" + std::to_string(d.right) + "]";
                }
            }

            if (net.type_name == "bit") {
                return (net.width > 1) ? ("bit[" + std::to_string(net.width - 1) + ":0]") : "bit";
            }
            if (net.width > 1) {
                return "logic[" + std::to_string(net.width - 1) + ":0]";
            }
            return "logic";
        }

        throw std::runtime_error("[CAS-DTINPCIL] Argument of " + context_name + " cannot be statically resolved to a known data type or netlist signal per IEEE 1800-2023 section " + sec_ref + ": '" + name + "'");
    }

    // 6. Evaluate general expression if possible
    LogicVal val = eval(target_expr, scope);
    if (val.is_string) return "string";
    if (val.is_real) return "real";
    if (val.width == 32) return "int";
    if (val.width > 1) return "logic[" + std::to_string(val.width - 1) + ":0]";
    return "logic";
}

LogicVal ExprEvaluator::eval_typename(TypeNameNode* tn, const std::string& scope) {
    if (!tn) {
        throw std::runtime_error("[CAS-DTINPCIL] $typename requires exactly one argument per IEEE 1800-2023 section 20.6");
    }
    return LogicVal::make_string(resolve_canonical_type(tn->target_expr.get(), tn->type_name_arg, scope, "$typename"));
}

LogicVal ExprEvaluator::eval_typeop(TypeOpNode* ton, const std::string& scope) {
    if (!ton) {
        throw std::runtime_error("[CAS-DTINPCIL] type() requires an argument per IEEE 1800-2023 section 6.23");
    }
    return LogicVal::make_string(resolve_canonical_type(ton->target_expr.get(), ton->type_name_arg, scope, "type()"));
}

LogicVal ExprEvaluator::eval_vector_intrinsic(VectorIntrinsicNode* vin, const std::string& scope) {
    if (!vin || !vin->operand) {
        throw std::runtime_error("[CAS-DTINPCIL] Vector intrinsic function requires exactly one argument per IEEE 1800-2023 section 20.9");
    }
    std::string func_name;
    switch (vin->kind) {
        case VectorIntrinsicKind::COUNTONES: func_name = "$countones"; break;
        case VectorIntrinsicKind::ONEHOT:    func_name = "$onehot"; break;
        case VectorIntrinsicKind::ONEHOT0:   func_name = "$onehot0"; break;
        case VectorIntrinsicKind::ISUNKNOWN: func_name = "$isunknown"; break;
    }

    std::string var_ident = "";
    if (auto id = dynamic_cast<IdentifierNode*>(vin->operand.get())) {
        var_ident = id->name;
    } else if (auto scid = dynamic_cast<StaticMemberAccessNode*>(vin->operand.get())) {
        var_ident = scid->class_type_name + "::" + scid->member_name;
    }

    if (!var_ident.empty()) {
        LocalVar* sloc = nullptr;
        if (context.has_active_frame()) sloc = context.lookup_local(var_ident);
        if (!sloc && current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                sloc = it->local_frame.get_var(var_ident);
                if (sloc) break;
            }
        }
        if (sloc && (sloc->is_class_handle || engine.is_class(sloc->type_name) || sloc->type_name == "chandle")) {
            throw std::runtime_error("[CAS-DTINPCIL] Non-integral argument '" + var_ident + "' passed to " + func_name + " per IEEE 1800-2023 section 20.9");
        }
        size_t snid = engine.get_net_id(scope, var_ident);
        if (snid != (size_t)-1 && (engine.netlist[snid].is_class_handle || engine.is_class(engine.netlist[snid].type_name) || engine.netlist[snid].type_name == "chandle")) {
            throw std::runtime_error("[CAS-DTINPCIL] Non-integral argument '" + var_ident + "' passed to " + func_name + " per IEEE 1800-2023 section 20.9");
        }
    }

    LogicVal val = eval(vin->operand.get(), scope);
    if (val.is_string || val.is_real || val.is_unpacked) {
        throw std::runtime_error("[CAS-DTINPCIL] Non-integral argument passed to " + func_name + " per IEEE 1800-2023 section 20.9");
    }

    uint32_t ones = 0;
    uint32_t unknowns = 0;
    uint32_t w = std::min(val.width, 64U);
    for (uint32_t i = 0; i < w; ++i) {
        bool b = (val.bval >> i) & 1ULL;
        bool a = (val.aval >> i) & 1ULL;
        if (b) {
            unknowns++;
        } else if (a) {
            ones++;
        }
    }

    switch (vin->kind) {
        case VectorIntrinsicKind::COUNTONES: {
            return LogicVal(32, (uint64_t)ones, 0);
        }
        case VectorIntrinsicKind::ONEHOT: {
            if (unknowns > 0) return LogicVal(1, 0, 0);
            return LogicVal(1, (ones == 1) ? 1ULL : 0ULL, 0);
        }
        case VectorIntrinsicKind::ONEHOT0: {
            return LogicVal(1, (ones + unknowns <= 1) ? 1ULL : 0ULL, 0);
        }
        case VectorIntrinsicKind::ISUNKNOWN: {
            return LogicVal(1, (unknowns > 0) ? 1ULL : 0ULL, 0);
        }
    }
    return LogicVal(1, 0, 0);
}

LogicVal ExprEvaluator::eval_clog2(Clog2Node* clog, const std::string& scope) {
    if (!clog || !clog->operand) {
        throw std::runtime_error("[CAS-DTINPCIL] $clog2 requires exactly one argument per IEEE 1800-2023 section 20.8.1");
    }

    std::string var_ident = "";
    if (auto id = dynamic_cast<IdentifierNode*>(clog->operand.get())) {
        var_ident = id->name;
    } else if (auto scid = dynamic_cast<StaticMemberAccessNode*>(clog->operand.get())) {
        var_ident = scid->class_type_name + "::" + scid->member_name;
    }

    if (!var_ident.empty()) {
        LocalVar* sloc = nullptr;
        if (context.has_active_frame()) sloc = context.lookup_local(var_ident);
        if (!sloc && current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                sloc = it->local_frame.get_var(var_ident);
                if (sloc) break;
            }
        }
        if (sloc && (sloc->is_class_handle || engine.is_class(sloc->type_name) || sloc->type_name == "chandle")) {
            throw std::runtime_error("[CAS-DTINPCIL] Non-integral argument '" + var_ident + "' passed to $clog2 per IEEE 1800-2023 section 20.8.1");
        }
        size_t snid = engine.get_net_id(scope, var_ident);
        if (snid != (size_t)-1 && (engine.netlist[snid].is_class_handle || engine.is_class(engine.netlist[snid].type_name) || engine.netlist[snid].type_name == "chandle")) {
            throw std::runtime_error("[CAS-DTINPCIL] Non-integral argument '" + var_ident + "' passed to $clog2 per IEEE 1800-2023 section 20.8.1");
        }
    }

    LogicVal val = eval(clog->operand.get(), scope);
    if (val.is_string || val.is_real || val.is_unpacked) {
        throw std::runtime_error("[CAS-DTINPCIL] Non-integral argument passed to $clog2 per IEEE 1800-2023 section 20.8.1");
    }

    if (val.is_unknown()) {
        return LogicVal::make_x(32);
    }

    return LogicVal::make_known(32, compute_clog2(val.aval));
}

LogicVal ExprEvaluator::eval_countbits(CountBitsNode* node, const std::string& scope) {
    if (!node || !node->target_expr || node->control_bits.empty() || node->control_bits.size() > 4) {
        throw std::runtime_error("[CAS-DTINPCIL] $countbits requires between 2 and 5 arguments per IEEE 1800-2023 section 20.9");
    }

    auto check_integral_arg = [&](ExpressionNode* expr) {
        if (!expr) return;
        std::string var_ident = "";
        if (auto id = dynamic_cast<IdentifierNode*>(expr)) {
            var_ident = id->name;
        } else if (auto scid = dynamic_cast<StaticMemberAccessNode*>(expr)) {
            var_ident = scid->class_type_name + "::" + scid->member_name;
        }

        if (!var_ident.empty()) {
            LocalVar* sloc = nullptr;
            if (context.has_active_frame()) sloc = context.lookup_local(var_ident);
            if (!sloc && current_bp && !current_bp->call_stack.empty()) {
                for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                    sloc = it->local_frame.get_var(var_ident);
                    if (sloc) break;
                }
            }
            if (sloc && (sloc->is_class_handle || engine.is_class(sloc->type_name) || sloc->type_name == "chandle" || sloc->type_name == "real" || sloc->type_name == "string" || sloc->value.is_real || sloc->value.is_string || sloc->value.is_unpacked)) {
                throw std::runtime_error("[CAS-DTINPCIL] Non-integral argument '" + var_ident + "' passed to $countbits per IEEE 1800-2023 section 20.9");
            }
            size_t snid = engine.get_net_id(scope, var_ident);
            if (snid != (size_t)-1 && (engine.netlist[snid].is_class_handle || engine.is_class(engine.netlist[snid].type_name) || engine.netlist[snid].type_name == "chandle" || engine.netlist[snid].type_name == "real" || engine.netlist[snid].type_name == "string" || engine.netlist[snid].value.is_real || engine.netlist[snid].value.is_string || engine.netlist[snid].value.is_unpacked)) {
                throw std::runtime_error("[CAS-DTINPCIL] Non-integral argument '" + var_ident + "' passed to $countbits per IEEE 1800-2023 section 20.9");
            }
        }
    };

    check_integral_arg(node->target_expr.get());
    for (auto& c : node->control_bits) {
        check_integral_arg(c.get());
    }

    LogicVal target_val = eval(node->target_expr.get(), scope);
    if (target_val.is_string || target_val.is_real || target_val.is_unpacked) {
        throw std::runtime_error("[CAS-DTINPCIL] Non-integral argument passed to $countbits per IEEE 1800-2023 section 20.9");
    }

    bool match_0 = false;
    bool match_1 = false;
    bool match_x = false;
    bool match_z = false;

    for (auto& ctrl_expr : node->control_bits) {
        LogicVal cval = eval(ctrl_expr.get(), scope);
        if (cval.is_string || cval.is_real || cval.is_unpacked) {
            throw std::runtime_error("[CAS-DTINPCIL] Non-integral argument passed to $countbits per IEEE 1800-2023 section 20.9");
        }
        bool a0 = (cval.aval & 1ULL) != 0;
        bool b0 = (cval.bval & 1ULL) != 0;
        if (!a0 && !b0) match_0 = true;
        else if (a0 && !b0) match_1 = true;
        else if (a0 && b0) match_x = true;
        else if (!a0 && b0) match_z = true;
    }

    uint32_t count = 0;
    uint32_t w = std::min(target_val.width, 64U);
    for (uint32_t i = 0; i < w; ++i) {
        bool a = (target_val.aval >> i) & 1ULL;
        bool b = (target_val.bval >> i) & 1ULL;
        if (!a && !b && match_0) count++;
        else if (a && !b && match_1) count++;
        else if (a && b && match_x) count++;
        else if (!a && b && match_z) count++;
    }

    return LogicVal::make_known(32, (uint64_t)count);
}

LogicVal ExprEvaluator::eval_array_dimension_query(ArrayDimensionQueryNode* node, const std::string& scope) {
    if (!node) {
        throw std::runtime_error("[CAS-DTINPCIL] Array query system function requires exactly one argument per IEEE 1800-2023 section 20.7");
    }

    std::string var_name = "";
    if (!node->type_name_arg.empty()) {
        var_name = node->type_name_arg;
    } else if (auto id = dynamic_cast<IdentifierNode*>(node->target.get())) {
        var_name = id->name;
    } else if (auto ma = dynamic_cast<MemberAccessNode*>(node->target.get())) {
        var_name = ma->var_name + "." + ma->member_name;
    }

    VariableDimMetadata meta;
    bool found = false;

    // 1. Check local variables
    if (!var_name.empty() && context.has_active_frame()) {
        LocalVar* loc = context.lookup_local(var_name);
        if (loc && loc->is_vector_or_array && !loc->dimensions.empty()) {
            meta.dimensions = loc->dimensions;
            meta.unpacked_dims = loc->unpacked_dims;
            meta.is_vector_or_array = true;
            found = true;
        }
    }

    // 2. Check call stack
    if (!found && !var_name.empty() && current_bp && !current_bp->call_stack.empty()) {
        for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
            LocalVar* loc = it->local_frame.get_var(var_name);
            if (loc && loc->is_vector_or_array && !loc->dimensions.empty()) {
                meta.dimensions = loc->dimensions;
                meta.unpacked_dims = loc->unpacked_dims;
                meta.is_vector_or_array = true;
                found = true;
                break;
            }
        }
    }

    // 3. Check engine dynamic array
    if (!found && !var_name.empty()) {
        DynamicArrayObject* da = engine.get_dynamic_array(var_name, scope);
        if (da) {
            meta.dimensions.push_back({0, (int64_t)da->size() - 1, true, true, false});
            if (da->elem_width > 1) {
                meta.dimensions.push_back({(int64_t)da->elem_width - 1, 0, false, false, false});
            }
            meta.unpacked_dims = 1;
            meta.is_vector_or_array = true;
            found = true;
        }
    }

    // 4. Check engine queue
    if (!found && !var_name.empty()) {
        QueueObject* q = engine.get_queue(var_name, scope);
        if (q) {
            meta.dimensions.push_back({0, (int64_t)q->size() - 1, true, false, true});
            if (q->elem_width > 1) {
                meta.dimensions.push_back({(int64_t)q->elem_width - 1, 0, false, false, false});
            }
            meta.unpacked_dims = 1;
            meta.is_vector_or_array = true;
            found = true;
        }
    }

    // 5. Check engine memory_map
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

    // 6. Check engine.var_dimensions
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

    // 7. Check engine.net_map
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

    // 8. Check Parser::find_type_dim_metadata
    if (!found && !var_name.empty()) {
        const VariableDimMetadata* tmeta = Parser::find_type_dim_metadata(var_name);
        if (tmeta && !tmeta->dimensions.empty()) {
            meta = *tmeta;
            found = true;
        }
    }

    // 9. Parse primitive keywords with bracket dimensions (e.g. logic[31:0])
    if (!found && !node->type_name_arg.empty()) {
        std::string s = node->type_name_arg;
        size_t pos = 0;
        while ((pos = s.find('[', pos)) != std::string::npos) {
            size_t cpos = s.find(':', pos);
            size_t epos = s.find(']', pos);
            if (cpos != std::string::npos && epos != std::string::npos && cpos < epos) {
                int64_t left = std::stoll(s.substr(pos + 1, cpos - pos - 1));
                int64_t right = std::stoll(s.substr(cpos + 1, epos - cpos - 1));
                meta.dimensions.push_back({left, right, false, false, false});
                meta.is_vector_or_array = true;
                found = true;
            }
            pos = epos + 1;
        }
    }

    if (!found || !meta.is_vector_or_array || meta.dimensions.empty()) {
        throw std::runtime_error("[CAS-DTINPCIL] Illegal first argument to array query system function; please pass an array identifier or array data type");
    }

    if (node->kind == DimQueryKind::TOTAL_DIMENSIONS) {
        return LogicVal::make_known(32, meta.dimensions.size(), true);
    } else {
        return LogicVal::make_known(32, meta.unpacked_dims, true);
    }
}

LogicVal ExprEvaluator::eval_array_bound_query(ArrayBoundQueryNode* node, const std::string& scope) {
    std::string var_name = node->type_name_arg;
    if (var_name.empty()) {
        if (auto id = dynamic_cast<IdentifierNode*>(node->target.get())) {
            var_name = id->name;
        } else if (auto ma = dynamic_cast<MemberAccessNode*>(node->target.get())) {
            var_name = ma->var_name + "." + ma->member_name;
        }
    }

    VariableDimMetadata meta;
    bool found = false;

    // 1. Check local variables in execution context
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
            } else if (loc->is_vector_or_array && !loc->dimensions.empty()) {
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

    // 2. Check call stack
    if (!found && !var_name.empty() && current_bp && !current_bp->call_stack.empty()) {
        for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
            LocalVar* loc = it->local_frame.get_var(var_name);
            if (loc && loc->is_vector_or_array && !loc->dimensions.empty()) {
                meta.dimensions = loc->dimensions;
                meta.unpacked_dims = loc->unpacked_dims;
                meta.is_vector_or_array = true;
                found = true;
                break;
            }
        }
    }

    // 3. Check engine dynamic array
    if (!found && !var_name.empty()) {
        DynamicArrayObject* da = engine.get_dynamic_array(var_name, scope);
        if (da) {
            meta.dimensions.push_back({0, (int64_t)da->size() - 1, true, true, false});
            if (da->elem_width > 1) {
                meta.dimensions.push_back({(int64_t)da->elem_width - 1, 0, false, false, false});
            }
            meta.unpacked_dims = 1;
            meta.is_vector_or_array = true;
            found = true;
        }
    }

    // 4. Check engine queue
    if (!found && !var_name.empty()) {
        QueueObject* q = engine.get_queue(var_name, scope);
        if (q) {
            meta.dimensions.push_back({0, (int64_t)q->size() - 1, true, false, true});
            if (q->elem_width > 1) {
                meta.dimensions.push_back({(int64_t)q->elem_width - 1, 0, false, false, false});
            }
            meta.unpacked_dims = 1;
            meta.is_vector_or_array = true;
            found = true;
        }
    }

    // 5. Check engine memory_map
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

    // 6. Check engine.var_dimensions
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

    // 7. Check engine.net_map
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

    // 8. Check Parser::find_type_dim_metadata
    if (!found && !var_name.empty()) {
        const VariableDimMetadata* tmeta = Parser::find_type_dim_metadata(var_name);
        if (tmeta && !tmeta->dimensions.empty()) {
            meta = *tmeta;
            found = true;
        }
    }

    // 9. Parse primitive keywords with bracket dimensions (e.g. logic[31:0])
    if (!found && !node->type_name_arg.empty()) {
        std::string s = node->type_name_arg;
        size_t pos = 0;
        while ((pos = s.find('[', pos)) != std::string::npos) {
            size_t cpos = s.find(':', pos);
            size_t epos = s.find(']', pos);
            if (cpos != std::string::npos && epos != std::string::npos && cpos < epos) {
                int64_t left = std::stoll(s.substr(pos + 1, cpos - pos - 1));
                int64_t right = std::stoll(s.substr(cpos + 1, epos - cpos - 1));
                meta.dimensions.push_back({left, right, false, false, false});
                meta.is_vector_or_array = true;
                found = true;
            }
            pos = epos + 1;
        }
    }

    if (!found || !meta.is_vector_or_array || meta.dimensions.empty()) {
        throw std::runtime_error("[CAS-DTINPCIL] Illegal first argument to array query system function; please pass an array identifier or array data type");
    }

    int64_t dim = 1;
    if (node->dimension_expr) {
        LogicVal dval = eval(node->dimension_expr.get(), scope);
        dim = (int64_t)dval.to_signed();
    }

    if (dim < 1 || (size_t)dim > meta.dimensions.size()) {
        throw std::runtime_error("[CAS-DTINPCIL] Invalid dimension index " + std::to_string(dim) + " for array querying function per IEEE 1800-2023 section 20.7");
    }

    const auto& d = meta.dimensions[dim - 1];
    switch (node->kind) {
        case BoundQueryKind::LEFT:
            return LogicVal::make_known(32, (uint64_t)(int64_t)d.left, true);
        case BoundQueryKind::RIGHT:
            return LogicVal::make_known(32, (uint64_t)(int64_t)d.right, true);
        case BoundQueryKind::LOW:
            return LogicVal::make_known(32, (uint64_t)(int64_t)d.low(), true);
        case BoundQueryKind::HIGH:
            return LogicVal::make_known(32, (uint64_t)(int64_t)d.high(), true);
        case BoundQueryKind::INCREMENT: {
            int64_t inc = d.increment();
            uint64_t val = (inc < 0) ? (uint64_t)(uint32_t)-1 : (uint64_t)inc;
            return LogicVal::make_known(32, val, true);
        }
    }
    return LogicVal::make_x(32);
}

std::string ExprEvaluator::format_systemverilog_string(
    const std::string& fmt,
    const std::vector<LogicVal>& args,
    uint64_t sim_time
) {
    std::string result;
    size_t arg_idx = 0;
    size_t fmt_len = fmt.size();

    for (size_t i = 0; i < fmt_len; ++i) {
        if (fmt[i] == '%' && i + 1 < fmt_len) {
            ++i;
            if (fmt[i] == '%') {
                result += '%';
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

            if (spec == 't') {
                if (arg_idx < args.size()) {
                    const LogicVal& val = args[arg_idx++];
                    if (val.is_unknown()) {
                        result += "x";
                    } else if (val.is_signed) {
                        result += std::to_string(val.to_signed());
                    } else {
                        result += std::to_string(val.aval);
                    }
                } else {
                    result += std::to_string(sim_time);
                }
                continue;
            }

            if (arg_idx >= args.size()) {
                continue;
            }

            const LogicVal& val = args[arg_idx++];

            if (spec == 's') {
                std::string s;
                if (val.is_string) {
                    s = val.str_val;
                } else {
                    for (int b = (int)val.width - 8; b >= 0; b -= 8) {
                        char ch = (char)((val.aval >> b) & 0xFF);
                        if (ch != 0 || !s.empty()) s += ch;
                    }
                    if (s.empty() && val.width <= 32) {
                        s = std::to_string(val.aval);
                    }
                }
                if (width > 0) {
                    while (s.size() < (size_t)width) s = " " + s;
                }
                result += s;
            } else if (spec == 'd') {
                std::string s;
                if (val.is_unknown()) {
                    s = "x";
                } else if (val.is_real) {
                    s = std::to_string((int64_t)val.real_val);
                } else if (val.is_signed) {
                    s = std::to_string(val.to_signed());
                } else {
                    s = std::to_string(val.aval);
                }
                if (pad_zero && width > 0) {
                    while (s.size() < (size_t)width) s = "0" + s;
                } else if (!pad_zero && width > 0) {
                    while (s.size() < (size_t)width) s = " " + s;
                }
                result += s;
            } else if (spec == 'f') {
                double d = val.is_real ? val.real_val : (double)val.aval;
                std::stringstream ss;
                ss << d;
                result += ss.str();
            } else if (spec == 'h' || spec == 'x') {
                std::string s;
                if (!val.is_unknown()) {
                    std::stringstream ss;
                    ss << std::hex << val.aval << std::dec;
                    s = ss.str();
                } else {
                    s = val.to_hex_string(true);
                }
                for (char& c : s) c = (char)std::tolower((unsigned char)c);
                if (pad_zero && width > 0) {
                    while (s.size() < (size_t)width) s = "0" + s;
                } else if (!pad_zero && width > 0) {
                    while (s.size() < (size_t)width) s = " " + s;
                } else if (!pad_zero && width == 0) {
                    size_t hex_digits = (val.width + 3) / 4;
                    while (s.size() < hex_digits) s = "0" + s;
                }
                result += s;
            } else if (spec == 'b') {
                std::string s = val.to_bin_string();
                if (pad_zero && width == 0) {
                    size_t first_non_zero = s.find_first_not_of('0');
                    if (first_non_zero != std::string::npos) {
                        s = s.substr(first_non_zero);
                    } else {
                        s = "0";
                    }
                } else if (pad_zero && width > 0) {
                    while (s.size() < (size_t)width) s = "0" + s;
                } else if (!pad_zero && width > 0) {
                    while (s.size() < (size_t)width) s = " " + s;
                }
                result += s;
            } else if (spec == 'o') {
                std::string s;
                if (!val.is_unknown()) {
                    std::stringstream ss;
                    ss << std::oct << val.aval << std::dec;
                    s = ss.str();
                } else {
                    s = "x";
                }
                if (pad_zero && width == 0) {
                    size_t first_non_zero = s.find_first_not_of('0');
                    if (first_non_zero != std::string::npos) {
                        s = s.substr(first_non_zero);
                    } else {
                        s = "0";
                    }
                } else if (pad_zero && width > 0) {
                    while (s.size() < (size_t)width) s = "0" + s;
                } else if (!pad_zero && width > 0) {
                    while (s.size() < (size_t)width) s = " " + s;
                }
                result += s;
            } else if (spec == 'c') {
                char ch = (char)(val.aval & 0xFF);
                result += ch;
            } else {
                result += val.is_string ? val.str_val : std::to_string(val.aval);
            }
        } else {
            result += fmt[i];
        }
    }
    while (arg_idx < args.size()) {
        const auto& val = args[arg_idx++];
        if (val.is_string) result += val.str_val;
        else if (val.is_real) result += std::to_string(val.real_val);
        else if (val.is_signed) result += std::to_string(val.to_signed());
        else result += std::to_string(val.aval);
    }
    return result;
}

LogicVal ExprEvaluator::eval_sformatf(SFormatfNode* node, const std::string& scope) {
    if (!node || node->args.empty()) {
        throw std::runtime_error("[CAS-DTINPCIL] $sformatf requires at least 1 argument per IEEE 1800-2023 section 20.14");
    }
    LogicVal fmt_val = eval(node->args[0].get(), scope);
    if (!fmt_val.is_string) {
        throw std::runtime_error("[CAS-DTINPCIL] Format argument in $sformatf must evaluate to a string type per IEEE 1800-2023 section 20.14");
    }
    std::vector<LogicVal> arg_vals;
    for (size_t i = 1; i < node->args.size(); ++i) {
        arg_vals.push_back(eval(node->args[i].get(), scope));
    }
    std::string formatted = format_systemverilog_string(fmt_val.str_val, arg_vals, engine.get_sim_time());
    return LogicVal::make_string(formatted);
}
