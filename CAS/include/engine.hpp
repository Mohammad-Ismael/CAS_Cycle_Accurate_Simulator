#pragma once
#include "ast.hpp"
#include "kernel/netlist.hpp"
#include "kernel/elaborator.hpp"
#include "kernel/evaluator.hpp"
#include "kernel/expr_evaluator.hpp"
#include "kernel/scheduler.hpp"
#include "profiler/profiler.hpp"
#include "tracer/vcd_dumper.hpp"
#include "common/constraint_solver.hpp"
#include "common/coverage_types.hpp"
#include "common/sva_types.hpp"
#include "common/dpic_types.hpp"
#include "common/sync_types.hpp"
#include <unordered_map>
#include <cmath>
#include <iostream>
#include <vector>

struct MonitorSession {
    bool active = false;
    std::string scope;
    std::string format_string;
    std::vector<std::string> args;
    std::vector<std::unique_ptr<ExpressionNode>> arg_exprs;
    std::vector<LogicVal> prev_values;
    bool has_leading_time = false;
    std::string src_file = "";
    size_t src_line = 0;
};

class Engine {
    friend class Evaluator;
    friend class ExprEvaluator;
    friend class StratifiedScheduler;

private:
    std::vector<Net> netlist;
    std::unordered_map<std::string, size_t> net_map;
    std::vector<size_t> canonical_net_map;
    std::vector<std::vector<size_t>> alias_members;
    std::vector<BoundProcess> processes;

    std::vector<SliceBinding> slice_bindings;
    std::vector<ContinuousAssignBinding> continuous_assigns;
    std::vector<ResolvedContinuousAssign> resolved_continuous_assigns;
    std::vector<GateInstance> gates;
    std::vector<ModulePathDelay> module_path_delays;
    std::unordered_map<std::string, MemoryArray> memory_map;
    std::unordered_map<std::string, QueueObject> queue_map;
    std::unordered_map<std::string, DynamicArrayObject> dynamic_array_map;
    std::unordered_map<std::string, AssociativeArrayObject> assoc_array_map;
    std::unordered_map<std::string, TaskNode*> task_map;
    std::unordered_map<std::string, FunctionNode*> function_map;
    std::unordered_map<std::string, uint64_t> param_map;
    std::vector<NamedEvent> named_events;
    std::unordered_map<std::string, size_t> named_event_map;
    std::unordered_map<std::string, EnumType> enum_types;
    std::unordered_map<std::string, std::string> var_to_enum_type;
    std::unordered_map<std::string, LogicVal> enum_constant_map;
    std::unordered_map<std::string, PackedStructType> struct_types;
    std::unordered_map<std::string, std::string> var_to_struct_type;
    std::unordered_map<std::string, std::string> interface_port_map;
    std::unordered_map<std::string, std::string> modport_binding_map;
    std::unordered_set<std::string> interface_instances;
    std::unordered_set<std::string> virtual_interface_names;
    std::unordered_map<uint64_t, VirtualInterfaceHandle> vif_table;
    uint64_t next_vif_id = 0x80000001;
    std::vector<ClockingBlockInstance> clocking_blocks;
    std::unordered_map<std::string, size_t> clocking_block_map;
    std::unordered_map<std::string, size_t> default_clocking_map;
    std::unordered_map<std::string, const ClassNode*> class_defs;
    std::unordered_map<std::string, std::unordered_map<std::string, LogicVal>> class_static_mem;
    std::unordered_set<std::string> class_handle_names;
    std::unordered_map<std::string, std::string> var_to_class_type;
    std::unordered_map<uint64_t, std::shared_ptr<ClassObject>> heap;
    std::unordered_map<std::string, const CovergroupNode*> covergroup_defs;
    std::unordered_set<std::string> covergroup_handle_names;
    std::unordered_map<std::string, std::string> var_to_covergroup_type;
    std::unordered_map<uint64_t, std::shared_ptr<CovergroupInstance>> covergroup_heap;
    std::vector<uint64_t> event_triggered_covergroups;
    std::vector<SvaAssertionInstance> sva_assertions;
    std::unordered_map<std::string, std::shared_ptr<PropertyNode>> property_defs;
    std::unordered_map<std::string, std::shared_ptr<SequenceNode>> sequence_defs;
    std::unordered_set<std::string> assertion_names;
    std::unordered_set<std::string> property_names;
    std::unordered_set<std::string> sequence_names;
    std::unordered_set<std::string> sva_local_var_names;
    std::unordered_set<std::string> chandle_names;
    std::unordered_set<std::string> open_array_names;
    std::unordered_set<std::string> semaphore_handle_names;
    std::unordered_map<uint64_t, std::shared_ptr<SemaphoreObject>> semaphore_heap;
    std::unordered_set<std::string> mailbox_handle_names;
    std::unordered_map<uint64_t, std::shared_ptr<MailboxObject>> mailbox_heap;
    uint64_t next_handle_id = 1;

