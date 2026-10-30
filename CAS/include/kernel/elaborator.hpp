#pragma once
#include "../frontend/ast_module.hpp"
#include "../common/sva_types.hpp"
#include "netlist.hpp"
#include "execution_context.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <iostream>

struct GateInstance {
    GateType type;
    std::string scope;
    NetSliceRef out_target;
    std::vector<ExpressionNode*> in_exprs;
};

struct ContinuousAssignBinding {
    std::string scope;
    NetSliceRef target;
    ExpressionNode* rhs;
};

struct ContinuousAssignDriver {
    std::string scope;
    ExpressionNode* rhs;
};

struct ResolvedContinuousAssign {
    NetSliceRef target;
    std::vector<ContinuousAssignDriver> drivers;
};

struct MemoryArray {
    std::string name;
    uint32_t word_width = 32;
    uint32_t depth = 0;
    int64_t start_idx = 0;
    int64_t end_idx = 0;
    bool is_descending = false;
    std::vector<LogicVal> words;

    MemoryArray() : word_width(32), depth(0), start_idx(0), end_idx(0), is_descending(false) {}
    MemoryArray(std::string n, uint32_t w, uint32_t d, int64_t b1 = 0, int64_t b2 = 0)
        : name(std::move(n)), word_width(w), depth(d) {
        if (b1 <= b2) {
            start_idx = b1;
            end_idx = b2;
            is_descending = false;
        } else {
            start_idx = b2;
            end_idx = b1;
            is_descending = true;
        }
        words = std::vector<LogicVal>(d, LogicVal::make_x(w));
    }

    bool is_valid_index(int64_t idx) const {
        return (idx >= start_idx && idx <= end_idx);
    }

    size_t to_word_offset(int64_t idx) const {
        if (is_descending) {
            return (size_t)(end_idx - idx);
        } else {
            return (size_t)(idx - start_idx);
        }
    }

    LogicVal get_word(int64_t idx) const {
        if (!is_valid_index(idx)) return LogicVal::make_x(word_width);
        size_t off = to_word_offset(idx);
        if (off < words.size()) return words[off];
        return LogicVal::make_x(word_width);
    }

    void set_word(int64_t idx, const LogicVal& val) {
        if (!is_valid_index(idx)) return;
        size_t off = to_word_offset(idx);
        if (off < words.size()) {
            words[off] = val;
        }
    }
};

struct EventWaitTarget {
    bool active = false;
    EdgeType edge = EdgeType::LEVEL;
    std::string signal_name;
    size_t net_id = 0;
    uint64_t initial_change_counter = 0;
};

struct TaskActivationFrame {
    std::string task_name;
    TaskNode* task_node = nullptr;
    size_t pc = 0;
    std::vector<StatementNode*> flattened_body;
    ScopeFrame local_frame;
    std::vector<std::pair<std::string, std::string>> output_bindings;

    TaskActivationFrame(std::string name, TaskNode* node)
        : task_name(std::move(name)), task_node(node), local_frame(task_name) {}
};

struct ForkBarrier {
    bool active = false;
    bool in_fork_setup = false;
    JoinType join_type = JoinType::JOIN;
    size_t total_children = 0;
    size_t completed_children = 0;
    std::vector<size_t> child_pids;
};

struct BoundProcess {
    std::string scope;
    ProcessNode* proc = nullptr;
    size_t pc = 0;
    bool completed = false;
    bool is_reactive = false;
    bool is_program_process = false;
    EventWaitTarget wait_target;
    bool is_looping = false;
    size_t loop_start_pc = 0;
    bool fired_in_time_slot = false;
    std::vector<TaskActivationFrame> call_stack;

    size_t parent_pid = (size_t)-1;
    ForkBarrier fork_barrier;
    std::vector<StatementNode*> dynamic_statements;
    bool is_delayed = false;
    std::vector<size_t> active_children;
    bool is_waiting_fork = false;
    size_t waiting_event_id = (size_t)-1;

