#pragma once
#include "ast.hpp"
#include "kernel/netlist.hpp"
#include "kernel/elaborator.hpp"
#include "kernel/evaluator.hpp"
#include "kernel/expr_evaluator.hpp"
#include "kernel/scheduler.hpp"
#include "profiler/profiler.hpp"
#include "tracer/vcd_dumper.hpp"
#include <unordered_map>
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
    std::vector<BoundProcess> processes;

    std::vector<SliceBinding> slice_bindings;
    std::vector<ContinuousAssignBinding> continuous_assigns;
    std::vector<ResolvedContinuousAssign> resolved_continuous_assigns;
    std::vector<GateInstance> gates;
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
    std::vector<ClockingBlockInstance> clocking_blocks;
    std::unordered_map<std::string, size_t> clocking_block_map;
    std::unordered_map<std::string, size_t> default_clocking_map;
    std::unordered_map<std::string, const ClassNode*> class_defs;
    std::unordered_set<std::string> class_handle_names;
    std::unordered_map<std::string, std::string> var_to_class_type;
    std::unordered_map<uint64_t, std::shared_ptr<ClassObject>> heap;
    uint64_t next_handle_id = 1;

    MonitorSession monitor_session;

    bool allow_display = true;
    bool allow_monitor = true;
    bool allow_finish_msg = true;

    VcdDumper vcd_dumper;
    Evaluator evaluator;
    StratifiedScheduler scheduler;

public:
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
            for (const auto& m : def->member_vars) {
                obj->properties[m.name] = LogicVal(m.width, 0, 0);
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

    uint64_t get_sim_time() const { return scheduler.get_sim_time(); }

    std::string resolve_scoped_name(const std::string& scope, const std::string& name) const;
    size_t get_net_id(const std::string& scope, const std::string& name) const;
    size_t get_clocking_block_id(const std::string& cb_name, const std::string& scope = "") const;
    bool is_clocking_block(const std::string& name) const;

    void load_elaboration(ElaborationResult&& elab_res);
    void elaborate(std::vector<std::unique_ptr<ModuleNode>>& modules, 
                   const std::string& top_module_name = "", 
                   std::ostream* debug_out = nullptr);
    void run(std::ostream* debug_out = nullptr);

    EngineStats get_stats();
};