    MonitorSession monitor_session;

    bool allow_display = true;
    bool allow_monitor = true;
    bool allow_finish_msg = true;

    VcdDumper vcd_dumper;
    Evaluator evaluator;
    StratifiedScheduler scheduler;

public:
    ConstraintSolver solver;
    DpiRegistry dpi_registry;
    std::vector<std::string> source_search_paths;
    std::unordered_map<std::string, DpiImportNode*> dpi_import_map;
    std::unordered_map<std::string, DpiExportNode*> dpi_export_map;
    std::unordered_map<std::string, std::shared_ptr<PackageNode>> package_defs;
    std::string active_scope_name;

    LogicVal dispatch_dpi_export(const std::string& c_name, const std::vector<LogicVal>& args);

    Engine();
    ~Engine() = default;

    void set_vcd_allowed(bool allowed) { vcd_dumper.set_allowed(allowed); }
    void set_display_allowed(bool allowed) { allow_display = allowed; }
    void set_monitor_allowed(bool allowed) { allow_monitor = allowed; }
    void set_finish_msg_allowed(bool allowed) { allow_finish_msg = allowed; }

    bool is_display_allowed() const { return allow_display; }
    bool is_monitor_allowed() const { return allow_monitor; }
    bool is_finish_msg_allowed() const { return allow_finish_msg; }

    bool is_queue(const std::string& name, const std::string& scope = "") const {
        std::string scoped = scope.empty() ? name : (scope + "." + name);
        if (queue_map.find(scoped) != queue_map.end()) return true;
        if (queue_map.find(name) != queue_map.end()) return true;
        return false;
    }

    QueueObject* get_queue(const std::string& name, const std::string& scope = "") {
        std::string scoped = scope.empty() ? name : (scope + "." + name);
        auto it = queue_map.find(scoped);
        if (it != queue_map.end()) return &it->second;
        it = queue_map.find(name);
        if (it != queue_map.end()) return &it->second;
        return nullptr;
    }

    bool is_dynamic_array(const std::string& name, const std::string& scope = "") const {
        std::string scoped = scope.empty() ? name : (scope + "." + name);
        if (dynamic_array_map.find(scoped) != dynamic_array_map.end()) return true;
        if (dynamic_array_map.find(name) != dynamic_array_map.end()) return true;
        return false;
    }

    DynamicArrayObject* get_dynamic_array(const std::string& name, const std::string& scope = "") {
        std::string scoped = scope.empty() ? name : (scope + "." + name);
        auto it = dynamic_array_map.find(scoped);
        if (it != dynamic_array_map.end()) return &it->second;
        it = dynamic_array_map.find(name);
        if (it != dynamic_array_map.end()) return &it->second;
        return nullptr;
    }

    bool is_assoc_array(const std::string& name, const std::string& scope = "") const {
        std::string scoped = scope.empty() ? name : (scope + "." + name);
        if (assoc_array_map.find(scoped) != assoc_array_map.end()) return true;
        if (assoc_array_map.find(name) != assoc_array_map.end()) return true;
        std::string cur_scope = scope;
        while (!cur_scope.empty()) {
            auto pos = cur_scope.rfind('.');
            if (pos != std::string::npos) {
                cur_scope = cur_scope.substr(0, pos);
                if (assoc_array_map.find(cur_scope + "." + name) != assoc_array_map.end()) return true;
            } else {
                break;
            }
        }
        return false;
    }

    AssociativeArrayObject* get_assoc_array(const std::string& name, const std::string& scope = "") {
        std::string scoped = scope.empty() ? name : (scope + "." + name);
        auto it = assoc_array_map.find(scoped);
        if (it != assoc_array_map.end()) return &it->second;
        it = assoc_array_map.find(name);
        if (it != assoc_array_map.end()) return &it->second;
        std::string cur_scope = scope;
        while (!cur_scope.empty()) {
            auto pos = cur_scope.rfind('.');
            if (pos != std::string::npos) {
                cur_scope = cur_scope.substr(0, pos);
                it = assoc_array_map.find(cur_scope + "." + name);
                if (it != assoc_array_map.end()) return &it->second;
            } else {
                break;
            }
        }
        return nullptr;
    }

