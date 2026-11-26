#include "../include/engine.hpp"
#include "../include/frontend/lexer.hpp"
#include "../include/kernel/logic_ops.hpp"
#include "../include/kernel/codegen.hpp"
#include "../include/kernel/native_jit.hpp"
#include "../include/kernel/llvm_jit.hpp"
#include "../include/kernel/vpi_runtime.hpp"

#include <algorithm>
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <elf.h>
#include <fcntl.h>
#include <unistd.h>
#endif

Engine::Engine() : evaluator(*this), scheduler(*this) {}

void Engine::load_elaboration(ElaborationResult&& elab_res) {
    netlist = std::move(elab_res.netlist);
    net_map = std::move(elab_res.net_map);
    canonical_net_map = std::move(elab_res.canonical_net_map);
    alias_members = std::move(elab_res.alias_members);
    slice_bindings = std::move(elab_res.slice_bindings);
    memory_bindings = std::move(elab_res.memory_bindings);
    memory_continuous_assigns = std::move(elab_res.memory_continuous_assigns);
    generated_processes = std::move(elab_res.generated_processes);
    generated_continuous_assigns = std::move(elab_res.generated_continuous_assigns);
    continuous_assigns = std::move(elab_res.continuous_assigns);
    resolved_continuous_assigns.clear();
    std::unordered_map<uint64_t, size_t> slice_to_rca;
    for (const auto& ca : continuous_assigns) {
        uint64_t key = ((uint64_t)ca.target.net_id << 32) | ((uint64_t)ca.target.offset << 16) | (uint64_t)ca.target.width;
        auto it = slice_to_rca.find(key);
        ContinuousAssignDriver drv;
        drv.is_gate = false;
        drv.gate_idx = 0;
        drv.scope = ca.scope;
        drv.rhs = ca.rhs;
        if (it == slice_to_rca.end()) {
            size_t idx = resolved_continuous_assigns.size();
            slice_to_rca[key] = idx;
            ResolvedContinuousAssign rca;
            rca.target = ca.target;
            rca.drivers.push_back(drv);
            resolved_continuous_assigns.push_back(std::move(rca));
        } else {
            resolved_continuous_assigns[it->second].drivers.push_back(drv);
        }
    }
    gates = std::move(elab_res.gates);
    for (size_t g_idx = 0; g_idx < gates.size(); ++g_idx) {
        const auto& g = gates[g_idx];
        uint64_t key = ((uint64_t)g.out_target.net_id << 32) | ((uint64_t)g.out_target.offset << 16) | (uint64_t)g.out_target.width;
        auto it = slice_to_rca.find(key);
        ContinuousAssignDriver drv;
        drv.is_gate = true;
        drv.gate_idx = g_idx;
        drv.scope = g.scope;
        drv.rhs = nullptr;
        if (it == slice_to_rca.end()) {
            size_t idx = resolved_continuous_assigns.size();
            slice_to_rca[key] = idx;
            ResolvedContinuousAssign rca;
            rca.target = g.out_target;
            rca.drivers.push_back(drv);
            resolved_continuous_assigns.push_back(std::move(rca));
        } else {
            resolved_continuous_assigns[it->second].drivers.push_back(drv);
        }
    }
    module_path_delays = std::move(elab_res.module_path_delays);
    timing_checks = std::move(elab_res.timing_checks);
    var_dimensions = std::move(elab_res.var_dimensions);
    memory_map = std::move(elab_res.memory_map);
    queue_map = std::move(elab_res.queue_map);
    queue_names.clear();
    for (const auto& kv : queue_map) {
        auto d = kv.first.rfind('.');
        queue_names.insert((d != std::string::npos) ? kv.first.substr(d + 1) : kv.first);
        queue_names.insert(kv.first);
    }
    dynamic_array_map = std::move(elab_res.dynamic_array_map);
    dynamic_array_names.clear();
    for (const auto& kv : dynamic_array_map) {
        auto d = kv.first.rfind('.');
        dynamic_array_names.insert((d != std::string::npos) ? kv.first.substr(d + 1) : kv.first);
        dynamic_array_names.insert(kv.first);
    }
    assoc_array_map = std::move(elab_res.assoc_array_map);
    assoc_array_names.clear();
    for (const auto& kv : assoc_array_map) {
        auto d = kv.first.rfind('.');
        assoc_array_names.insert((d != std::string::npos) ? kv.first.substr(d + 1) : kv.first);
        assoc_array_names.insert(kv.first);
    }
    timescale_map = std::move(elab_res.timescale_map);
    global_prec_fs = elab_res.global_prec_fs;
    scopes_with_timeunit = std::move(elab_res.scopes_with_timeunit);
    scope_cu_id = std::move(elab_res.scope_cu_id);
    task_map = std::move(elab_res.task_map);
    param_map = std::move(elab_res.param_map);
    param_val_map = std::move(elab_res.param_val_map);
    processes = std::move(elab_res.processes);
    comb_target_net_ids = std::move(elab_res.comb_target_net_ids);
    function_map = std::move(elab_res.function_map);
    unbounded_params = std::move(elab_res.unbounded_params);
    named_events = std::move(elab_res.named_events);
    named_event_map = std::move(elab_res.named_event_map);
    enum_types = std::move(elab_res.enum_types);
    var_to_enum_type = std::move(elab_res.var_to_enum_type);
    enum_constant_map = std::move(elab_res.enum_constant_map);
    struct_types = std::move(elab_res.struct_types);
    var_to_struct_type = std::move(elab_res.var_to_struct_type);
    simple_types = std::move(elab_res.simple_types);
    unpacked_array_types = std::move(elab_res.unpacked_array_types);
    interface_port_map = std::move(elab_res.interface_port_map);
    modport_binding_map = std::move(elab_res.modport_binding_map);
    clocking_blocks = std::move(elab_res.clocking_blocks);
    clocking_block_map = std::move(elab_res.clocking_block_map);
    default_clocking_map = std::move(elab_res.default_clocking_map);
    class_defs = std::move(elab_res.class_defs);
    class_handle_names = std::move(elab_res.class_handle_names);
    var_to_class_type = std::move(elab_res.var_to_class_type);
    for (const auto& ich : elab_res.initial_class_handles) {
        auto obj = allocate_class_object(ich.second);
        if (ich.first < netlist.size()) {
            netlist[ich.first].value = LogicVal(64, obj->handle_id, 0);
            netlist[ich.first].prev_value = LogicVal(64, obj->handle_id, 0);
        }
    }
    covergroup_defs = std::move(elab_res.covergroup_defs);
    covergroup_handle_names = std::move(elab_res.covergroup_handle_names);
    var_to_covergroup_type = std::move(elab_res.var_to_covergroup_type);
    sva_assertions = std::move(elab_res.sva_assertions);
    property_defs = std::move(elab_res.property_defs);
    sequence_defs = std::move(elab_res.sequence_defs);
    assertion_names = std::move(elab_res.assertion_names);
    property_names = std::move(elab_res.property_names);
    sequence_names = std::move(elab_res.sequence_names);
    sva_local_var_names = std::move(elab_res.sva_local_var_names);
    dpi_import_map = std::move(elab_res.dpi_import_map);
    dpi_export_map = std::move(elab_res.dpi_export_map);
    chandle_names = std::move(elab_res.chandle_names);
    open_array_names = std::move(elab_res.open_array_names);
    semaphore_handle_names = std::move(elab_res.semaphore_handle_names);
    mailbox_handle_names = std::move(elab_res.mailbox_handle_names);
    interface_instances = std::move(elab_res.interface_instances);
    virtual_interface_names = std::move(elab_res.virtual_interface_names);
    for (size_t i = 0; i < processes.size(); ++i) {
        processes[i].pid = i;
    }
    active_scope_name = elab_res.top_scope;
    DpiRegistry::set_active_scope_by_name(active_scope_name);
    DpiRegistry::set_active_engine(this);
    bind_all_ast_nodes();
    build_fanout_graph();

    if (init_reg_mode != InitRegMode::X) {
        for (auto& net : netlist) {
            if (net.kind == NetKind::VAR) {
                if (init_reg_mode == InitRegMode::ZERO) {
                    net.value = LogicVal::make_known(net.width, 0, net.is_signed);
                } else if (init_reg_mode == InitRegMode::ONE) {
                    net.value = LogicVal::make_ones(net.width, net.is_signed);
                }
                net.prev_value = net.value;
            }
        }
        for (auto& kv : memory_map) {
            auto& mem = kv.second;
            for (auto& w : mem.words) {
                if (init_reg_mode == InitRegMode::ZERO) {
                    w = LogicVal::make_known(mem.word_width, 0, false);
                } else if (init_reg_mode == InitRegMode::ONE) {
                    w = LogicVal::make_ones(mem.word_width, false);
                }
            }
        }
    }
}

struct FuncSensContext {
    std::unordered_set<std::string> visited_fn_names;
    std::unordered_set<std::string> shadowed_names;
};

static size_t resolve_hierarchical_net_id(const Engine& engine, const std::string& scope, const std::string& name) {
    size_t raw_nid = engine.get_net_id(scope, name);
    if (raw_nid != (size_t)-1 || name.find('.') == std::string::npos) {
        return raw_nid;
    }
    size_t dpos = name.rfind('.');
    while (dpos != std::string::npos && dpos > 0) {
        std::string base = name.substr(0, dpos);
        raw_nid = engine.get_net_id(scope, base);
        if (raw_nid != (size_t)-1) return raw_nid;
        if (dpos == 0) break;
        dpos = name.rfind('.', dpos - 1);
    }
    return (size_t)-1;
}

static void collect_read_nets_internal(const ExpressionNode* expr, const std::string& scope, const Engine& engine, std::vector<uint32_t>& out_net_ids, bool& out_reads_mem, FuncSensContext* ctx = nullptr);
static void collect_stmt_read_nets(const StatementNode* stmt, const std::string& scope, const Engine& engine, std::vector<uint32_t>& out_net_ids, bool& out_reads_mem, FuncSensContext* ctx = nullptr);

