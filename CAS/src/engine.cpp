#include "../include/engine.hpp"
#include "../include/kernel/logic_ops.hpp"

#include <algorithm>

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
    dynamic_array_map = std::move(elab_res.dynamic_array_map);
    assoc_array_map = std::move(elab_res.assoc_array_map);
    task_map = std::move(elab_res.task_map);
    param_map = std::move(elab_res.param_map);
    param_val_map = std::move(elab_res.param_val_map);
    processes = std::move(elab_res.processes);
    comb_target_net_ids = std::move(elab_res.comb_target_net_ids);
    function_map = std::move(elab_res.function_map);
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
}

static void collect_read_nets(const ExpressionNode* expr, const std::string& scope, const Engine& engine, std::vector<uint32_t>& out_net_ids, bool& out_reads_mem);

static void collect_read_nets(const ExpressionNode* expr, const std::string& scope, const Engine& engine, std::vector<uint32_t>& out_net_ids) {
    bool dummy = false;
    collect_read_nets(expr, scope, engine, out_net_ids, dummy);
}
static void collect_read_nets(const ExpressionNode* expr, const std::string& scope, const Engine& engine, std::vector<uint32_t>& out_net_ids, bool& out_reads_mem) {
    if (!expr) return;

    if (auto id = dynamic_cast<const IdentifierNode*>(expr)) {
        if (id->is_bound && id->cached_net_id != UINT32_MAX) {
            out_net_ids.push_back(id->cached_net_id);
            return;
        }
        if (engine.is_memory(scope, id->name)) {
            out_reads_mem = true;
        }
        size_t raw_nid = engine.get_net_id(scope, id->name);
        if (raw_nid != (size_t)-1) {
            size_t canon = engine.get_canonical_net_id(raw_nid);
            out_net_ids.push_back((uint32_t)canon);
        }
        return;
    }

    if (auto idx = dynamic_cast<const IndexSelectNode*>(expr)) {
        if (engine.is_memory(scope, idx->name)) {
            out_reads_mem = true;
        }
        size_t raw_nid = engine.get_net_id(scope, idx->name);
        if (raw_nid != (size_t)-1) {
            size_t canon = engine.get_canonical_net_id(raw_nid);
            out_net_ids.push_back((uint32_t)canon);
        }
        if (idx->msb_expr) collect_read_nets(idx->msb_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        if (idx->lsb_expr) collect_read_nets(idx->lsb_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        if (idx->bit_msb_expr) collect_read_nets(idx->bit_msb_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        if (idx->bit_lsb_expr) collect_read_nets(idx->bit_lsb_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto mem = dynamic_cast<const MemberAccessNode*>(expr)) {
        std::string full_name = mem->var_name + "." + mem->member_name;
        size_t raw_nid = engine.get_net_id(scope, full_name);
        if (raw_nid != (size_t)-1) {
            size_t canon = engine.get_canonical_net_id(raw_nid);
            out_net_ids.push_back((uint32_t)canon);
        } else {
            raw_nid = engine.get_net_id(scope, mem->var_name);
            if (raw_nid != (size_t)-1) {
                size_t canon = engine.get_canonical_net_id(raw_nid);
                out_net_ids.push_back((uint32_t)canon);
            }
        }
        if (mem->index_expr) collect_read_nets(mem->index_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto un = dynamic_cast<const UnaryOpNode*>(expr)) {
        collect_read_nets(un->operand.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto bin = dynamic_cast<const BinaryOpNode*>(expr)) {
        collect_read_nets(bin->lhs.get(), scope, engine, out_net_ids, out_reads_mem);
        collect_read_nets(bin->rhs.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto tern = dynamic_cast<const TernaryOpNode*>(expr)) {
        collect_read_nets(tern->condition.get(), scope, engine, out_net_ids, out_reads_mem);
        collect_read_nets(tern->true_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        collect_read_nets(tern->false_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto cat = dynamic_cast<const ConcatNode*>(expr)) {
        for (const auto& elem : cat->elements) {
            collect_read_nets(elem.get(), scope, engine, out_net_ids, out_reads_mem);
        }
        return;
    }

    if (auto ucat = dynamic_cast<const UnpackedConcatNode*>(expr)) {
        for (const auto& elem : ucat->elements) {
            collect_read_nets(elem.get(), scope, engine, out_net_ids, out_reads_mem);
        }
        return;
    }

    if (auto pm = dynamic_cast<const PatternMemberNode*>(expr)) {
        if (pm->value) collect_read_nets(pm->value.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto rep = dynamic_cast<const ReplicateNode*>(expr)) {
        collect_read_nets(rep->count_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        collect_read_nets(rep->target_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto sc = dynamic_cast<const StreamingConcatNode*>(expr)) {
        if (sc->slice_size) collect_read_nets(sc->slice_size.get(), scope, engine, out_net_ids, out_reads_mem);
        for (const auto& elem : sc->stream_items) {
            collect_read_nets(elem.get(), scope, engine, out_net_ids, out_reads_mem);
        }
        return;
    }

    if (auto fn = dynamic_cast<const FunctionCallNode*>(expr)) {
        for (const auto& arg : fn->args) {
            collect_read_nets(arg.get(), scope, engine, out_net_ids, out_reads_mem);
        }
        return;
    }

    if (auto mc = dynamic_cast<const MethodCallNode*>(expr)) {
        size_t raw_nid = engine.get_net_id(scope, mc->var_name);
        if (raw_nid != (size_t)-1) {
            size_t canon = engine.get_canonical_net_id(raw_nid);
            out_net_ids.push_back((uint32_t)canon);
        }
        for (const auto& arg : mc->args) collect_read_nets(arg.get(), scope, engine, out_net_ids, out_reads_mem);
        if (mc->with_expr) collect_read_nets(mc->with_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto cast_expr = dynamic_cast<const CastNode*>(expr)) {
        collect_read_nets(cast_expr->expr.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto scast = dynamic_cast<const StaticCastExprNode*>(expr)) {
        collect_read_nets(scast->expr.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto sys_cast = dynamic_cast<const SysCastCallNode*>(expr)) {
        if (sys_cast->dest_expr) collect_read_nets(sys_cast->dest_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        if (sys_cast->src_expr) collect_read_nets(sys_cast->src_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto ins = dynamic_cast<const InsideNode*>(expr)) {
        if (ins->target) collect_read_nets(ins->target.get(), scope, engine, out_net_ids, out_reads_mem);
        for (const auto& r : ins->ranges) {
            if (r.low) collect_read_nets(r.low.get(), scope, engine, out_net_ids, out_reads_mem);
            if (r.high) collect_read_nets(r.high.get(), scope, engine, out_net_ids, out_reads_mem);
        }
        return;
    }

    if (auto clog = dynamic_cast<const Clog2Node*>(expr)) {
        collect_read_nets(clog->operand.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto cb = dynamic_cast<const CountBitsNode*>(expr)) {
        if (cb->target_expr) collect_read_nets(cb->target_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        for (const auto& c : cb->control_bits) {
            collect_read_nets(c.get(), scope, engine, out_net_ids, out_reads_mem);
        }
        return;
    }

    if (auto vi = dynamic_cast<const VectorIntrinsicNode*>(expr)) {
        collect_read_nets(vi->operand.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto sf = dynamic_cast<const SFormatfNode*>(expr)) {
        for (const auto& arg : sf->args) {
            collect_read_nets(arg.get(), scope, engine, out_net_ids, out_reads_mem);
        }
        return;
    }

    if (auto aq = dynamic_cast<const ArrayQueryNode*>(expr)) {
        if (aq->array_expr) collect_read_nets(aq->array_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto adq = dynamic_cast<const ArrayDimensionQueryNode*>(expr)) {
        if (adq->target) collect_read_nets(adq->target.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto abq = dynamic_cast<const ArrayBoundQueryNode*>(expr)) {
        if (abq->target) collect_read_nets(abq->target.get(), scope, engine, out_net_ids, out_reads_mem);
        if (abq->dimension_expr) collect_read_nets(abq->dimension_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto lc = dynamic_cast<const LetCallNode*>(expr)) {
        for (const auto& a : lc->args) collect_read_nets(a.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }
}

static void collect_stmt_read_nets(const StatementNode* stmt, const std::string& scope, const Engine& engine, std::vector<uint32_t>& out_net_ids, bool& out_reads_mem) {
    if (!stmt) return;

    if (auto if_s = dynamic_cast<const IfStatementNode*>(stmt)) {
        collect_read_nets(if_s->condition.get(), scope, engine, out_net_ids, out_reads_mem);
        for (const auto& s : if_s->then_branch) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem);
        for (const auto& s : if_s->else_branch) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto blk = dynamic_cast<const BlockStatementNode*>(stmt)) {
        for (const auto& s : blk->statements) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto cs = dynamic_cast<const CaseStatementNode*>(stmt)) {
        collect_read_nets(cs->selector.get(), scope, engine, out_net_ids, out_reads_mem);
        for (const auto& item : cs->items) {
            if (item.match_expr) collect_read_nets(item.match_expr.get(), scope, engine, out_net_ids, out_reads_mem);
            for (const auto& me : item.match_exprs) collect_read_nets(me.get(), scope, engine, out_net_ids, out_reads_mem);
            for (const auto& s : item.statements) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem);
        }
        return;
    }

    if (auto for_s = dynamic_cast<const ForStatementNode*>(stmt)) {
        collect_stmt_read_nets(for_s->init_stmt.get(), scope, engine, out_net_ids, out_reads_mem);
        collect_read_nets(for_s->condition.get(), scope, engine, out_net_ids, out_reads_mem);
        collect_stmt_read_nets(for_s->step_stmt.get(), scope, engine, out_net_ids, out_reads_mem);
        for (const auto& s : for_s->body) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto fe = dynamic_cast<const ForeachStatementNode*>(stmt)) {
        size_t raw_nid = engine.get_net_id(scope, fe->array_name);
        if (raw_nid != (size_t)-1) {
            size_t canon = engine.get_canonical_net_id(raw_nid);
            out_net_ids.push_back((uint32_t)canon);
        }
        for (const auto& s : fe->body) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto wh = dynamic_cast<const WhileStatementNode*>(stmt)) {
        collect_read_nets(wh->condition.get(), scope, engine, out_net_ids, out_reads_mem);
        for (const auto& s : wh->body) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto rep = dynamic_cast<const RepeatStatementNode*>(stmt)) {
        collect_read_nets(rep->count_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        for (const auto& s : rep->body) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto fo = dynamic_cast<const ForeverStatementNode*>(stmt)) {
        for (const auto& s : fo->body) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto ba = dynamic_cast<const BlockingAssignNode*>(stmt)) {
        collect_read_nets(ba->rhs.get(), scope, engine, out_net_ids, out_reads_mem);
        if (ba->bit_idx) collect_read_nets(ba->bit_idx.get(), scope, engine, out_net_ids, out_reads_mem);
        if (ba->bit_lsb) collect_read_nets(ba->bit_lsb.get(), scope, engine, out_net_ids, out_reads_mem);
        if (ba->bit_idx2) collect_read_nets(ba->bit_idx2.get(), scope, engine, out_net_ids, out_reads_mem);
        if (ba->bit_lsb2) collect_read_nets(ba->bit_lsb2.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto nba = dynamic_cast<const NonBlockingAssignNode*>(stmt)) {
        collect_read_nets(nba->rhs.get(), scope, engine, out_net_ids, out_reads_mem);
        if (nba->bit_idx) collect_read_nets(nba->bit_idx.get(), scope, engine, out_net_ids, out_reads_mem);
        if (nba->bit_lsb) collect_read_nets(nba->bit_lsb.get(), scope, engine, out_net_ids, out_reads_mem);
        if (nba->bit_idx2) collect_read_nets(nba->bit_idx2.get(), scope, engine, out_net_ids, out_reads_mem);
        if (nba->bit_lsb2) collect_read_nets(nba->bit_lsb2.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto mem = dynamic_cast<const MemAssignNode*>(stmt)) {
        collect_read_nets(mem->addr_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        collect_read_nets(mem->rhs.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto pa = dynamic_cast<const ProceduralAssignNode*>(stmt)) {
        collect_read_nets(pa->rhs.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto ret = dynamic_cast<const ReturnStatementNode*>(stmt)) {
        if (ret->expr) collect_read_nets(ret->expr.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto tc = dynamic_cast<const TaskCallNode*>(stmt)) {
        for (const auto& arg : tc->args) collect_read_nets(arg.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto disp = dynamic_cast<const DisplayStatementNode*>(stmt)) {
        for (const auto& arg : disp->arg_exprs) collect_read_nets(arg.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto sev = dynamic_cast<const SeverityTaskStatementNode*>(stmt)) {
        if (sev->finish_number) collect_read_nets(sev->finish_number.get(), scope, engine, out_net_ids, out_reads_mem);
        for (const auto& arg : sev->arg_exprs) collect_read_nets(arg.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto ev_ctl = dynamic_cast<const EventControlStatementNode*>(stmt)) {
        size_t raw_nid = engine.get_net_id(scope, ev_ctl->signal_name);
        if (raw_nid != (size_t)-1) {
            size_t canon = engine.get_canonical_net_id(raw_nid);
            out_net_ids.push_back((uint32_t)canon);
        }
        return;
    }

    if (auto fork_s = dynamic_cast<const ForkStatementNode*>(stmt)) {
        for (const auto& s : fork_s->branches) collect_stmt_read_nets(s.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto delay_s = dynamic_cast<const DelayStatementNode*>(stmt)) {
        if (delay_s->delay_expr) collect_read_nets(delay_s->delay_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto vd = dynamic_cast<const VarDeclStatementNode*>(stmt)) {
        if (vd->init_expr) collect_read_nets(vd->init_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto sf = dynamic_cast<const SFormatStatementNode*>(stmt)) {
        if (sf->target_expr) collect_read_nets(sf->target_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        if (sf->format_expr) collect_read_nets(sf->format_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        for (const auto& arg : sf->arg_exprs) collect_read_nets(arg.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto vc = dynamic_cast<const VoidCastStatementNode*>(stmt)) {
        if (vc->expr) collect_read_nets(vc->expr.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto sc = dynamic_cast<const SysCastStatementNode*>(stmt)) {
        if (sc->dest_expr) collect_read_nets(sc->dest_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        if (sc->src_expr) collect_read_nets(sc->src_expr.get(), scope, engine, out_net_ids, out_reads_mem);
        return;
    }

    if (auto r = dynamic_cast<const RandomizeStatementNode*>(stmt)) {
        if (r->call) collect_read_nets(r->call.get(), scope, engine, out_net_ids, out_reads_mem);
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
        if (!engine.is_excluded_from_net_binding(id->name, scope)) {
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
        if (idx->msb_expr) bind_ast_expr(idx->msb_expr.get(), scope, engine);
        if (idx->lsb_expr) bind_ast_expr(idx->lsb_expr.get(), scope, engine);
        if (idx->bit_msb_expr) bind_ast_expr(idx->bit_msb_expr.get(), scope, engine);
        if (idx->bit_lsb_expr) bind_ast_expr(idx->bit_lsb_expr.get(), scope, engine);
        return;
    }

    if (auto mem = dynamic_cast<MemberAccessNode*>(expr)) {
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
            uint32_t target_w = rca.target.width;
            bool target_s = (rca.target.net_id < netlist.size()) ? netlist[rca.target.net_id].is_signed : false;
            rca.compiled_expr = BytecodeCompiler::compile(rca.drivers[0].rhs, rca.drivers[0].scope, *this, target_w, target_s);
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
}

void Engine::build_fanout_graph() {
    fanout.resize(netlist.size());
    fanout.mem_reading_rcas.clear();
    fanout.mem_reading_comb_procs.clear();

    // 1. Build net_to_rca
    for (size_t rca_idx = 0; rca_idx < resolved_continuous_assigns.size(); ++rca_idx) {
        const auto& rca = resolved_continuous_assigns[rca_idx];
        std::vector<uint32_t> read_nets;
        bool reads_mem = false;
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
        for (const auto& s : bp.proc->body) {
            collect_stmt_read_nets(s.get(), bp.scope, *this, read_nets, reads_mem);
        }
        std::sort(read_nets.begin(), read_nets.end());
        read_nets.erase(std::unique(read_nets.begin(), read_nets.end()), read_nets.end());

        if (reads_mem) {
            fanout.mem_reading_comb_procs.push_back((uint32_t)pid);
        }

        for (uint32_t nid : read_nets) {
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

    std::string sv_name = c_name;
    bool is_task = false;

    std::string scoped_export = scope.empty() ? c_name : (scope + "." + c_name);
    if (dpi_export_map.find(scoped_export) != dpi_export_map.end()) {
        sv_name = dpi_export_map[scoped_export]->sv_name;
        is_task = dpi_export_map[scoped_export]->is_task;
    } else if (dpi_export_map.find(c_name) != dpi_export_map.end()) {
        sv_name = dpi_export_map[c_name]->sv_name;
        is_task = dpi_export_map[c_name]->is_task;
    }

    std::string scoped_target = scope.empty() ? sv_name : (scope + "." + sv_name);
    FunctionNode* fn = nullptr;
    if (function_map.find(scoped_target) != function_map.end()) {
        fn = function_map[scoped_target];
    } else if (function_map.find(sv_name) != function_map.end()) {
        fn = function_map[sv_name];
    }

    if (fn) {
        LogicVal ret = LogicVal::make_known(fn->return_width, 0);
        bool ok = evaluator.get_expr_eval().eval_function(scoped_target, args, ret);
        if (!ok) {
            evaluator.get_expr_eval().eval_function(fn->name, args, ret);
        }
        return ret;
    }

    TaskNode* tn = nullptr;
    if (task_map.find(scoped_target) != task_map.end()) {
        tn = task_map[scoped_target];
    } else if (task_map.find(sv_name) != task_map.end()) {
        tn = task_map[sv_name];
    }

    if (tn) {
        BoundProcess bp{scope.empty() ? tn->scope : scope, nullptr, 0, false};
        for (auto& s : tn->body) {
            evaluator.execute_statement(s.get(), bp);
            if (!scheduler.is_active()) break;
        }
        return LogicVal(0, 0, 0);
    }

    throw std::runtime_error("[DPI-FNF] Exported routine '" + c_name + "' (SV target: '" + sv_name + "') not found in scope '" + scope + "'");
}

extern "C" {
svScope svGetScope(void) {
    return DpiRegistry::get_active_scope();
}

svScope svSetScope(const svScope scope) {
    return DpiRegistry::set_active_scope(scope);
}

void sv_write_reg(int addr, int data) {
    Engine* eng = static_cast<Engine*>(DpiRegistry::get_active_engine());
    if (!eng) {
        throw std::runtime_error("[CAS-DPI-ERR] sv_write_reg called without active simulation engine context");
    }
    eng->dispatch_dpi_export("sv_write_reg", {LogicVal(32, static_cast<uint32_t>(addr)), LogicVal(32, static_cast<uint32_t>(data))});
}

int sv_read_reg(int addr) {
    Engine* eng = static_cast<Engine*>(DpiRegistry::get_active_engine());
    if (!eng) {
        throw std::runtime_error("[CAS-DPI-ERR] sv_read_reg called without active simulation engine context");
    }
    LogicVal res = eng->dispatch_dpi_export("sv_read_reg", {LogicVal(32, static_cast<uint32_t>(addr))});
    return static_cast<int>(static_cast<int32_t>(res.aval));
}

// IEEE Std 1800-2023 Annex I.9 DPI-C Open Array Inquiry & Access APIs
int svDimensions(const void* h) {
    if (!h) return 0;
    return 1;
}

int svLeft(const void* h, int d) {
    if (!h || d != 1) return 0;
    return 0;
}

int svRight(const void* h, int d) {
    if (!h || d != 1) return 0;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(h);
    return arr->size > 0 ? arr->size - 1 : 0;
}

int svLow(const void* h, int d) {
    if (!h || d != 1) return 0;
    return 0;
}

int svHigh(const void* h, int d) {
    if (!h || d != 1) return 0;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(h);
    return arr->size > 0 ? arr->size - 1 : 0;
}

int svIncrement(const void* h, int d) {
    return 1;
}

int svLength(const void* h, int d) {
    if (!h || d != 1) return 0;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(h);
    return arr->size;
}

int svSize(const void* h, int d) {
    if (!h || d != 1) return 0;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(h);
    return arr->size;
}

int svSizeOfArray(const void* h) {
    if (!h) return 0;
    const SvOpenArray* arr = reinterpret_cast<const SvOpenArray*>(h);
    return arr->size;
}

void* svGetArrElemPtr1(const void* h, int indx) {
    if (!h) return nullptr;
    SvOpenArray* arr = const_cast<SvOpenArray*>(reinterpret_cast<const SvOpenArray*>(h));
    if (indx < 0 || indx >= arr->size) return nullptr;
    if (arr->elem_width <= 32) {
        return static_cast<void*>(&(arr->int32_elements[indx]));
    } else {
        return static_cast<void*>(&(arr->int64_elements[indx]));
    }
}

void* svGetArrElemPtr2(const void* h, int indx1, int /*indx2*/) {
    return svGetArrElemPtr1(h, indx1);
}

void* svGetArrElemPtr3(const void* h, int indx1, int /*indx2*/, int /*indx3*/) {
    return svGetArrElemPtr1(h, indx1);
}

void* svGetArrElemPtr(const void* h, int indx1, ...) {
    return svGetArrElemPtr1(h, indx1);
}

void* svGetArrayPtr(const void* h) {
    return svGetArrElemPtr1(h, 0);
}
}

void Engine::elaborate(std::vector<std::unique_ptr<ModuleNode>>& modules,
                       const std::string& top_module_name,
                       std::ostream* debug_out) {
    Elaborator elaborator;
    elaborator.top_param_overrides = top_param_overrides;
    load_elaboration(elaborator.elaborate(modules, top_module_name, debug_out));
}

std::string Engine::resolve_scoped_name(const std::string& scope, const std::string& name) const {
    auto dot_pos = name.find('.');
    if (dot_pos != std::string::npos) {
        std::string prefix = name.substr(0, dot_pos);
        std::string suffix = name.substr(dot_pos + 1);
        std::string scoped_prefix = scope.empty() ? prefix : (scope + "." + prefix);

        auto it = interface_port_map.find(scoped_prefix);
        if (it != interface_port_map.end()) {
            return it->second + "." + suffix;
        }
        auto it2 = interface_port_map.find(prefix);
        if (it2 != interface_port_map.end()) {
            return it2->second + "." + suffix;
        }
    }
    return scope.empty() ? name : (scope + "." + name);
}

size_t Engine::get_net_id(const std::string& scope, const std::string& name) const {
    std::string cur_scope = scope;
    while (true) {
        std::string resolved = resolve_scoped_name(cur_scope, name);
        auto it = net_map.find(resolved);
        if (it != net_map.end()) return it->second;

        std::string scoped = cur_scope.empty() ? name : (cur_scope + "." + name);
        auto it3 = net_map.find(scoped);
        if (it3 != net_map.end()) return it3->second;

        if (cur_scope.empty()) break;
        auto dot = cur_scope.rfind('.');
        if (dot == std::string::npos) cur_scope = "";
        else cur_scope = cur_scope.substr(0, dot);
    }

    auto it2 = net_map.find(name);
    if (it2 != net_map.end()) return it2->second;

    return (size_t)-1;
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

    scheduler.run_simulation();

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