    bool is_class_handle(const std::string& name, const std::string& scope = "") const {
        std::string scoped = scope.empty() ? name : (scope + "." + name);
        if (class_handle_names.find(scoped) != class_handle_names.end()) return true;
        if (class_handle_names.find(name) != class_handle_names.end()) return true;
        return false;
    }

    std::string get_class_type(const std::string& name, const std::string& scope = "") const {
        std::string scoped = scope.empty() ? name : (scope + "." + name);
        auto it = var_to_class_type.find(scoped);
        if (it != var_to_class_type.end()) return it->second;
        it = var_to_class_type.find(name);
        if (it != var_to_class_type.end()) return it->second;
        return "";
    }

    const ClassNode* get_class_def(const std::string& class_name) const {
        auto it = class_defs.find(class_name);
        if (it != class_defs.end()) return it->second;
        return nullptr;
    }

    std::shared_ptr<ClassObject> allocate_class_object(const std::string& class_type) {
        uint64_t id = next_handle_id++;
        const ClassNode* def = get_class_def(class_type);
        auto obj = std::make_shared<ClassObject>(id, class_type, def);
        if (def) {
            std::vector<const ClassNode*> hierarchy;
            const ClassNode* cur = def;
            while (cur) {
                hierarchy.push_back(cur);
                cur = cur->base_class;
            }
            for (auto it = hierarchy.rbegin(); it != hierarchy.rend(); ++it) {
                for (const auto& m : (*it)->member_vars) {
                    if (!m.is_static) {
                        obj->properties[m.name] = LogicVal(m.width, 0, 0);
                        if (m.is_virtual_interface) {
                            VirtualInterfaceHandle h;
                            h.iface_type = m.interface_name;
                            h.modport = m.modport_name;
                            h.is_valid_vif = true;
                            obj->virtual_interfaces[m.name] = h;
                        }
                    }
                }
            }
        }
        heap[id] = obj;
        return obj;
    }

    std::shared_ptr<ClassObject> get_class_object(uint64_t handle_id) {
        if (handle_id == 0) return nullptr;
        auto it = heap.find(handle_id);
        if (it != heap.end()) return it->second;
        return nullptr;
    }

    LogicVal get_class_static_var(const std::string& class_type, const std::string& var_name) {
        auto it_cls = class_static_mem.find(class_type);
        if (it_cls != class_static_mem.end()) {
            auto it_v = it_cls->second.find(var_name);
            if (it_v != it_cls->second.end()) {
                return it_v->second;
            }
        }
        const ClassNode* def = get_class_def(class_type);
        if (def) {
            for (const auto& m : def->member_vars) {
                if (m.name == var_name && m.is_static) {
                    if (m.default_expr) {
                        if (auto num = dynamic_cast<NumberNode*>(m.default_expr.get())) {
                            LogicVal dval(m.width, num->aval, num->bval);
                            class_static_mem[class_type][var_name] = dval;
                            return dval;
                        }
                    }
                    LogicVal zval(m.width, 0, 0);
                    class_static_mem[class_type][var_name] = zval;
                    return zval;
                }
            }
            if (def->base_class) {
                return get_class_static_var(def->base_class->name, var_name);
            }
        }
        return LogicVal::make_known(32, 0);
    }

    void set_class_static_var(const std::string& class_type, const std::string& var_name, const LogicVal& val) {
        class_static_mem[class_type][var_name] = val;
    }

    bool is_covergroup_handle(const std::string& name, const std::string& scope = "") const {
        std::string scoped = scope.empty() ? name : (scope + "." + name);
        if (covergroup_handle_names.find(scoped) != covergroup_handle_names.end()) return true;
        if (covergroup_handle_names.find(name) != covergroup_handle_names.end()) return true;
        return false;
    }

    bool is_chandle(const std::string& name, const std::string& scope = "") const {
        std::string scoped = scope.empty() ? name : (scope + "." + name);
        if (chandle_names.find(scoped) != chandle_names.end()) return true;
        if (chandle_names.find(name) != chandle_names.end()) return true;
        return false;
    }

    bool is_open_array(const std::string& name, const std::string& scope = "") const {
        std::string scoped = scope.empty() ? name : (scope + "." + name);
        if (open_array_names.find(scoped) != open_array_names.end()) return true;
        if (open_array_names.find(name) != open_array_names.end()) return true;
        return false;
    }