static void collect_read_nets(const ExpressionNode* expr, const std::string& scope, const Engine& engine, std::vector<uint32_t>& out_net_ids) {
    bool dummy = false;
    collect_read_nets_internal(expr, scope, engine, out_net_ids, dummy, nullptr);
}
void collect_read_nets(const ExpressionNode* expr, const std::string& scope, const Engine& engine, std::vector<uint32_t>& out_net_ids, bool& out_reads_mem) {
    collect_read_nets_internal(expr, scope, engine, out_net_ids, out_reads_mem, nullptr);
}
static void collect_read_nets_internal(const ExpressionNode* expr, const std::string& scope, const Engine& engine, std::vector<uint32_t>& out_net_ids, bool& out_reads_mem, FuncSensContext* ctx) {
    if (!expr) return;

    if (auto id = dynamic_cast<const IdentifierNode*>(expr)) {
        if (ctx && ctx->shadowed_names.count(id->name)) {
            return;
        }
        if (id->is_bound) {
            if (id->cached_net_id != UINT32_MAX) {
                out_net_ids.push_back(id->cached_net_id);
            }
            return;
        }
        if (engine.is_memory(scope, id->name)) {
            out_reads_mem = true;
        }
        size_t exact_nid = engine.get_net_id(scope, id->name);
        size_t raw_nid = (exact_nid != (size_t)-1) ? exact_nid : resolve_hierarchical_net_id(engine, scope, id->name);
        if (raw_nid != (size_t)-1) {
            size_t canon = engine.get_canonical_net_id(raw_nid);
            if (exact_nid != (size_t)-1) {
                id->cached_net_id = (uint32_t)canon;
                id->is_bound = true;
            }
            out_net_ids.push_back((uint32_t)canon);
        }
        return;
    }

    if (auto idx = dynamic_cast<const IndexSelectNode*>(expr)) {
        if (ctx && ctx->shadowed_names.count(idx->name)) {
            if (idx->msb_expr) collect_read_nets_internal(idx->msb_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
            if (idx->lsb_expr) collect_read_nets_internal(idx->lsb_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
            if (idx->bit_msb_expr) collect_read_nets_internal(idx->bit_msb_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
            if (idx->bit_lsb_expr) collect_read_nets_internal(idx->bit_lsb_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
            for (const auto& extra : idx->extra_indices) {
                if (extra) collect_read_nets_internal(extra.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
            }
            return;
        }
        if (idx->is_bound) {
            if (idx->cached_net_id != UINT32_MAX) {
                out_net_ids.push_back(idx->cached_net_id);
            }
        } else {
            if (engine.is_memory(scope, idx->name)) {
                out_reads_mem = true;
            }
            size_t exact_nid = engine.get_net_id(scope, idx->name);
            size_t raw_nid = (exact_nid != (size_t)-1) ? exact_nid : resolve_hierarchical_net_id(engine, scope, idx->name);
            if (raw_nid != (size_t)-1) {
                size_t canon = engine.get_canonical_net_id(raw_nid);
                if (exact_nid != (size_t)-1) {
                    idx->cached_net_id = (uint32_t)canon;
                    idx->is_bound = true;
                }
                out_net_ids.push_back((uint32_t)canon);
            }
        }
        if (idx->msb_expr) collect_read_nets_internal(idx->msb_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        if (idx->lsb_expr) collect_read_nets_internal(idx->lsb_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        if (idx->bit_msb_expr) collect_read_nets_internal(idx->bit_msb_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        if (idx->bit_lsb_expr) collect_read_nets_internal(idx->bit_lsb_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        for (const auto& extra : idx->extra_indices) {
            if (extra) collect_read_nets_internal(extra.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        }
        return;
    }

    if (auto mem = dynamic_cast<const MemberAccessNode*>(expr)) {
        if (mem->is_bound) {
            if (mem->cached_net_id != UINT32_MAX) {
                out_net_ids.push_back(mem->cached_net_id);
            }
        } else {
            std::string full_name = mem->var_name + "." + mem->member_name;
            if (mem->index_expr) {
                if (auto num = dynamic_cast<const NumberNode*>(mem->index_expr.get())) {
                    full_name = mem->var_name + "[" + std::to_string(num->aval) + "]." + mem->member_name;
                }
            }
            size_t exact_nid = engine.get_net_id(scope, full_name);
            size_t raw_nid = (exact_nid != (size_t)-1) ? exact_nid : resolve_hierarchical_net_id(engine, scope, full_name);
            if (raw_nid == (size_t)-1) {
                raw_nid = engine.get_net_id(scope, mem->var_name);
            }
            if (raw_nid == (size_t)-1) {
                auto dpos = mem->var_name.find('.');
                if (dpos != std::string::npos) {
                    raw_nid = engine.get_net_id(scope, mem->var_name.substr(0, dpos));
                }
            }
            if (raw_nid != (size_t)-1) {
                size_t canon = engine.get_canonical_net_id(raw_nid);
                if (exact_nid != (size_t)-1) {
                    mem->cached_net_id = (uint32_t)canon;
                    mem->is_bound = true;
                }
                out_net_ids.push_back((uint32_t)canon);
            }
        }
        if (mem->index_expr) collect_read_nets_internal(mem->index_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto un = dynamic_cast<const UnaryOpNode*>(expr)) {
        collect_read_nets_internal(un->operand.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto bin = dynamic_cast<const BinaryOpNode*>(expr)) {
        collect_read_nets_internal(bin->lhs.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        collect_read_nets_internal(bin->rhs.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto tern = dynamic_cast<const TernaryOpNode*>(expr)) {
        collect_read_nets_internal(tern->condition.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        collect_read_nets_internal(tern->true_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        collect_read_nets_internal(tern->false_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto cat = dynamic_cast<const ConcatNode*>(expr)) {
        for (const auto& elem : cat->elements) {
            collect_read_nets_internal(elem.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        }
        return;
    }

    if (auto ucat = dynamic_cast<const UnpackedConcatNode*>(expr)) {
        for (const auto& elem : ucat->elements) {
            collect_read_nets_internal(elem.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        }
        return;
    }

    if (auto pm = dynamic_cast<const PatternMemberNode*>(expr)) {
        if (pm->value) collect_read_nets_internal(pm->value.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto rep = dynamic_cast<const ReplicateNode*>(expr)) {
        collect_read_nets_internal(rep->count_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        collect_read_nets_internal(rep->target_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto sc = dynamic_cast<const StreamingConcatNode*>(expr)) {
        if (sc->slice_size) collect_read_nets_internal(sc->slice_size.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        for (const auto& elem : sc->stream_items) {
            collect_read_nets_internal(elem.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        }
        return;
    }

    if (auto fn = dynamic_cast<const FunctionCallNode*>(expr)) {
        for (const auto& arg : fn->args) {
            collect_read_nets_internal(arg.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        }
        std::string scoped_fn = scope.empty() ? fn->func_name : (scope + "." + fn->func_name);
        FuncSensContext local_ctx;
        FuncSensContext* act_ctx = ctx ? ctx : &local_ctx;
        if (act_ctx->visited_fn_names.count(scoped_fn) || act_ctx->visited_fn_names.count(fn->func_name)) {
            return;
        }
        act_ctx->visited_fn_names.insert(scoped_fn);
        act_ctx->visited_fn_names.insert(fn->func_name);

        const FunctionNode* target_fn = engine.find_function(scoped_fn);
        if (!target_fn) {
            target_fn = engine.find_function(fn->func_name);
        }
        if (target_fn) {
            std::string fn_scope = target_fn->scope.empty() ? scope : target_fn->scope;
            for (size_t i = fn->args.size(); i < target_fn->inputs.size(); ++i) {
                if (target_fn->inputs[i].default_value) {
                    collect_read_nets_internal(target_fn->inputs[i].default_value.get(), fn_scope, engine, out_net_ids, out_reads_mem, act_ctx);
                }
            }
            std::vector<std::string> added_shadows;
            if (act_ctx->shadowed_names.insert(target_fn->name).second) {
                added_shadows.push_back(target_fn->name);
            }
            for (const auto& p : target_fn->inputs) {
                if (act_ctx->shadowed_names.insert(p.name).second) {
                    added_shadows.push_back(p.name);
                }
            }
            for (const auto& s : target_fn->get_body()) {
                collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem, act_ctx);
            }
            for (const auto& sname : added_shadows) {
                act_ctx->shadowed_names.erase(sname);
            }
        }
        return;
    }

    if (auto mc = dynamic_cast<const MethodCallNode*>(expr)) {
        size_t raw_nid = engine.get_net_id(scope, mc->var_name);
        if (raw_nid != (size_t)-1) {
            size_t canon = engine.get_canonical_net_id(raw_nid);
            out_net_ids.push_back((uint32_t)canon);
        }
        for (const auto& arg : mc->args) collect_read_nets_internal(arg.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        if (mc->with_expr) collect_read_nets_internal(mc->with_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto cast_expr = dynamic_cast<const CastNode*>(expr)) {
        collect_read_nets_internal(cast_expr->expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto scast = dynamic_cast<const StaticCastExprNode*>(expr)) {
        collect_read_nets_internal(scast->expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto sys_cast = dynamic_cast<const SysCastCallNode*>(expr)) {
        if (sys_cast->dest_expr) collect_read_nets_internal(sys_cast->dest_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        if (sys_cast->src_expr) collect_read_nets_internal(sys_cast->src_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto ins = dynamic_cast<const InsideNode*>(expr)) {
        if (ins->target) collect_read_nets_internal(ins->target.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        for (const auto& r : ins->ranges) {
            if (r.low) collect_read_nets_internal(r.low.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
            if (r.high) collect_read_nets_internal(r.high.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        }
        return;
    }

    if (auto clog = dynamic_cast<const Clog2Node*>(expr)) {
        collect_read_nets_internal(clog->operand.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto cb = dynamic_cast<const CountBitsNode*>(expr)) {
        if (cb->target_expr) collect_read_nets_internal(cb->target_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        for (const auto& c : cb->control_bits) {
            collect_read_nets_internal(c.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        }
        return;
    }

    if (auto vi = dynamic_cast<const VectorIntrinsicNode*>(expr)) {
        collect_read_nets_internal(vi->operand.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto sf = dynamic_cast<const SFormatfNode*>(expr)) {
        for (const auto& arg : sf->args) {
            collect_read_nets_internal(arg.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        }
        return;
    }

    if (auto aq = dynamic_cast<const ArrayQueryNode*>(expr)) {
        if (aq->array_expr) collect_read_nets_internal(aq->array_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto adq = dynamic_cast<const ArrayDimensionQueryNode*>(expr)) {
        if (adq->target) collect_read_nets_internal(adq->target.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto abq = dynamic_cast<const ArrayBoundQueryNode*>(expr)) {
        if (abq->target) collect_read_nets_internal(abq->target.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        if (abq->dimension_expr) collect_read_nets_internal(abq->dimension_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto lc = dynamic_cast<const LetCallNode*>(expr)) {
        for (const auto& a : lc->args) collect_read_nets_internal(a.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }
}

static void collect_stmt_read_nets(const StatementNode* stmt, const std::string& scope, const Engine& engine, std::vector<uint32_t>& out_net_ids, bool& out_reads_mem, FuncSensContext* ctx) {
    if (!stmt) return;

    if (auto if_s = dynamic_cast<const IfStatementNode*>(stmt)) {
        collect_read_nets_internal(if_s->condition.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        for (const auto& s : if_s->then_branch) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        for (const auto& s : if_s->else_branch) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto blk = dynamic_cast<const BlockStatementNode*>(stmt)) {
        for (const auto& s : blk->statements) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto cs = dynamic_cast<const CaseStatementNode*>(stmt)) {
        collect_read_nets_internal(cs->selector.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        for (const auto& item : cs->items) {
            if (item.match_expr) collect_read_nets_internal(item.match_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
            for (const auto& me : item.match_exprs) collect_read_nets_internal(me.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
            for (const auto& s : item.statements) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        }
        return;
    }

    if (auto for_s = dynamic_cast<const ForStatementNode*>(stmt)) {
        collect_stmt_read_nets(for_s->init_stmt.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        collect_read_nets_internal(for_s->condition.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        collect_stmt_read_nets(for_s->step_stmt.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        for (const auto& s : for_s->body) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto fe = dynamic_cast<const ForeachStatementNode*>(stmt)) {
        size_t raw_nid = engine.get_net_id(scope, fe->array_name);
        if (raw_nid != (size_t)-1) {
            size_t canon = engine.get_canonical_net_id(raw_nid);
            out_net_ids.push_back((uint32_t)canon);
        }
        for (const auto& s : fe->body) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto wh = dynamic_cast<const WhileStatementNode*>(stmt)) {
        collect_read_nets_internal(wh->condition.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        for (const auto& s : wh->body) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto rep = dynamic_cast<const RepeatStatementNode*>(stmt)) {
        collect_read_nets_internal(rep->count_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        for (const auto& s : rep->body) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto fo = dynamic_cast<const ForeverStatementNode*>(stmt)) {
        for (const auto& s : fo->body) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto ba = dynamic_cast<const BlockingAssignNode*>(stmt)) {
        collect_read_nets_internal(ba->rhs.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        if (ba->bit_idx) collect_read_nets_internal(ba->bit_idx.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        if (ba->bit_lsb) collect_read_nets_internal(ba->bit_lsb.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        if (ba->bit_idx2) collect_read_nets_internal(ba->bit_idx2.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        if (ba->bit_lsb2) collect_read_nets_internal(ba->bit_lsb2.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto nba = dynamic_cast<const NonBlockingAssignNode*>(stmt)) {
        collect_read_nets_internal(nba->rhs.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        if (nba->bit_idx) collect_read_nets_internal(nba->bit_idx.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        if (nba->bit_lsb) collect_read_nets_internal(nba->bit_lsb.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        if (nba->bit_idx2) collect_read_nets_internal(nba->bit_idx2.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        if (nba->bit_lsb2) collect_read_nets_internal(nba->bit_lsb2.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto mem = dynamic_cast<const MemAssignNode*>(stmt)) {
        collect_read_nets_internal(mem->addr_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        collect_read_nets_internal(mem->rhs.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto pa = dynamic_cast<const ProceduralAssignNode*>(stmt)) {
        collect_read_nets_internal(pa->rhs.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto ret = dynamic_cast<const ReturnStatementNode*>(stmt)) {
        if (ret->expr) collect_read_nets_internal(ret->expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto tc = dynamic_cast<const TaskCallNode*>(stmt)) {
        for (const auto& arg : tc->args) collect_read_nets_internal(arg.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto disp = dynamic_cast<const DisplayStatementNode*>(stmt)) {
        for (const auto& arg : disp->arg_exprs) collect_read_nets_internal(arg.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto sev = dynamic_cast<const SeverityTaskStatementNode*>(stmt)) {
        if (sev->finish_number) collect_read_nets_internal(sev->finish_number.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        for (const auto& arg : sev->arg_exprs) collect_read_nets_internal(arg.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto ev_ctl = dynamic_cast<const EventControlStatementNode*>(stmt)) {
        if (!ev_ctl->events.empty()) {
            for (const auto& ev : ev_ctl->events) {
                size_t raw_nid = engine.get_net_id(scope, ev.second);
                if (raw_nid != (size_t)-1) {
                    size_t canon = engine.get_canonical_net_id(raw_nid);
                    out_net_ids.push_back((uint32_t)canon);
                }
            }
        } else {
            size_t raw_nid = engine.get_net_id(scope, ev_ctl->signal_name);
            if (raw_nid != (size_t)-1) {
                size_t canon = engine.get_canonical_net_id(raw_nid);
                out_net_ids.push_back((uint32_t)canon);
            }
        }
        return;
    }

    if (auto wc = dynamic_cast<const WaitConditionStatementNode*>(stmt)) {
        if (wc->condition) collect_read_nets_internal(wc->condition.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        if (wc->body) collect_stmt_read_nets(wc->body.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto fork_s = dynamic_cast<const ForkStatementNode*>(stmt)) {
        for (const auto& s : fork_s->branches) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto delay_s = dynamic_cast<const DelayStatementNode*>(stmt)) {
        if (delay_s->delay_expr) collect_read_nets_internal(delay_s->delay_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto vd = dynamic_cast<const VarDeclStatementNode*>(stmt)) {
        if (vd->init_expr) collect_read_nets_internal(vd->init_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto sf = dynamic_cast<const SFormatStatementNode*>(stmt)) {
        if (sf->target_expr) collect_read_nets_internal(sf->target_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        if (sf->format_expr) collect_read_nets_internal(sf->format_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        for (const auto& arg : sf->arg_exprs) collect_read_nets_internal(arg.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto vc = dynamic_cast<const VoidCastStatementNode*>(stmt)) {
        if (vc->expr) collect_read_nets_internal(vc->expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto sc = dynamic_cast<const SysCastStatementNode*>(stmt)) {
        if (sc->dest_expr) collect_read_nets_internal(sc->dest_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        if (sc->src_expr) collect_read_nets_internal(sc->src_expr.get(), scope, engine, out_net_ids, out_reads_mem, ctx);
        return;
    }

    if (auto r = dynamic_cast<const RandomizeStatementNode*>(stmt)) {
        if (r->call) collect_read_nets(r->call.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }
}

static void collect_stmt_write_nets(const StatementNode* stmt, const std::string& scope, const Engine& engine, std::unordered_set<uint32_t>& out_written_nets) {
    if (!stmt) return;

    auto resolve_and_insert = [&](const std::string& raw_name) {
        size_t raw_nid = engine.get_net_id(scope, raw_name);
        if (raw_nid == (size_t)-1) {
            auto dot_pos = raw_name.find('.');
            auto bracket_pos = raw_name.find('[');
            size_t cut = std::min(dot_pos, bracket_pos);
            if (cut != std::string::npos) {
                raw_nid = engine.get_net_id(scope, raw_name.substr(0, cut));
            }
        }
        if (raw_nid != (size_t)-1) {
            size_t canon = engine.get_canonical_net_id(raw_nid);
            out_written_nets.insert((uint32_t)canon);
            const auto& am = engine.get_alias_members();
            if (canon < am.size()) {
                for (size_t aid : am[canon]) {
                    out_written_nets.insert((uint32_t)aid);
                }
            }
            if (raw_nid < am.size()) {
                for (size_t aid : am[raw_nid]) {
                    out_written_nets.insert((uint32_t)aid);
                }
            }
        }
    };

    if (auto ba = dynamic_cast<const BlockingAssignNode*>(stmt)) {
        for (const auto& t : ba->lhs_targets) {
            resolve_and_insert(t);
        }
        return;
    }

    if (auto nba = dynamic_cast<const NonBlockingAssignNode*>(stmt)) {
        resolve_and_insert(nba->target);
        return;
    }

    if (auto pa = dynamic_cast<const ProceduralAssignNode*>(stmt)) {
        resolve_and_insert(pa->target);
        return;
    }

    if (auto blk = dynamic_cast<const BlockStatementNode*>(stmt)) {
        for (const auto& s : blk->statements) collect_stmt_write_nets(s.get(), scope, engine, out_written_nets);
        return;
    }

    if (auto if_s = dynamic_cast<const IfStatementNode*>(stmt)) {
        for (const auto& s : if_s->then_branch) collect_stmt_write_nets(s.get(), scope, engine, out_written_nets);
        for (const auto& s : if_s->else_branch) collect_stmt_write_nets(s.get(), scope, engine, out_written_nets);
        return;
    }

    if (auto cs = dynamic_cast<const CaseStatementNode*>(stmt)) {
        for (const auto& item : cs->items) {
            for (const auto& s : item.statements) collect_stmt_write_nets(s.get(), scope, engine, out_written_nets);
        }
        return;
    }

    if (auto for_s = dynamic_cast<const ForStatementNode*>(stmt)) {
        collect_stmt_write_nets(for_s->init_stmt.get(), scope, engine, out_written_nets);
        collect_stmt_write_nets(for_s->step_stmt.get(), scope, engine, out_written_nets);
        for (const auto& s : for_s->body) collect_stmt_write_nets(s.get(), scope, engine, out_written_nets);
        return;
    }

    if (auto wh = dynamic_cast<const WhileStatementNode*>(stmt)) {
        for (const auto& s : wh->body) collect_stmt_write_nets(s.get(), scope, engine, out_written_nets);
        return;
    }

    if (auto rep = dynamic_cast<const RepeatStatementNode*>(stmt)) {
        for (const auto& s : rep->body) collect_stmt_write_nets(s.get(), scope, engine, out_written_nets);
        return;
    }

    if (auto fo = dynamic_cast<const ForeverStatementNode*>(stmt)) {
        for (const auto& s : fo->body) collect_stmt_write_nets(s.get(), scope, engine, out_written_nets);
        return;
    }

    if (auto fork_s = dynamic_cast<const ForkStatementNode*>(stmt)) {
        for (const auto& s : fork_s->branches) collect_stmt_write_nets(s.get(), scope, engine, out_written_nets);
        return;
    }
}

static void collect_stmt_read_nets(const StatementNode* stmt, const std::string& scope, const Engine& engine, std::vector<uint32_t>& out_net_ids) {
    bool dummy = false;
    collect_stmt_read_nets(stmt, scope, engine, out_net_ids, dummy);
}

bool Engine::is_excluded_from_net_binding(const std::string& name, const std::string& scope) const {
    if (name.empty() || name[0] == '$') return true;
    if (name.find('.') != std::string::npos || name.find("::") != std::string::npos) return true;

    std::string scoped = scope.empty() ? name : (scope + "." + name);

    // 1. Enum constants
    if (enum_constant_map.find(scoped) != enum_constant_map.end() ||
        enum_constant_map.find(name) != enum_constant_map.end()) {
        return true;
    }

    // 2. Parameters
    if (param_val_map.find(scoped) != param_val_map.end() ||
        param_map.find(scoped) != param_map.end() ||
        param_val_map.find(name) != param_val_map.end() ||
        param_map.find(name) != param_map.end()) {
        return true;
    }

    std::string cur_s = scope;
    while (!cur_s.empty()) {
        std::string s_id = cur_s + "." + name;
        if (param_val_map.find(s_id) != param_val_map.end() ||
            param_map.find(s_id) != param_map.end()) {
            return true;
        }
        auto d = cur_s.rfind('.');
        if (d == std::string::npos) break;
        cur_s = cur_s.substr(0, d);
    }

    for (const auto& pkg : package_defs) {
        if (pkg.second && pkg.second->param_constants.count(name)) {
            return true;
        }
    }

    // 3. Memories
    if (is_memory(scope, name)) return true;

    // 4. Dynamic arrays, queues, associative arrays
    if (is_dynamic_array(name, scope) || is_queue(name, scope) || is_assoc_array(name, scope)) {
        return true;
    }

    // 5. Interface instances
    if (interface_instances.find(scoped) != interface_instances.end() ||
        interface_instances.find(name) != interface_instances.end() ||
        virtual_interface_names.find(name) != virtual_interface_names.end()) {
        return true;
    }

    return false;
}

static void bind_ast_stmt(StatementNode* stmt, const std::string& scope, const Engine& engine);

static void bind_ast_expr(ExpressionNode* expr, const std::string& scope, const Engine& engine) {
    if (!expr) return;

    if (auto id = dynamic_cast<IdentifierNode*>(expr)) {
        if (!id->is_bound && !engine.is_excluded_from_net_binding(id->name, scope)) {
            size_t raw_nid = engine.get_net_id(scope, id->name);
            if (raw_nid != (size_t)-1) {
                size_t canon = engine.get_canonical_net_id(raw_nid);
                id->cached_net_id = (uint32_t)canon;
                id->is_bound = true;
            }
        }
        return;
    }

    if (auto idx = dynamic_cast<IndexSelectNode*>(expr)) {
        if (!idx->is_bound) {
            size_t raw_nid = engine.get_net_id(scope, idx->name);
            if (raw_nid != (size_t)-1) {
                idx->cached_net_id = (uint32_t)engine.get_canonical_net_id(raw_nid);
                idx->is_bound = true;
            }
        }
        if (idx->msb_expr) bind_ast_expr(idx->msb_expr.get(), scope, engine);
        if (idx->lsb_expr) bind_ast_expr(idx->lsb_expr.get(), scope, engine);
        if (idx->bit_msb_expr) bind_ast_expr(idx->bit_msb_expr.get(), scope, engine);
        if (idx->bit_lsb_expr) bind_ast_expr(idx->bit_lsb_expr.get(), scope, engine);
        for (const auto& ex : idx->extra_indices) {
            if (ex) bind_ast_expr(ex.get(), scope, engine);
        }
        return;
    }

    if (auto mem = dynamic_cast<MemberAccessNode*>(expr)) {
        if (!mem->is_bound) {
            std::string full_name = mem->var_name + "." + mem->member_name;
            if (mem->index_expr) {
                if (auto num = dynamic_cast<const NumberNode*>(mem->index_expr.get())) {
                    full_name = mem->var_name + "[" + std::to_string(num->aval) + "]." + mem->member_name;
                }
            }
            size_t raw_nid = engine.get_net_id(scope, full_name);
            if (raw_nid != (size_t)-1) {
                mem->cached_net_id = (uint32_t)engine.get_canonical_net_id(raw_nid);
                mem->is_bound = true;
            }
        }
        if (mem->index_expr) bind_ast_expr(mem->index_expr.get(), scope, engine);
        return;
    }

    if (auto un = dynamic_cast<UnaryOpNode*>(expr)) {
        bind_ast_expr(un->operand.get(), scope, engine);
        return;
    }

    if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
        bind_ast_expr(bin->lhs.get(), scope, engine);
        bind_ast_expr(bin->rhs.get(), scope, engine);
        return;
    }

    if (auto tern = dynamic_cast<TernaryOpNode*>(expr)) {
        bind_ast_expr(tern->condition.get(), scope, engine);
        bind_ast_expr(tern->true_expr.get(), scope, engine);
        bind_ast_expr(tern->false_expr.get(), scope, engine);
        return;
    }

    if (auto cat = dynamic_cast<ConcatNode*>(expr)) {
        for (const auto& elem : cat->elements) {
            bind_ast_expr(elem.get(), scope, engine);
        }
        return;
    }

    if (auto ucat = dynamic_cast<UnpackedConcatNode*>(expr)) {
        for (const auto& elem : ucat->elements) {
            bind_ast_expr(elem.get(), scope, engine);
        }
        return;
    }

    if (auto pm = dynamic_cast<PatternMemberNode*>(expr)) {
        if (pm->value) bind_ast_expr(pm->value.get(), scope, engine);
        return;
    }

    if (auto rep = dynamic_cast<ReplicateNode*>(expr)) {
        bind_ast_expr(rep->count_expr.get(), scope, engine);
        bind_ast_expr(rep->target_expr.get(), scope, engine);
        return;
    }

    if (auto sc = dynamic_cast<StreamingConcatNode*>(expr)) {
        if (sc->slice_size) bind_ast_expr(sc->slice_size.get(), scope, engine);
        for (const auto& elem : sc->stream_items) {
            bind_ast_expr(elem.get(), scope, engine);
        }
        return;
    }

    if (auto fn = dynamic_cast<FunctionCallNode*>(expr)) {
        for (const auto& arg : fn->args) {
            bind_ast_expr(arg.get(), scope, engine);
        }
        return;
    }

    if (auto mc = dynamic_cast<MethodCallNode*>(expr)) {
        for (const auto& arg : mc->args) bind_ast_expr(arg.get(), scope, engine);
        if (mc->with_expr) bind_ast_expr(mc->with_expr.get(), scope, engine);
        return;
    }

    if (auto cast_expr = dynamic_cast<CastNode*>(expr)) {
        bind_ast_expr(cast_expr->expr.get(), scope, engine);
        return;
    }

    if (auto scast = dynamic_cast<StaticCastExprNode*>(expr)) {
        bind_ast_expr(scast->expr.get(), scope, engine);
        return;
    }

    if (auto sys_cast = dynamic_cast<SysCastCallNode*>(expr)) {
        if (sys_cast->dest_expr) bind_ast_expr(sys_cast->dest_expr.get(), scope, engine);
        if (sys_cast->src_expr) bind_ast_expr(sys_cast->src_expr.get(), scope, engine);
        return;
    }

    if (auto ins = dynamic_cast<InsideNode*>(expr)) {
        if (ins->target) bind_ast_expr(ins->target.get(), scope, engine);
        for (const auto& r : ins->ranges) {
            if (r.low) bind_ast_expr(r.low.get(), scope, engine);
            if (r.high) bind_ast_expr(r.high.get(), scope, engine);
        }
        return;
    }

    if (auto clog = dynamic_cast<Clog2Node*>(expr)) {
        bind_ast_expr(clog->operand.get(), scope, engine);
        return;
    }

    if (auto cb = dynamic_cast<CountBitsNode*>(expr)) {
        if (cb->target_expr) bind_ast_expr(cb->target_expr.get(), scope, engine);
        for (const auto& c : cb->control_bits) {
            bind_ast_expr(c.get(), scope, engine);
        }
        return;
    }

    if (auto vi = dynamic_cast<VectorIntrinsicNode*>(expr)) {
        bind_ast_expr(vi->operand.get(), scope, engine);
        return;
    }

    if (auto sf = dynamic_cast<SFormatfNode*>(expr)) {
        for (const auto& arg : sf->args) {
            bind_ast_expr(arg.get(), scope, engine);
        }
        return;
    }

    if (auto aq = dynamic_cast<ArrayQueryNode*>(expr)) {
        if (aq->array_expr) bind_ast_expr(aq->array_expr.get(), scope, engine);
        return;
    }

    if (auto adq = dynamic_cast<ArrayDimensionQueryNode*>(expr)) {
        if (adq->target) bind_ast_expr(adq->target.get(), scope, engine);
        return;
    }

    if (auto abq = dynamic_cast<ArrayBoundQueryNode*>(expr)) {
        if (abq->target) bind_ast_expr(abq->target.get(), scope, engine);
        if (abq->dimension_expr) bind_ast_expr(abq->dimension_expr.get(), scope, engine);
        return;
    }

    if (auto lc = dynamic_cast<LetCallNode*>(expr)) {
        for (const auto& a : lc->args) bind_ast_expr(a.get(), scope, engine);
        return;
    }
}

static void bind_ast_stmt(StatementNode* stmt, const std::string& scope, const Engine& engine) {
    if (!stmt) return;

    if (auto if_s = dynamic_cast<IfStatementNode*>(stmt)) {
        bind_ast_expr(if_s->condition.get(), scope, engine);
        for (const auto& s : if_s->then_branch) bind_ast_stmt(s.get(), scope, engine);
        for (const auto& s : if_s->else_branch) bind_ast_stmt(s.get(), scope, engine);
        return;
    }

    if (auto blk = dynamic_cast<BlockStatementNode*>(stmt)) {
        for (const auto& s : blk->statements) bind_ast_stmt(s.get(), scope, engine);
        return;
    }

    if (auto cs = dynamic_cast<CaseStatementNode*>(stmt)) {
        bind_ast_expr(cs->selector.get(), scope, engine);
        for (const auto& item : cs->items) {
            if (item.match_expr) bind_ast_expr(item.match_expr.get(), scope, engine);
            for (const auto& me : item.match_exprs) bind_ast_expr(me.get(), scope, engine);
            for (const auto& s : item.statements) bind_ast_stmt(s.get(), scope, engine);
        }
        return;
    }

    if (auto for_s = dynamic_cast<ForStatementNode*>(stmt)) {
        bind_ast_stmt(for_s->init_stmt.get(), scope, engine);
        bind_ast_expr(for_s->condition.get(), scope, engine);
        bind_ast_stmt(for_s->step_stmt.get(), scope, engine);
        for (const auto& s : for_s->body) bind_ast_stmt(s.get(), scope, engine);
        return;
    }

    if (auto fe = dynamic_cast<ForeachStatementNode*>(stmt)) {
        for (const auto& s : fe->body) bind_ast_stmt(s.get(), scope, engine);
        return;
    }

    if (auto wh = dynamic_cast<WhileStatementNode*>(stmt)) {
        bind_ast_expr(wh->condition.get(), scope, engine);
        for (const auto& s : wh->body) bind_ast_stmt(s.get(), scope, engine);
        return;
    }

    if (auto rep = dynamic_cast<RepeatStatementNode*>(stmt)) {
        bind_ast_expr(rep->count_expr.get(), scope, engine);
        for (const auto& s : rep->body) bind_ast_stmt(s.get(), scope, engine);
        return;
    }

    if (auto fo = dynamic_cast<ForeverStatementNode*>(stmt)) {
        for (const auto& s : fo->body) bind_ast_stmt(s.get(), scope, engine);
        return;
    }

    if (auto ba = dynamic_cast<BlockingAssignNode*>(stmt)) {
        bind_ast_expr(ba->rhs.get(), scope, engine);
        if (ba->bit_idx) bind_ast_expr(ba->bit_idx.get(), scope, engine);
        if (ba->bit_lsb) bind_ast_expr(ba->bit_lsb.get(), scope, engine);
        if (ba->bit_idx2) bind_ast_expr(ba->bit_idx2.get(), scope, engine);
        if (ba->bit_lsb2) bind_ast_expr(ba->bit_lsb2.get(), scope, engine);

        if (ba->lhs_targets.size() == 1 && !ba->bit_idx && !ba->stream_lhs && ba->intra_delay == 0) {
            const std::string& tname = ba->lhs_targets[0];
            if (!engine.is_excluded_from_net_binding(tname, scope)) {
                size_t raw_nid = engine.get_net_id(scope, tname);
                if (raw_nid != (size_t)-1) {
                    size_t canon = engine.get_canonical_net_id(raw_nid);
                    ba->cached_net_id = (uint32_t)canon;
                    ba->is_bound = true;
                }
            }
        }
        return;
    }

    if (auto nba = dynamic_cast<NonBlockingAssignNode*>(stmt)) {
        bind_ast_expr(nba->rhs.get(), scope, engine);
        if (nba->bit_idx) bind_ast_expr(nba->bit_idx.get(), scope, engine);
        if (nba->bit_lsb) bind_ast_expr(nba->bit_lsb.get(), scope, engine);
        if (nba->bit_idx2) bind_ast_expr(nba->bit_idx2.get(), scope, engine);
        if (nba->bit_lsb2) bind_ast_expr(nba->bit_lsb2.get(), scope, engine);

        if (!nba->bit_idx && !nba->has_intra_event && nba->intra_delay == 0 && !nba->stream_lhs) {
            const std::string& tname = nba->target;
            if (!engine.is_excluded_from_net_binding(tname, scope)) {
                size_t raw_nid = engine.get_net_id(scope, tname);
                if (raw_nid != (size_t)-1) {
                    size_t canon = engine.get_canonical_net_id(raw_nid);
                    nba->cached_net_id = (uint32_t)canon;
                    nba->is_bound = true;
                }
            }
        }
        return;
    }

    if (auto mem = dynamic_cast<MemAssignNode*>(stmt)) {
        bind_ast_expr(mem->addr_expr.get(), scope, engine);
        bind_ast_expr(mem->rhs.get(), scope, engine);
        return;
    }

    if (auto pa = dynamic_cast<ProceduralAssignNode*>(stmt)) {
        bind_ast_expr(pa->rhs.get(), scope, engine);
        return;
    }

    if (auto ret = dynamic_cast<ReturnStatementNode*>(stmt)) {
        if (ret->expr) bind_ast_expr(ret->expr.get(), scope, engine);
        return;
    }

    if (auto tc = dynamic_cast<TaskCallNode*>(stmt)) {
        for (const auto& arg : tc->args) bind_ast_expr(arg.get(), scope, engine);
        return;
    }

    if (auto disp = dynamic_cast<DisplayStatementNode*>(stmt)) {
        for (const auto& arg : disp->arg_exprs) bind_ast_expr(arg.get(), scope, engine);
        return;
    }

    if (auto sev = dynamic_cast<SeverityTaskStatementNode*>(stmt)) {
        if (sev->finish_number) bind_ast_expr(sev->finish_number.get(), scope, engine);
        for (const auto& arg : sev->arg_exprs) bind_ast_expr(arg.get(), scope, engine);
        return;
    }

    if (auto ev_ctl = dynamic_cast<EventControlStatementNode*>(stmt)) {
        for (const auto& idx : ev_ctl->index_exprs) {
            if (idx) bind_ast_expr(idx.get(), scope, engine);
        }
        return;
    }

    if (auto trig = dynamic_cast<EventTriggerStatementNode*>(stmt)) {
        for (const auto& idx : trig->index_exprs) {
            if (idx) bind_ast_expr(idx.get(), scope, engine);
        }
        return;
    }

    if (auto wc = dynamic_cast<WaitConditionStatementNode*>(stmt)) {
        if (wc->condition) bind_ast_expr(wc->condition.get(), scope, engine);
        if (wc->body) bind_ast_stmt(wc->body.get(), scope, engine);
        return;
    }

    if (auto fork_s = dynamic_cast<ForkStatementNode*>(stmt)) {
        for (const auto& s : fork_s->branches) bind_ast_stmt(s.get(), scope, engine);
        return;
    }

    if (auto delay_s = dynamic_cast<DelayStatementNode*>(stmt)) {
        if (delay_s->delay_expr) bind_ast_expr(delay_s->delay_expr.get(), scope, engine);
        return;
    }

    if (auto vd = dynamic_cast<VarDeclStatementNode*>(stmt)) {
        if (vd->init_expr) bind_ast_expr(vd->init_expr.get(), scope, engine);
        return;
    }

    if (auto sf = dynamic_cast<SFormatStatementNode*>(stmt)) {
        if (sf->target_expr) bind_ast_expr(sf->target_expr.get(), scope, engine);
        if (sf->format_expr) bind_ast_expr(sf->format_expr.get(), scope, engine);
        for (const auto& arg : sf->arg_exprs) bind_ast_expr(arg.get(), scope, engine);
        return;
    }

    if (auto vc = dynamic_cast<VoidCastStatementNode*>(stmt)) {
        if (vc->expr) bind_ast_expr(vc->expr.get(), scope, engine);
        return;
    }

    if (auto sc = dynamic_cast<SysCastStatementNode*>(stmt)) {
        if (sc->dest_expr) bind_ast_expr(sc->dest_expr.get(), scope, engine);
        if (sc->src_expr) bind_ast_expr(sc->src_expr.get(), scope, engine);
        return;
    }

    if (auto r = dynamic_cast<RandomizeStatementNode*>(stmt)) {
        if (r->call) bind_ast_expr(r->call.get(), scope, engine);
        return;
    }
}

static bool extract_fast_assignments_from_stmts(const std::vector<std::unique_ptr<StatementNode>>& stmts,
                                                std::vector<FastNetAssign>& out_fast) {
    if (stmts.empty()) return false;
    for (const auto& s_ptr : stmts) {
        StatementNode* s = s_ptr.get();
        if (!s) return false;
        if (auto blk = dynamic_cast<BlockStatementNode*>(s)) {
            if (!extract_fast_assignments_from_stmts(blk->statements, out_fast)) {
                return false;
            }
            continue;
        }
        if (auto nba = dynamic_cast<NonBlockingAssignNode*>(s)) {
            if (nba->is_bound && nba->cached_net_id != UINT32_MAX && !nba->bit_idx && !nba->has_intra_event && nba->intra_delay == 0 && !nba->stream_lhs) {
                if (auto rhs_id = dynamic_cast<IdentifierNode*>(nba->rhs.get())) {
                    if (rhs_id->is_bound && rhs_id->cached_net_id != UINT32_MAX) {
                        FastNetAssign fa;
                        fa.dst_net_id = nba->cached_net_id;
                        fa.src_net_id = rhs_id->cached_net_id;
                        fa.is_nba = true;
                        out_fast.push_back(fa);
                        continue;
                    }
                }
            }
            return false;
        }
        if (auto ba = dynamic_cast<BlockingAssignNode*>(s)) {
            if (ba->is_bound && ba->cached_net_id != UINT32_MAX && ba->lhs_targets.size() == 1 && !ba->bit_idx && !ba->stream_lhs && ba->intra_delay == 0) {
                if (auto rhs_id = dynamic_cast<IdentifierNode*>(ba->rhs.get())) {
                    if (rhs_id->is_bound && rhs_id->cached_net_id != UINT32_MAX) {
                        FastNetAssign fa;
                        fa.dst_net_id = ba->cached_net_id;
                        fa.src_net_id = rhs_id->cached_net_id;
                        fa.is_nba = false;
                        out_fast.push_back(fa);
                        continue;
                    }
                }
            }
            return false;
        }
        return false;
    }
    return !out_fast.empty();
}

void Engine::bind_all_ast_nodes() {
    for (auto& rca : resolved_continuous_assigns) {
        for (auto& drv : rca.drivers) {
            if (drv.is_gate) {
                if (drv.gate_idx < gates.size()) {
                    auto& g = gates[drv.gate_idx];
                    for (auto in_expr : g.in_exprs) {
                        bind_ast_expr(in_expr, g.scope, *this);
                    }
                }
            } else if (drv.rhs) {
                bind_ast_expr(drv.rhs, drv.scope, *this);
            }
        }
        if (rca.drivers.size() == 1 && !rca.drivers[0].is_gate && rca.drivers[0].rhs) {
            if (rca.target.net_id < netlist.size() &&
                (netlist[rca.target.net_id].type_name == "real" || netlist[rca.target.net_id].type_name == "shortreal")) {
                // Real targets should be evaluated via evaluator
            } else {
                uint32_t target_w = rca.target.width;
                bool target_s = (rca.target.net_id < netlist.size()) ? netlist[rca.target.net_id].is_signed : false;
                rca.compiled_expr = BytecodeCompiler::compile(rca.drivers[0].rhs, rca.drivers[0].scope, *this, target_w, target_s);
            }
        }
    }

    for (auto& mca : memory_continuous_assigns) {
        if (mca.rhs) bind_ast_expr(mca.rhs.get(), mca.scope, *this);
        if (mca.index_expr) bind_ast_expr(mca.index_expr.get(), mca.scope, *this);
    }

    for (auto& bp : processes) {
        if (!bp.proc) continue;
        for (auto& s : bp.proc->body) {
            bind_ast_stmt(s.get(), bp.scope, *this);
        }

        std::vector<FastNetAssign> fast_list;
        if (extract_fast_assignments_from_stmts(bp.proc->body, fast_list)) {
            bp.fast_assignments = std::move(fast_list);
            bp.is_pure_fast_proc = true;
        } else if (bp.proc->type == ProcessType::COMBINATIONAL || bp.proc->is_always_comb || bp.proc->is_always_latch) {
            bp.is_bytecode_compiled = BytecodeCompiler::compile_process(bp, *this);
        }
    }

    topological_order = DependencyGraph::build_and_sort_rcas(resolved_continuous_assigns, *this);

    bool do_native_codegen = enable_native_codegen || (std::getenv("CAS_NATIVE_CODEGEN") != nullptr);
    if (do_native_codegen) {
        std::string out_path = native_codegen_output_path;
        if (const char* env_path = std::getenv("CAS_NATIVE_CODEGEN_PATH")) {
            out_path = env_path;
        }
        bool gen_ok = NativeCodeGen::generate_comb_source(out_path, resolved_continuous_assigns, processes, *this);
        if (gen_ok) {
            std::string so_path = "/tmp/cas_native_comb.so";
#ifdef _WIN32
            so_path = "cas_native_comb.so";
#endif
            if (const char* env_so = std::getenv("CAS_NATIVE_SO_PATH")) {
                so_path = env_so;
            }
            bool jit_ok = NativeJIT::compile_and_load(out_path, so_path, scheduler.native_dispatch);
            if (jit_ok) {
                size_t active_rcas = 0;
                for (size_t i = 0; i < scheduler.native_dispatch.rca_count; ++i) {
                    if (scheduler.native_dispatch.rca_table[i] != nullptr) active_rcas++;
                }
                size_t active_procs = 0;
                for (size_t i = 0; i < scheduler.native_dispatch.proc_count; ++i) {
                    if (scheduler.native_dispatch.proc_table[i] != nullptr) active_procs++;
                }
                size_t native_seq_count = 0;
                for (const auto& bp : processes) {
                    if (bp.is_native_seq) native_seq_count++;
                }
                std::cout << "[Native JIT] Successfully compiled and dynamically linked "
                          << active_rcas << " RCAs (" << (scheduler.native_dispatch.topological_comb_fn ? "Topologically Flattened" : "Individual") << "), "
                          << active_procs << " Comb Procs, and " << native_seq_count << " Seq Procs into execution kernel.\n";
            } else {
                std::cout << "[Native JIT] Warning: compile_and_load failed, falling back to bytecode engine.\n";
            }

            if (LlvmJIT::is_available()) {
                bool llvm_ok = LlvmJIT::compile_and_bind(resolved_continuous_assigns, processes, *this, scheduler.llvm_dispatch);
                if (llvm_ok) {
                    std::cout << "[LLVM ORC JIT] In-memory JIT execution kernel bound to StratifiedScheduler.\n";
                }
            }
        } else {
            std::cout << "[NativeCodeGen] Failed to generate " << out_path << "\n";
        }
    }
}

void Engine::build_fanout_graph() {
    fanout.resize(netlist.size());
    fanout.mem_reading_rcas.clear();
    fanout.mem_reading_comb_procs.clear();
    fanout.mem_reading_mcas.clear();

    // 1. Build net_to_rca
    for (size_t rca_idx = 0; rca_idx < resolved_continuous_assigns.size(); ++rca_idx) {
        const auto& rca = resolved_continuous_assigns[rca_idx];
        std::vector<uint32_t> read_nets;
        bool reads_mem = false;
        if (rca.compiled_expr.is_compiled) {
            for (const auto& inst : rca.compiled_expr.instructions) {
                if (inst.op == Opcode::DONE) break;
                if (inst.op == Opcode::PUSH_NET_U64 || inst.op == Opcode::PUSH_NET_VAL) {
                    read_nets.push_back((uint32_t)get_canonical_net_id(inst.arg0));
                }
            }
        } else {
            for (const auto& drv : rca.drivers) {
                if (drv.is_gate) {
                    if (drv.gate_idx < gates.size()) {
                        const auto& g = gates[drv.gate_idx];
                        for (auto in_expr : g.in_exprs) {
                            collect_read_nets(in_expr, g.scope, *this, read_nets, reads_mem);
                        }
                    }
                } else if (drv.rhs) {
                    collect_read_nets(drv.rhs, drv.scope, *this, read_nets, reads_mem);
                }
            }
        }
        std::sort(read_nets.begin(), read_nets.end());
        read_nets.erase(std::unique(read_nets.begin(), read_nets.end()), read_nets.end());

        if (reads_mem) {
            fanout.mem_reading_rcas.push_back((uint32_t)rca_idx);
        }

        for (uint32_t nid : read_nets) {
            if (nid < fanout.net_to_rca.size()) {
                fanout.net_to_rca[nid].push_back((uint32_t)rca_idx);
            }
        }
    }

    // 2. Build net_to_comb_proc
    for (size_t pid = 0; pid < processes.size(); ++pid) {
        const auto& bp = processes[pid];
        if (!bp.proc) continue;
        if (bp.proc->type != ProcessType::COMBINATIONAL && !bp.proc->is_always_comb && !bp.proc->is_always_latch) continue;

        std::vector<uint32_t> read_nets;
        bool reads_mem = false;
        for (const auto& sens : bp.proc->sensitivity_list) {
            size_t raw_nid = get_net_id(bp.scope, sens.signal_name);
            if (raw_nid != (size_t)-1) {
                size_t canon = get_canonical_net_id(raw_nid);
                read_nets.push_back((uint32_t)canon);
            }
        }
        std::unordered_set<uint32_t> written_nets;
        FuncSensContext sens_ctx;
        for (const auto& s : bp.proc->body) {
            collect_stmt_read_nets(s.get(), bp.scope, *this, read_nets, reads_mem, &sens_ctx);
            collect_stmt_write_nets(s.get(), bp.scope, *this, written_nets);
        }
        for (uint32_t tid : bp.comb_target_nets) {
            written_nets.insert((uint32_t)get_canonical_net_id(tid));
            if (tid < alias_members.size()) {
                for (size_t aid : alias_members[tid]) {
                    written_nets.insert((uint32_t)aid);
                }
            }
        }
        std::sort(read_nets.begin(), read_nets.end());
        read_nets.erase(std::unique(read_nets.begin(), read_nets.end()), read_nets.end());

        if (reads_mem) {
            fanout.mem_reading_comb_procs.push_back((uint32_t)pid);
        }

        for (uint32_t nid : read_nets) {
            // IEEE 1800-2023 9.2.2.2: Do not sensitize process to its own written targets
            if (written_nets.count(nid)) continue;
            if (nid < fanout.net_to_comb_proc.size()) {
                fanout.net_to_comb_proc[nid].push_back((uint32_t)pid);
            }
        }
    }

    // 3. Build net_to_mca
    for (size_t mca_idx = 0; mca_idx < memory_continuous_assigns.size(); ++mca_idx) {
        const auto& mca = memory_continuous_assigns[mca_idx];
        std::vector<uint32_t> read_nets;
        bool reads_mem = false;
        if (mca.rhs) collect_read_nets(mca.rhs.get(), mca.scope, *this, read_nets, reads_mem);
        if (mca.index_expr) collect_read_nets(mca.index_expr.get(), mca.scope, *this, read_nets, reads_mem);
        if (reads_mem) {
            fanout.mem_reading_mcas.push_back((uint32_t)mca_idx);
        }
        std::sort(read_nets.begin(), read_nets.end());
        read_nets.erase(std::unique(read_nets.begin(), read_nets.end()), read_nets.end());

        for (uint32_t nid : read_nets) {
            if (nid < fanout.net_to_mca.size()) {
                fanout.net_to_mca[nid].push_back((uint32_t)mca_idx);
            }
        }
    }

    // 4. Mirror fanout to alias members
    for (size_t canon_nid = 0; canon_nid < alias_members.size(); ++canon_nid) {
        if (!alias_members[canon_nid].empty()) {
            for (size_t other_id : alias_members[canon_nid]) {
                if (other_id != canon_nid && other_id < fanout.net_to_rca.size()) {
                    fanout.net_to_rca[other_id] = fanout.net_to_rca[canon_nid];
                    fanout.net_to_comb_proc[other_id] = fanout.net_to_comb_proc[canon_nid];
                    if (other_id < fanout.net_to_mca.size()) {
                        fanout.net_to_mca[other_id] = fanout.net_to_mca[canon_nid];
                    }
                }
            }
        }
    }
}

LogicVal Engine::dispatch_dpi_export(const std::string& c_name, const std::vector<LogicVal>& args) {
    std::string scope = DpiRegistry::get_active_scope_name();
    if (scope.empty()) {
        scope = active_scope_name;
    }
    if (scope.rfind("top.", 0) == 0) scope = scope.substr(4);
    else if (scope.rfind("TOP.", 0) == 0) scope = scope.substr(4);

    std::string sv_name = c_name;
    bool is_task = false;

    std::string scoped_export = scope.empty() ? c_name : (scope + "." + c_name);
    if (dpi_export_map.find(scoped_export) != dpi_export_map.end()) {
        sv_name = dpi_export_map[scoped_export]->sv_name;
        is_task = dpi_export_map[scoped_export]->is_task;
    } else if (scoped_export.rfind("top.", 0) == 0 && dpi_export_map.find(scoped_export.substr(4)) != dpi_export_map.end()) {
        sv_name = dpi_export_map[scoped_export.substr(4)]->sv_name;
        is_task = dpi_export_map[scoped_export.substr(4)]->is_task;
        scope = scope.substr(4);
    } else if (scoped_export.rfind("TOP.", 0) == 0 && dpi_export_map.find(scoped_export.substr(4)) != dpi_export_map.end()) {
        sv_name = dpi_export_map[scoped_export.substr(4)]->sv_name;
        is_task = dpi_export_map[scoped_export.substr(4)]->is_task;
        scope = scope.substr(4);
    } else if (dpi_export_map.find(c_name) != dpi_export_map.end()) {
        sv_name = dpi_export_map[c_name]->sv_name;
        is_task = dpi_export_map[c_name]->is_task;
    }

    std::string scoped_target = scope.empty() ? sv_name : (scope + "." + sv_name);
    FunctionNode* fn = nullptr;
    if (function_map.find(scoped_target) != function_map.end()) {
        fn = function_map[scoped_target];
    } else if (scoped_target.rfind("top.", 0) == 0 && function_map.find(scoped_target.substr(4)) != function_map.end()) {
        scoped_target = scoped_target.substr(4);
        fn = function_map[scoped_target];
    } else if (scoped_target.rfind("TOP.", 0) == 0 && function_map.find(scoped_target.substr(4)) != function_map.end()) {
        scoped_target = scoped_target.substr(4);
        fn = function_map[scoped_target];
    } else if (function_map.find(sv_name) != function_map.end()) {
        fn = function_map[sv_name];
    }

    if (fn) {
        bool is_real_ret = (fn->return_type_name == "real" || fn->return_type_name == "shortreal" ||
                            fn->return_type_name == "real_t" || fn->return_type_name == "shortreal_t");
        LogicVal ret = is_real_ret ? LogicVal::make_real(0.0) : LogicVal::make_known(fn->return_width, 0);
        bool ok = evaluator.get_expr_eval().eval_function(scoped_target, args, ret);
        if (!ok) {
            evaluator.get_expr_eval().eval_function(fn->name, args, ret);
        }
        scheduler.propagate_comb();
        return ret;
    }

    TaskNode* tn = nullptr;
    if (task_map.find(scoped_target) != task_map.end()) {
        tn = task_map[scoped_target];
    } else if (scoped_target.rfind("top.", 0) == 0 && task_map.find(scoped_target.substr(4)) != task_map.end()) {
        scoped_target = scoped_target.substr(4);
        tn = task_map[scoped_target];
    } else if (scoped_target.rfind("TOP.", 0) == 0 && task_map.find(scoped_target.substr(4)) != task_map.end()) {
        scoped_target = scoped_target.substr(4);
        tn = task_map[scoped_target];
    } else if (task_map.find(sv_name) != task_map.end()) {
        tn = task_map[sv_name];
    }

    if (tn) {
        std::string frame_name = (scope.empty() ? "" : (scope + ".")) + tn->name + "__call";
        evaluator.get_expr_eval().get_context().push_frame(frame_name);
        if (tn->ports.size() == 1 && tn->ports[0].width > 32 && args.size() > 1) {
            uint64_t combined_aval = 0;
            uint64_t combined_bval = 0;
            for (size_t k = 0; k < args.size() && k < 2; ++k) {
                combined_aval |= ((uint64_t)args[k].aval << (k * 32));
                combined_bval |= ((uint64_t)args[k].bval << (k * 32));
            }
            LogicVal wide_arg(tn->ports[0].width, combined_aval, combined_bval, tn->ports[0].is_signed);
            evaluator.get_expr_eval().get_context().declare_local(tn->ports[0].name, tn->ports[0].width, tn->ports[0].is_signed, tn->ports[0].type_name);
            evaluator.get_expr_eval().get_context().set_local_value(tn->ports[0].name, wide_arg);
        } else {
            for (size_t i = 0; i < tn->ports.size() && i < args.size(); ++i) {
                evaluator.get_expr_eval().get_context().declare_local(tn->ports[i].name, tn->ports[i].width, tn->ports[i].is_signed, tn->ports[i].type_name);
                evaluator.get_expr_eval().get_context().set_local_value(tn->ports[i].name, args[i]);
            }
        }
        BoundProcess bp{scope.empty() ? tn->scope : scope, nullptr, 0, false};
        for (auto& s : tn->body) {
            evaluator.execute_statement(s.get(), bp);
            if (!scheduler.is_active()) break;
        }
        evaluator.get_expr_eval().get_context().pop_frame();
        scheduler.propagate_comb();
        return LogicVal(0, 0, 0);
    }

    throw std::runtime_error("[DPI-FNF] Exported routine '" + c_name + "' (SV target: '" + sv_name + "') not found in scope '" + scope + "'");
}

void Engine::elaborate(std::vector<std::unique_ptr<ModuleNode>>& modules,
                       const std::string& top_module_name,
                       std::ostream* debug_out) {
    auto t_start = std::chrono::high_resolution_clock::now();
    Elaborator elaborator;
    elaborator.top_param_overrides = top_param_overrides;
    auto elab_res = elaborator.elaborate(modules, top_module_name, debug_out);
    auto t_elab = std::chrono::high_resolution_clock::now();
    load_elaboration(std::move(elab_res));
    auto t_load = std::chrono::high_resolution_clock::now();
    std::cout << "[TIMING] elaborator.elaborate=" << std::chrono::duration<double, std::milli>(t_elab - t_start).count() << " ms"
              << " load_elaboration=" << std::chrono::duration<double, std::milli>(t_load - t_elab).count() << " ms" << std::endl;
}

std::string Engine::resolve_scoped_name(const std::string& scope, const std::string& name) const {
    size_t dot_pos = name.find('.');
    while (dot_pos != std::string::npos) {
        std::string prefix = name.substr(0, dot_pos);
        std::string suffix = name.substr(dot_pos + 1);
        std::string scoped_prefix = scope.empty() ? prefix : (scope + "." + prefix);

        auto it = interface_port_map.find(scoped_prefix);
        if (it != interface_port_map.end()) {
            std::string bound = it->second;
            while (interface_port_map.find(bound) != interface_port_map.end()) {
                bound = interface_port_map.at(bound);
            }
            return bound + "." + suffix;
        }
        auto it2 = interface_port_map.find(prefix);
        if (it2 != interface_port_map.end()) {
            std::string bound = it2->second;
            while (interface_port_map.find(bound) != interface_port_map.end()) {
                bound = interface_port_map.at(bound);
            }
            return bound + "." + suffix;
        }
        size_t bpos = prefix.find('[');
        if (bpos != std::string::npos) {
            std::string base_prefix = prefix.substr(0, bpos);
            std::string idx_str = prefix.substr(bpos);
            std::string scoped_base = scope.empty() ? base_prefix : (scope + "." + base_prefix);
            auto it_b = interface_port_map.find(scoped_base);
            if (it_b != interface_port_map.end()) {
                std::string bound = it_b->second + idx_str;
                while (interface_port_map.find(bound) != interface_port_map.end()) {
                    bound = interface_port_map.at(bound);
                }
                return bound + "." + suffix;
            }
            auto it_b2 = interface_port_map.find(base_prefix);
            if (it_b2 != interface_port_map.end()) {
                std::string bound = it_b2->second + idx_str;
                while (interface_port_map.find(bound) != interface_port_map.end()) {
                    bound = interface_port_map.at(bound);
                }
                return bound + "." + suffix;
            }
        }
        dot_pos = name.find('.', dot_pos + 1);
    }
    return scope.empty() ? name : (scope + "." + name);
}

size_t Engine::get_net_id(const std::string& scope, const std::string& name) const {
    static thread_local std::unordered_map<std::string, size_t> s_net_id_cache;
    std::string cache_key;
    cache_key.reserve(scope.size() + name.size() + 1);
    cache_key.append(scope);
    cache_key.push_back('\0');
    cache_key.append(name);
    auto it_c = s_net_id_cache.find(cache_key);
    if (it_c != s_net_id_cache.end()) return it_c->second;

    auto do_lookup = [&]() -> size_t {
        std::string cur_scope = scope;
        while (true) {
            std::string resolved = resolve_scoped_name(cur_scope, name);
            auto it = net_map.find(resolved);
            if (it != net_map.end()) return it->second;

            std::string scoped = cur_scope.empty() ? name : (cur_scope + "." + name);
            auto it3 = net_map.find(scoped);
            if (it3 != net_map.end()) return it3->second;

            if (!cur_scope.empty()) {
                std::string colon_scoped = cur_scope + "::" + name;
                auto itc = net_map.find(colon_scoped);
                if (itc != net_map.end()) return itc->second;
            }

            if (cur_scope.empty()) break;
            auto dot = cur_scope.rfind('.');
            if (dot == std::string::npos) cur_scope = "";
            else cur_scope = cur_scope.substr(0, dot);
        }

        auto it2 = net_map.find(name);
        if (it2 != net_map.end()) return it2->second;

        auto it_unit = net_map.find("$unit." + name);
        if (it_unit != net_map.end()) return it_unit->second;

        if (scope == "$unit" || scope.empty()) {
            for (const auto& pair : net_map) {
                auto d = pair.first.rfind('.');
                std::string uq = (d != std::string::npos) ? pair.first.substr(d + 1) : pair.first;
                if (uq == name) return pair.second;
            }
        }

        return (size_t)-1;
    };

    size_t res = do_lookup();
    s_net_id_cache[std::move(cache_key)] = res;
    return res;
}



void Engine::run(std::ostream* debug_out) {
    if (debug_out) {
        *debug_out << "[PHASE 4: Scheduler Initialization]\n";
        *debug_out << "  -> Hierarchical Nets Elaborated : " << netlist.size() << "\n";
        *debug_out << "  -> Slice Interconnects Bound    : " << slice_bindings.size() << "\n";
        *debug_out << "  -> Built-in Gates Instantiated  : " << gates.size() << "\n";
        *debug_out << "  -> Continuous Assigns Bound     : " << continuous_assigns.size() << "\n";
        *debug_out << "  -> Memory Arrays Allocated      : " << memory_map.size() << "\n";
        *debug_out << "  -> Memory Continuous Assigns    : " << memory_continuous_assigns.size() << "\n";
        *debug_out << "  -> Concurrent Processes Bound   : " << processes.size() << "\n";
        size_t total_rca_edges = 0;
        size_t nets_with_rca = 0;
        size_t max_rca = 0;
        for (const auto& rca_list : fanout.net_to_rca) {
            if (!rca_list.empty()) {
                nets_with_rca++;
                total_rca_edges += rca_list.size();
                max_rca = std::max(max_rca, rca_list.size());
            }
        }
        size_t total_proc_edges = 0;
        size_t nets_with_proc = 0;
        size_t max_proc = 0;
        for (const auto& proc_list : fanout.net_to_comb_proc) {
            if (!proc_list.empty()) {
                nets_with_proc++;
                total_proc_edges += proc_list.size();
                max_proc = std::max(max_proc, proc_list.size());
            }
        }
        *debug_out << "  -> Static Fanout Extracted      : " << total_rca_edges << " RCA edges (" << nets_with_rca << " nets, max deg " << max_rca << "), "
                   << total_proc_edges << " Comb Proc edges (" << nets_with_proc << " nets, max deg " << max_proc << ")\n";
        size_t compiled_rcas = 0;
        size_t pure_2state_rcas = 0;
        for (const auto& rca : resolved_continuous_assigns) {
            if (rca.compiled_expr.is_compiled) {
                compiled_rcas++;
                if (rca.compiled_expr.is_pure_2state_64) pure_2state_rcas++;
            }
        }
        *debug_out << "  -> Bytecode Compiled RCAs       : " << compiled_rcas << " / " << resolved_continuous_assigns.size()
                   << " (" << pure_2state_rcas << " pure 2-state 64-bit)\n";
        size_t total_comb_procs = 0;
        size_t compiled_procs = 0;
        size_t pure_2state_procs = 0;
        for (const auto& bp : processes) {
            if (bp.proc && (bp.proc->type == ProcessType::COMBINATIONAL || bp.proc->is_always_comb || bp.proc->is_always_latch)) {
                total_comb_procs++;
                if (bp.is_bytecode_compiled) {
                    compiled_procs++;
                    if (bp.compiled_body.is_pure_2state_64) pure_2state_procs++;
                }
            }
        }
        *debug_out << "  -> Bytecode Compiled Comb Procs : " << compiled_procs << " / " << total_comb_procs
                   << " (" << pure_2state_procs << " pure 2-state 64-bit)\n";
        *debug_out << "[PHASE 5: Simulation Runtime (Modular IEEE 1800 Kernel)]\n";
        *debug_out << "--------------------------------------------------------\n";
    }

    VpiRuntime::set_active_engine(this);
    VpiRuntime::dispatch_start_of_simulation(this);

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    std::string preload_elf = "";
    for (const auto& pa : plusargs) {
        if (pa.rfind("+PRELOAD=", 0) == 0) preload_elf = pa.substr(9);
        else if (pa.rfind("+elf_file=", 0) == 0 && preload_elf.empty()) preload_elf = pa.substr(10);
    }
    if (!preload_elf.empty()) {
        MemoryArray* dram_mem = nullptr;
        for (auto& kv : memory_map) {
            if (kv.first.find("i_sram") != std::string::npos && kv.first.find("sram") != std::string::npos && kv.first.find("i_tc_sram.sram") != std::string::npos) {
                dram_mem = &kv.second;
                break;
            }
        }
        if (!dram_mem) {
            for (auto& kv : memory_map) {
                if (kv.first.find("sram") != std::string::npos && kv.second.words.size() >= 1024) {
                    dram_mem = &kv.second;
                    break;
                }
            }
        }
        if (dram_mem) {
            int fd = open(preload_elf.c_str(), O_RDONLY);
            if (fd >= 0) {
                Elf64_Ehdr ehdr;
                if (read(fd, &ehdr, sizeof(ehdr)) == sizeof(ehdr)) {
                    if (ehdr.e_ident[EI_MAG0] == ELFMAG0 && ehdr.e_ident[EI_MAG1] == ELFMAG1 &&
                        ehdr.e_ident[EI_MAG2] == ELFMAG2 && ehdr.e_ident[EI_MAG3] == ELFMAG3) {
                        lseek(fd, ehdr.e_phoff, SEEK_SET);
                        std::vector<Elf64_Phdr> phdrs(ehdr.e_phnum);
                        if (read(fd, phdrs.data(), sizeof(Elf64_Phdr) * ehdr.e_phnum) == (ssize_t)(sizeof(Elf64_Phdr) * ehdr.e_phnum)) {
                            size_t loaded_bytes = 0;
                            for (const auto& ph : phdrs) {
                                if (ph.p_type == PT_LOAD && ph.p_filesz > 0 && ph.p_vaddr >= 0x80000000ULL) {
                                    uint64_t dram_offset = ph.p_vaddr - 0x80000000ULL;
                                    std::vector<uint8_t> seg_data(ph.p_filesz);
                                    lseek(fd, ph.p_offset, SEEK_SET);
                                    if (read(fd, seg_data.data(), ph.p_filesz) == (ssize_t)ph.p_filesz) {
                                        for (size_t k = 0; k < ph.p_filesz; ++k) {
                                            size_t w_idx = (dram_offset + k) / 8;
                                            size_t b_idx = (dram_offset + k) % 8;
                                            if (w_idx < dram_mem->words.size()) {
                                                dram_mem->words[w_idx].aval |= ((uint64_t)seg_data[k] << (b_idx * 8));
                                                dram_mem->words[w_idx].bval = 0;
                                                loaded_bytes++;
                                            }
                                        }
                                    }
                                }
                            }
                            dram_mem->change_counter++;
                            std::cout << "[CAS PRELOAD] Loaded " << loaded_bytes << " bytes from " << preload_elf << " into DRAM (" << dram_mem->words.size() << " words)\n" << std::flush;
                        }
                    }
                }
                close(fd);
            }
        }
    }
#endif

    scheduler.run_simulation();

    VpiRuntime::dispatch_end_of_simulation(this);

    if (vcd_dumper.is_enabled()) {
        std::string dumped_file = vcd_dumper.get_filename();
        vcd_dumper.close();
        if (allow_finish_msg) {
            std::cout << "[CAS VCD] Trace successfully dumped to: " << dumped_file << "\n";
        }
    }

    if (allow_finish_msg) {
        std::cout << "--------------------------------------------------------\n";
        std::cout << "[CAS SUMMARY] Simulation completed cleanly at T = " << scheduler.get_sim_time() << " ns.\n" << std::flush;
    }
}

EngineStats Engine::get_stats() {
    EngineStats s;
    s.net_count = netlist.size();
    s.net_cap = netlist.capacity();
    s.proc_count = processes.size();
    s.proc_cap = processes.capacity();
    s.slice_count = slice_bindings.size();
    s.slice_cap = slice_bindings.capacity();
    s.gate_count = gates.size();
    s.gate_cap = gates.capacity();
    s.ca_count = continuous_assigns.size();
    s.ca_cap = continuous_assigns.capacity();
    s.ram_count = memory_map.size();

    size_t words = 0;
    for (const auto& kv : memory_map) {
        words += kv.second.words.capacity();
    }
    s.ram_total_words = words;
    s.net_map_keys = net_map.size();
    s.net_map_buckets = net_map.bucket_count();
    s.net_map_load = net_map.load_factor();

    s.events_dispatched = scheduler.get_events_dispatched();
    s.delta_cycles = scheduler.get_delta_cycles();
    s.nba_commits = scheduler.get_nba_commits();
    return s;
}

size_t Engine::get_clocking_block_id(const std::string& cb_name, const std::string& scope) const {
    if (!cb_name.empty()) {
        std::string scoped = scope.empty() ? cb_name : (scope + "." + cb_name);
        auto it = clocking_block_map.find(scoped);
        if (it != clocking_block_map.end()) return it->second;
        auto it2 = clocking_block_map.find(cb_name);
        if (it2 != clocking_block_map.end()) return it2->second;
    } else {
        auto it = default_clocking_map.find(scope);
        if (it != default_clocking_map.end()) return it->second;
        if (!default_clocking_map.empty()) return default_clocking_map.begin()->second;
    }
    return (size_t)-1;
}

bool Engine::is_clocking_block(const std::string& name) const {
    return clocking_block_map.find(name) != clocking_block_map.end();
}

bool Engine::subroutine_has_blocking_timing_control(const StatementNode* stmt) const {
    if (!stmt) return false;
    if (dynamic_cast<const DelayStatementNode*>(stmt)) return true;
    if (dynamic_cast<const EventControlStatementNode*>(stmt)) return true;
    if (dynamic_cast<const CycleDelayStatementNode*>(stmt)) return true;
    if (dynamic_cast<const ForeverStatementNode*>(stmt)) return true;
    if (dynamic_cast<const WaitForkStatementNode*>(stmt)) return true;
    if (dynamic_cast<const WaitEventTriggeredStatementNode*>(stmt)) return true;

    if (auto block = dynamic_cast<const BlockStatementNode*>(stmt)) {
        for (const auto& s : block->statements) {
            if (subroutine_has_blocking_timing_control(s.get())) return true;
        }
    }
    if (auto if_stmt = dynamic_cast<const IfStatementNode*>(stmt)) {
        for (const auto& s : if_stmt->then_branch) {
            if (subroutine_has_blocking_timing_control(s.get())) return true;
        }
        for (const auto& s : if_stmt->else_branch) {
            if (subroutine_has_blocking_timing_control(s.get())) return true;
        }
    }
    if (auto case_stmt = dynamic_cast<const CaseStatementNode*>(stmt)) {
        for (const auto& item : case_stmt->items) {
            for (const auto& s : item.statements) {
                if (subroutine_has_blocking_timing_control(s.get())) return true;
            }
        }
    }
    if (auto for_stmt = dynamic_cast<const ForStatementNode*>(stmt)) {
        for (const auto& s : for_stmt->body) {
            if (subroutine_has_blocking_timing_control(s.get())) return true;
        }
    }
    if (auto wh_stmt = dynamic_cast<const WhileStatementNode*>(stmt)) {
        for (const auto& s : wh_stmt->body) {
            if (subroutine_has_blocking_timing_control(s.get())) return true;
        }
    }
    if (auto rep_stmt = dynamic_cast<const RepeatStatementNode*>(stmt)) {
        for (const auto& s : rep_stmt->body) {
            if (subroutine_has_blocking_timing_control(s.get())) return true;
        }
    }
    if (auto frk = dynamic_cast<const ForkStatementNode*>(stmt)) {
        for (const auto& s : frk->branches) {
            if (subroutine_has_blocking_timing_control(s.get())) return true;
        }
    }
    if (auto tc = dynamic_cast<const TaskCallNode*>(stmt)) {
        TaskNode* nested_task = nullptr;
        if (task_map.find(tc->task_name) != task_map.end()) nested_task = task_map.at(tc->task_name);
        if (nested_task) {
            for (const auto& s : nested_task->get_body()) {
                if (subroutine_has_blocking_timing_control(s.get())) return true;
            }
        }
    }
    return false;
}

void Engine::execute_sva_subroutine(const std::string& name, const std::vector<LogicVal>& args, const std::string& scope) {
    if (name == "$display") {
        if (!is_display_allowed()) return;
        std::cout << "[CAS $display] ";
        for (size_t i = 0; i < args.size(); ++i) {
            std::cout << args[i].aval << " ";
        }
        std::cout << "\n";
        return;
    }

    std::string scoped_name = scope.empty() ? name : (scope + "." + name);
    FunctionNode* target_fn = nullptr;
    std::string fn_lookup_name = "";
    if (function_map.find(scoped_name) != function_map.end()) {
        target_fn = function_map.at(scoped_name);
        fn_lookup_name = scoped_name;
    } else if (function_map.find(name) != function_map.end()) {
        target_fn = function_map.at(name);
        fn_lookup_name = name;
    }

    if (target_fn) {
        for (const auto& s : target_fn->body) {
            if (subroutine_has_blocking_timing_control(s.get())) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: sequence match subroutine '" + name + "' contains blocking timing controls per IEEE 1800-2023 section 16.11");
            }
        }
        LogicVal dummy;
        evaluator.get_expr_eval().eval_function(fn_lookup_name, args, dummy);
        return;
    }

    TaskNode* target_task = nullptr;
    if (task_map.find(scoped_name) != task_map.end()) {
        target_task = task_map.at(scoped_name);
    } else if (task_map.find(name) != task_map.end()) {
        target_task = task_map.at(name);
    }

    if (target_task) {
        for (const auto& s : target_task->get_body()) {
            if (subroutine_has_blocking_timing_control(s.get())) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: sequence match subroutine '" + name + "' contains blocking timing controls per IEEE 1800-2023 section 16.11");
            }
        }
        TaskActivationFrame frame(name, target_task);
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
            if (p.direction == "input" || p.direction == "inout") {
                if (i < args.size()) {
                    frame.local_frame.get_var(p.name)->value = LogicOps::cast_width(args[i], w, p.is_signed);
                }
            }
        }
        BoundProcess bp{target_task->scope.empty() ? scope : target_task->scope, nullptr, 0, false};
        bp.call_stack.push_back(std::move(frame));
        for (auto& s : target_task->get_body()) {
            evaluator.execute_statement(s.get(), bp);
        }
        return;
    }

    throw std::runtime_error("Unknown sequence match subroutine: " + name);
}

VirtualInterfaceHandle Engine::lookup_virtual_interface(const std::string& name, const std::string& scope, ClassObject* current_this, const BoundProcess* bp) {
    std::string clean_name = name;
    if (clean_name.rfind("this.", 0) == 0) {
        clean_name = clean_name.substr(5);
    } else if (clean_name.rfind("super.", 0) == 0) {
        clean_name = clean_name.substr(6);
    }

    // 1. Current 'this' object
    if (current_this) {
        if (current_this->has_virtual_interface(clean_name)) {
            auto h = current_this->get_virtual_interface(clean_name);
            h.is_valid_vif = true;
            return h;
        }
        if (current_this->has_property(clean_name)) {
            uint64_t hid = current_this->get_property(clean_name).aval;
            if (is_virtual_interface_handle(hid)) {
                auto h = get_vif_handle(hid);
                h.is_valid_vif = true;
                return h;
            }
        }
    }

    // 2. Active frame in Evaluator / ExprEvaluator
    if (evaluator.get_expr_eval().get_context().has_active_frame()) {
        const LocalVar* lv = evaluator.get_expr_eval().get_context().lookup_local(clean_name);
        if (lv) {
            if (lv->is_virtual_interface || lv->vif_handle != 0 || is_virtual_interface_handle(lv->value.aval)) {
                VirtualInterfaceHandle h;
                if (lv->vif_handle != 0) h = get_vif_handle(lv->vif_handle);
                else if (is_virtual_interface_handle(lv->value.aval)) h = get_vif_handle(lv->value.aval);
                else { h.iface_type = lv->interface_name; h.modport = lv->modport_name; }
                h.is_valid_vif = true;
                return h;
            }
        }
    }

    // 3. Process Call Stack
    if (bp && !bp->call_stack.empty()) {
        for (auto it = bp->call_stack.rbegin(); it != bp->call_stack.rend(); ++it) {
            const LocalVar* lv = it->local_frame.get_var(clean_name);
            if (lv) {
                if (lv->is_virtual_interface || lv->vif_handle != 0 || is_virtual_interface_handle(lv->value.aval)) {
                    VirtualInterfaceHandle h;
                    if (lv->vif_handle != 0) h = get_vif_handle(lv->vif_handle);
                    else if (is_virtual_interface_handle(lv->value.aval)) h = get_vif_handle(lv->value.aval);
                    else { h.iface_type = lv->interface_name; h.modport = lv->modport_name; }
                    h.is_valid_vif = true;
                    return h;
                }
            }
            if (it->this_obj) {
                if (it->this_obj->has_virtual_interface(clean_name)) {
                    auto h = it->this_obj->get_virtual_interface(clean_name);
                    h.is_valid_vif = true;
                    return h;
                }
                if (it->this_obj->has_property(clean_name)) {
                    uint64_t hid = it->this_obj->get_property(clean_name).aval;
                    if (is_virtual_interface_handle(hid)) {
                        auto h = get_vif_handle(hid);
                        h.is_valid_vif = true;
                        return h;
                    }
                }
            }
        }
    }

    // 4. Netlist (module-level virtual interface variable)
    size_t nid = get_net_id(scope, clean_name);
    if (nid != (size_t)-1) {
        if (netlist[nid].is_virtual_interface || is_virtual_interface_handle(netlist[nid].value.aval)) {
            VirtualInterfaceHandle h;
            if (is_virtual_interface_handle(netlist[nid].value.aval)) {
                h = get_vif_handle(netlist[nid].value.aval);
            } else {
                h.iface_type = netlist[nid].interface_name;
                h.modport = netlist[nid].modport_name;
            }
            h.is_valid_vif = true;
            return h;
        }
    }

    // 5. Check if clean_name itself is an interface instance path
    std::string scoped_path = scope.empty() ? clean_name : (scope + "." + clean_name);
    if (interface_instances.find(scoped_path) != interface_instances.end()) {
        uint64_t hid = const_cast<Engine*>(this)->get_or_create_vif_handle(scoped_path, "");
        auto h = get_vif_handle(hid);
        h.is_valid_vif = true;
        return h;
    }
    if (interface_instances.find(clean_name) != interface_instances.end()) {
        uint64_t hid = const_cast<Engine*>(this)->get_or_create_vif_handle(clean_name, "");
        auto h = get_vif_handle(hid);
        h.is_valid_vif = true;
        return h;
    }

    return VirtualInterfaceHandle();
}

size_t Engine::resolve_vif_net_id(const VirtualInterfaceHandle& vif, const std::string& member_name) {
    if (vif.inst_path.empty()) {
        throw std::runtime_error("[CAS-DTINPCIL] Null virtual interface dereference: attempted to access member '" + member_name + "' on uninitialized virtual interface handle per IEEE 1800-2023 section 25.9");
    }
    std::string full_net = vif.inst_path + "." + member_name;
    if (net_map.find(full_net) != net_map.end()) {
        return net_map.at(full_net);
    }
    if (net_map.find(member_name) != net_map.end()) {
        return net_map.at(member_name);
    }
    throw std::runtime_error("Virtual interface member not found in netlist: " + full_net);
}

uint64_t Engine::get_scope_timeunit_fs(const std::string& scope) const {
    std::string eff_tu = "";
    if (!scope.empty()) {
        auto it = timescale_map.find(scope);
        if (it != timescale_map.end() && !it->second.first.empty()) eff_tu = it->second.first;
    }
    if (eff_tu.empty() && !scope.empty()) {
        auto dot = scope.find('.');
        if (dot != std::string::npos) {
            std::string mod_n = scope.substr(0, dot);
            auto it = timescale_map.find(mod_n);
            if (it != timescale_map.end() && !it->second.first.empty()) eff_tu = it->second.first;
        }
    }
    if (eff_tu.empty()) {
        auto it = timescale_map.find("$unit");
        if (it != timescale_map.end() && !it->second.first.empty()) eff_tu = it->second.first;
    }
    if (eff_tu.empty()) {
        auto it = timescale_map.find("");
        if (it != timescale_map.end() && !it->second.first.empty()) eff_tu = it->second.first;
    }
    if (eff_tu.empty()) eff_tu = Lexer::global_time_unit_str;
    if (eff_tu.empty()) return global_prec_fs;
    uint64_t tu_fs = Lexer::parse_time_fs(eff_tu);
    return tu_fs > 0 ? tu_fs : global_prec_fs;
}

bool Engine::has_scope_timeunit(const std::string& scope) const {
    if (scopes_with_timeunit.count(scope)) return true;
    auto dot = scope.find('.');
    if (dot != std::string::npos) {
        if (scopes_with_timeunit.count(scope.substr(0, dot))) return true;
        auto last_dot = scope.rfind('.');
        if (last_dot != std::string::npos) {
            if (scopes_with_timeunit.count(scope.substr(last_dot + 1))) return true;
        }
    }
    return false;
}