    bool is_waiting_cycle = false;
    size_t cycle_delay_count = 0;
    size_t clocking_block_id = (size_t)-1;

    size_t pid = (size_t)-1;
    bool is_waiting_sync = false;

    BoundProcess() = default;
    BoundProcess(std::string s, ProcessNode* p, size_t pc_val = 0, bool comp = false)
        : scope(std::move(s)), proc(p), pc(pc_val), completed(comp) {}
};

struct ElaborationResult {
    std::vector<Net> netlist;
    std::unordered_map<std::string, size_t> net_map;
    std::vector<SliceBinding> slice_bindings;
    std::vector<ContinuousAssignBinding> continuous_assigns;
    std::vector<GateInstance> gates;
    std::unordered_map<std::string, MemoryArray> memory_map;
    std::unordered_map<std::string, QueueObject> queue_map;
    std::unordered_map<std::string, DynamicArrayObject> dynamic_array_map;
    std::unordered_map<std::string, AssociativeArrayObject> assoc_array_map;
    std::unordered_map<std::string, TaskNode*> task_map;
    std::unordered_map<std::string, FunctionNode*> function_map;
    std::unordered_map<std::string, uint64_t> param_map;
    std::vector<BoundProcess> processes;
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
    std::vector<ClockingBlockInstance> clocking_blocks;
    std::unordered_map<std::string, size_t> clocking_block_map;
    std::unordered_map<std::string, size_t> default_clocking_map;
    std::unordered_map<std::string, const ClassNode*> class_defs;
    std::unordered_set<std::string> class_handle_names;
    std::unordered_map<std::string, std::string> var_to_class_type;
    std::unordered_map<std::string, const CovergroupNode*> covergroup_defs;
    std::unordered_set<std::string> covergroup_handle_names;
    std::unordered_map<std::string, std::string> var_to_covergroup_type;
    std::vector<SvaAssertionInstance> sva_assertions;
    std::unordered_map<std::string, std::shared_ptr<PropertyNode>> property_defs;
    std::unordered_map<std::string, std::shared_ptr<SequenceNode>> sequence_defs;
    std::unordered_set<std::string> assertion_names;
    std::unordered_set<std::string> property_names;
    std::unordered_set<std::string> sequence_names;
    std::unordered_set<std::string> sva_local_var_names;
    std::unordered_map<std::string, DpiImportNode*> dpi_import_map;
    std::unordered_map<std::string, DpiExportNode*> dpi_export_map;
    std::unordered_set<std::string> chandle_names;
    std::unordered_set<std::string> open_array_names;
    std::unordered_set<std::string> semaphore_handle_names;
    std::unordered_set<std::string> mailbox_handle_names;
    std::string top_scope;
};

class Elaborator {
private:
    ElaborationResult result;

    size_t get_or_create_net(const std::string& scoped_name, uint32_t width, bool is_signed = false, NetKind kind = NetKind::WIRE);
    uint64_t eval_const_expr(ExpressionNode* expr, const std::string& scope);
    NetSliceRef resolve_slice_ref(ExpressionNode* expr, const std::string& scope);

    void elaborate_module(ModuleNode* mod, const std::string& scope,
                          const std::unordered_map<std::string, ModuleNode*>& module_defs,
                          const std::vector<ParamOverride>* overrides = nullptr,
                          const std::string& parent_scope = "",
                          std::ostream* debug_out = nullptr);

    ConfigNode* active_config = nullptr;
    std::unordered_map<std::string, std::string> active_instance_cell_map;
    std::unordered_map<std::string, std::string> active_cell_rebind_map;
    std::unordered_set<std::string> valid_instance_paths;

public:
    Elaborator() = default;

    ElaborationResult elaborate(std::vector<std::unique_ptr<ModuleNode>>& modules,
                                const std::string& top_module_name = "",
                                std::ostream* debug_out = nullptr);
};