    bool is_sva_local_var(const std::string& name, const std::string& scope = "") const {
        std::string scoped = scope.empty() ? name : (scope + "." + name);
        if (sva_local_var_names.find(scoped) != sva_local_var_names.end()) return true;
        if (sva_local_var_names.find(name) != sva_local_var_names.end()) return true;
        return false;
    }

    bool is_semaphore(const std::string& name, const std::string& scope = "") const {
        std::string scoped = scope.empty() ? name : (scope + "." + name);
        if (semaphore_handle_names.find(scoped) != semaphore_handle_names.end()) return true;
        if (semaphore_handle_names.find(name) != semaphore_handle_names.end()) return true;
        return false;
    }

    std::shared_ptr<SemaphoreObject> allocate_semaphore(int keys = 0) {
        uint64_t id = next_handle_id++;
        auto sobj = std::make_shared<SemaphoreObject>();
        sobj->handle_id = id;
        sobj->keys = keys;
        semaphore_heap[id] = sobj;
        return sobj;
    }

    std::shared_ptr<SemaphoreObject> get_semaphore(uint64_t handle_id) {
        if (handle_id == 0) return nullptr;
        auto it = semaphore_heap.find(handle_id);
        if (it != semaphore_heap.end()) return it->second;
        return nullptr;
    }

    bool is_mailbox(const std::string& name, const std::string& scope = "") const {
        std::string scoped = scope.empty() ? name : (scope + "." + name);
        if (mailbox_handle_names.find(scoped) != mailbox_handle_names.end()) return true;
        if (mailbox_handle_names.find(name) != mailbox_handle_names.end()) return true;
        return false;
    }

    std::shared_ptr<MailboxObject> allocate_mailbox(int bound = 0) {
        uint64_t id = next_handle_id++;
        auto mobj = std::make_shared<MailboxObject>();
        mobj->handle_id = id;
        mobj->bound = bound;
        mailbox_heap[id] = mobj;
        return mobj;
    }

    std::shared_ptr<MailboxObject> get_mailbox(uint64_t handle_id) {
        if (handle_id == 0) return nullptr;
        auto it = mailbox_heap.find(handle_id);
        if (it != mailbox_heap.end()) return it->second;
        return nullptr;
    }

    uint64_t get_semaphore_handle_id(const std::string& var_name, const std::string& scope, BoundProcess* bp, ExecutionContext* ctx) {
        if (ctx && ctx->has_active_frame()) {
            LocalVar* local = ctx->lookup_local(var_name);
            if (local && local->is_semaphore) return local->value.aval;
        }
        if (bp && !bp->call_stack.empty()) {
            for (auto it = bp->call_stack.rbegin(); it != bp->call_stack.rend(); ++it) {
                LocalVar* local = it->local_frame.get_var(var_name);
                if (local && local->is_semaphore) return local->value.aval;
            }
        }
        size_t nid = get_net_id(scope, var_name);
        if (nid != (size_t)-1 && netlist[nid].is_semaphore) {
            return netlist[nid].value.aval;
        }
        return 0;
    }

    uint64_t get_mailbox_handle_id(const std::string& var_name, const std::string& scope, BoundProcess* bp, ExecutionContext* ctx) {
        if (ctx && ctx->has_active_frame()) {
            LocalVar* local = ctx->lookup_local(var_name);
            if (local && local->is_mailbox) return local->value.aval;
        }
        if (bp && !bp->call_stack.empty()) {
            for (auto it = bp->call_stack.rbegin(); it != bp->call_stack.rend(); ++it) {
                LocalVar* local = it->local_frame.get_var(var_name);
                if (local && local->is_mailbox) return local->value.aval;
            }
        }
        size_t nid = get_net_id(scope, var_name);
        if (nid != (size_t)-1 && netlist[nid].is_mailbox) {
            return netlist[nid].value.aval;
        }
        return 0;
    }

    std::string get_covergroup_type(const std::string& name, const std::string& scope = "") const {
        std::string scoped = scope.empty() ? name : (scope + "." + name);
        auto it = var_to_covergroup_type.find(scoped);
        if (it != var_to_covergroup_type.end()) return it->second;
        it = var_to_covergroup_type.find(name);
        if (it != var_to_covergroup_type.end()) return it->second;
        return "";
    }

    const CovergroupNode* get_covergroup_def(const std::string& cg_name) const {
        auto it = covergroup_defs.find(cg_name);
        if (it != covergroup_defs.end()) return it->second;
        return nullptr;
    }

