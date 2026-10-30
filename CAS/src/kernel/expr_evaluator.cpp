#include "../../include/kernel/expr_evaluator.hpp"
#include "../../include/engine.hpp"
#include "../../include/kernel/logic_ops.hpp"
#include <algorithm>
#include <cmath>

ExprEvaluator::ExprEvaluator(Engine& eng) : engine(eng) {}

bool ExprEvaluator::eval_function(const std::string& func_name, const std::vector<LogicVal>& arg_vals, LogicVal& result_out) {
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
    context.declare_local(target_fn->name, target_fn->return_width, target_fn->return_signed);
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
        context.declare_local(in_p.name, w, in_p.is_signed);
        if (i < arg_vals.size()) {
            context.set_local_value(in_p.name, arg_vals[i]);
        }
    }

    // 3. Execute body statements directly through Evaluator
    BoundProcess* prev_bp = current_bp;
    BoundProcess bp{target_fn->scope.empty() ? frame_name : target_fn->scope, nullptr, 0, false};
    set_current_process(&bp);
    engine.evaluator.set_current_process(&bp);
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
        result_out = ret_var->value;
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
        result_out = ret_var->value;
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
        return val;
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

        if (var_name == "this" || var_name == "super") {
            if (current_this) {
                if (current_this->class_def && current_this->class_def->has_static_member(member_name)) {
                    return engine.get_class_static_var(current_this->class_type_name, member_name);
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

        std::vector<LogicVal> evaluated_args;
        for (const auto& a : fn_call->args) {
            evaluated_args.push_back(eval(a.get(), scope));
        }

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
            if (local) return local->value;
        }

        // Check task activation frame on current process
        if (current_bp && !current_bp->call_stack.empty()) {
            for (auto it = current_bp->call_stack.rbegin(); it != current_bp->call_stack.rend(); ++it) {
                LocalVar* local = it->local_frame.get_var(ident->name);
                if (local) return local->value;
            }
        }

        if (current_this && current_this->has_property(ident->name)) {
            return current_this->get_property(ident->name);
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
        if (engine.net_map.find(scoped_ident) != engine.net_map.end()) {
            size_t nid = engine.net_map.at(scoped_ident);
            if (preponed_override && nid < preponed_override->size()) {
                return (*preponed_override)[nid];
            }
            return engine.netlist[nid].value;
        }
        if (engine.param_map.find(ident->name) != engine.param_map.end()) {
            return LogicVal::make_known(32, engine.param_map.at(ident->name));
        }
        if (engine.net_map.find(ident->name) != engine.net_map.end()) {
            size_t nid = engine.net_map.at(ident->name);
            if (preponed_override && nid < preponed_override->size()) {
                return (*preponed_override)[nid];
            }
            return engine.netlist[nid].value;
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
            size_t nid = engine.net_map.at(scoped_ident);
            if (preponed_override && nid < preponed_override->size()) {
                target_val = (*preponed_override)[nid];
            } else {
                target_val = engine.netlist[nid].value;
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