    std::shared_ptr<CovergroupInstance> allocate_covergroup_instance(
        const std::string& cg_type,
        const std::string& creation_scope = "",
        const std::vector<std::unique_ptr<ExpressionNode>>& actual_args = {},
        const std::function<LogicVal(ExpressionNode*)>& eval_expr = nullptr
    ) {
        uint64_t id = next_handle_id++;
        const CovergroupNode* def = get_covergroup_def(cg_type);
        auto inst = std::make_shared<CovergroupInstance>(id, cg_type, def, creation_scope);
        if (def) {
            inst->per_instance = def->per_instance;

            std::unordered_map<std::string, const ExpressionNode*> ref_map;
            std::unordered_map<std::string, uint64_t> const_map;

            for (size_t i = 0; i < def->arguments.size(); ++i) {
                const auto& formal = def->arguments[i];
                const ExpressionNode* actual_expr = (i < actual_args.size()) ? actual_args[i].get() : formal.default_value.get();
                if (!actual_expr) continue;

                if (formal.direction == "ref") {
                    ref_map[formal.name] = actual_expr;
                } else {
                    uint64_t val = 0;
                    if (eval_expr) {
                        LogicVal lv = eval_expr(const_cast<ExpressionNode*>(actual_expr));
                        val = lv.is_real ? static_cast<uint64_t>(std::round(lv.real_val)) : lv.aval;
                    } else if (auto num = dynamic_cast<const NumberNode*>(actual_expr)) {
                        val = num->aval;
                    }
                    const_map[formal.name] = val;
                }
            }

            std::function<std::unique_ptr<ExpressionNode>(const ExpressionNode*)> subst =
                [&](const ExpressionNode* expr) -> std::unique_ptr<ExpressionNode> {
                    if (!expr) return nullptr;
                    if (auto id_expr = dynamic_cast<const IdentifierNode*>(expr)) {
                        auto it_ref = ref_map.find(id_expr->name);
                        if (it_ref != ref_map.end()) {
                            return it_ref->second->clone();
                        }
                        auto it_c = const_map.find(id_expr->name);
                        if (it_c != const_map.end()) {
                            return std::make_unique<NumberNode>(it_c->second, 32);
                        }
                        return id_expr->clone();
                    }
                    if (auto num = dynamic_cast<const NumberNode*>(expr)) {
                        return num->clone();
                    }
                    if (auto bin = dynamic_cast<const BinaryOpNode*>(expr)) {
                        return std::make_unique<BinaryOpNode>(bin->op, subst(bin->lhs.get()), subst(bin->rhs.get()));
                    }
                    if (auto un = dynamic_cast<const UnaryOpNode*>(expr)) {
                        return std::make_unique<UnaryOpNode>(un->op, subst(un->operand.get()));
                    }
                    if (auto ter = dynamic_cast<const TernaryOpNode*>(expr)) {
                        return std::make_unique<TernaryOpNode>(subst(ter->condition.get()), subst(ter->true_expr.get()), subst(ter->false_expr.get()));
                    }
                    if (auto idx = dynamic_cast<const IndexSelectNode*>(expr)) {
                        if (idx->is_part_select) {
                            return std::make_unique<IndexSelectNode>(idx->name, subst(idx->msb_expr.get()), subst(idx->lsb_expr.get()));
                        } else {
                            return std::make_unique<IndexSelectNode>(idx->name, subst(idx->msb_expr.get()));
                        }
                    }
                    if (auto cast = dynamic_cast<const CastNode*>(expr)) {
                        return std::make_unique<CastNode>(cast->target_type, subst(cast->expr.get()));
                    }
                    return expr->clone();
                };

            for (const auto& cp_node : def->coverpoints) {
                CoverpointItem cp_item(cp_node->label, subst(cp_node->target_expr.get()));
                for (const auto& bin_node : cp_node->bins) {
                    std::vector<InsideRange> bin_ranges;
                    for (const auto& r : bin_node->ranges) {
                        bin_ranges.push_back({subst(r.low.get()), subst(r.high.get())});
                    }
                    cp_item.add_bin(CovergroupBin(bin_node->name, std::move(bin_ranges)));
                }
                inst->coverpoints.push_back(std::move(cp_item));
            }
            for (const auto& cr_node : def->crosses) {
                CoverCrossItem cr_item;
                cr_item.label = cr_node->label;
                cr_item.crossed_cp_names = cr_node->crossed_coverpoints;
                for (const auto& cp_name : cr_node->crossed_coverpoints) {
                    for (size_t i = 0; i < inst->coverpoints.size(); ++i) {
                        if (inst->coverpoints[i].label == cp_name) {
                            cr_item.crossed_cp_indices.push_back(i);
                            break;
                        }
                    }
                }
                cr_item.generate_cartesian(inst->coverpoints);
                inst->crosses.push_back(std::move(cr_item));
            }

            if (def->has_event_trigger) {
                inst->has_event_trigger = true;
                inst->clock_edge = def->event_edge;
                std::string clk_name = def->event_signal;
                auto it_ref = ref_map.find(clk_name);
                if (it_ref != ref_map.end()) {
                    if (auto id_clk = dynamic_cast<const IdentifierNode*>(it_ref->second)) {
                        clk_name = id_clk->name;
                    }
                }
                inst->clock_net_id = get_net_id(creation_scope, clk_name);
                event_triggered_covergroups.push_back(id);
            }
        }
        covergroup_heap[id] = inst;
        return inst;
    }

    std::shared_ptr<CovergroupInstance> get_covergroup_instance(uint64_t handle_id) {
        if (handle_id == 0) return nullptr;
        auto it = covergroup_heap.find(handle_id);
        if (it != covergroup_heap.end()) return it->second;
        return nullptr;
    }

    bool is_property(const std::string& name, const std::string& scope = "") const {
        std::string scoped = scope.empty() ? name : (scope + "." + name);
        if (property_names.find(scoped) != property_names.end()) return true;
        if (property_names.find(name) != property_names.end()) return true;
        return false;
    }

    bool is_sequence(const std::string& name, const std::string& scope = "") const {
        std::string scoped = scope.empty() ? name : (scope + "." + name);
        if (sequence_names.find(scoped) != sequence_names.end()) return true;
        if (sequence_names.find(name) != sequence_names.end()) return true;
        return false;
    }

    bool is_assertion(const std::string& name, const std::string& scope = "") const {
        std::string scoped = scope.empty() ? name : (scope + "." + name);
        if (assertion_names.find(scoped) != assertion_names.end()) return true;
        if (assertion_names.find(name) != assertion_names.end()) return true;
        return false;
    }

    uint64_t get_or_create_vif_handle(const std::string& inst_path, const std::string& modport = "", const std::string& iface_type = "") {
        for (const auto& pair : vif_table) {
            if (pair.second.inst_path == inst_path && pair.second.modport == modport) {
                return pair.first;
            }
        }
        uint64_t id = next_vif_id++;
        VirtualInterfaceHandle h;
        h.id = id;
        h.inst_path = inst_path;
        h.modport = modport;
        h.iface_type = iface_type;
        h.is_valid_vif = true;
        vif_table[id] = h;
        return id;
    }

    VirtualInterfaceHandle get_vif_handle(uint64_t id) const {
        auto it = vif_table.find(id);
        if (it != vif_table.end()) return it->second;
        return VirtualInterfaceHandle();
    }

    bool is_virtual_interface_handle(uint64_t id) const {
        return vif_table.find(id) != vif_table.end();
    }

    bool is_virtual_interface(const std::string& name, const std::string& scope = "") const {
        std::string scoped = scope.empty() ? name : (scope + "." + name);
        if (virtual_interface_names.find(scoped) != virtual_interface_names.end()) return true;
        if (virtual_interface_names.find(name) != virtual_interface_names.end()) return true;
        return false;
    }

    VirtualInterfaceHandle lookup_virtual_interface(const std::string& name, const std::string& scope, ClassObject* current_this, const BoundProcess* bp);
    size_t resolve_vif_net_id(const VirtualInterfaceHandle& vif, const std::string& member_name);

    uint64_t get_sim_time() const { return scheduler.get_sim_time(); }

    std::string resolve_scoped_name(const std::string& scope, const std::string& name) const;
    size_t get_net_id(const std::string& scope, const std::string& name) const;
    size_t get_clocking_block_id(const std::string& cb_name, const std::string& scope = "") const;
    bool is_clocking_block(const std::string& name) const;

    bool subroutine_has_blocking_timing_control(const StatementNode* stmt) const;
    void execute_sva_subroutine(const std::string& name, const std::vector<LogicVal>& args, const std::string& scope);

    void load_elaboration(ElaborationResult&& elab_res);
    void elaborate(std::vector<std::unique_ptr<ModuleNode>>& modules, 
                   const std::string& top_module_name = "", 
                   std::ostream* debug_out = nullptr);
    void run(std::ostream* debug_out = nullptr);

    EngineStats get_stats();
};