#pragma once
#include "ast_base.hpp"
#include "ast_expr.hpp"
#include "ast_stmt.hpp"
#include "../common/types.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <unordered_set>

struct PortNode : public ASTNode {
    std::string name;
    std::string direction; // "input", "output", "inout", "internal"
    std::unique_ptr<ExpressionNode> msb_expr;
    std::unique_ptr<ExpressionNode> lsb_expr;
    bool is_signed = false;
    bool is_array = false;
    std::unique_ptr<ExpressionNode> bound1_expr;
    std::unique_ptr<ExpressionNode> bound2_expr;
    bool is_queue = false;
    bool is_dynamic_array = false;
    bool is_associative_array = false;
    std::string assoc_key_type = "";
    uint32_t assoc_key_width = 32;
    bool is_interface = false;
    std::string interface_type_name = "";
    std::string modport_name = "";
    std::unique_ptr<ExpressionNode> queue_bound_expr;
    NetKind net_kind = NetKind::WIRE;
    std::string type_name = "";
    bool is_class_handle = false;
    std::string class_type_name = "";
    bool is_covergroup_handle = false;
    std::string covergroup_type_name = "";
    bool is_chandle = false;
    bool is_open_array = false;
    bool is_semaphore = false;
    bool is_mailbox = false;
    std::unique_ptr<ExpressionNode> default_value = nullptr;

    PortNode() = default;

    PortNode(std::string n, std::string dir,
             std::unique_ptr<ExpressionNode> msb = nullptr,
             std::unique_ptr<ExpressionNode> lsb = nullptr,
             bool signed_flag = false,
             NetKind kind = NetKind::WIRE)
        : name(std::move(n)), direction(std::move(dir)),
          msb_expr(std::move(msb)), lsb_expr(std::move(lsb)),
          is_signed(signed_flag), net_kind(kind) {}
};

struct ParameterDef {
    std::string name;
    std::unique_ptr<ExpressionNode> default_expr;

    ParameterDef() = default;
    ParameterDef(std::string n, std::unique_ptr<ExpressionNode> e) : name(std::move(n)), default_expr(std::move(e)) {}
    ParameterDef(const ParameterDef& other) : name(other.name), default_expr(other.default_expr ? other.default_expr->clone() : nullptr) {}
    ParameterDef& operator=(const ParameterDef& other) {
        if (this != &other) {
            name = other.name;
            default_expr = other.default_expr ? other.default_expr->clone() : nullptr;
        }
        return *this;
    }
    ParameterDef(ParameterDef&&) = default;
    ParameterDef& operator=(ParameterDef&&) = default;
};

struct ParamOverride {
    std::string param_name;
    std::unique_ptr<ExpressionNode> expr;
};

enum class ProcessType {
    INITIAL,
    ALWAYS,
    COMBINATIONAL,
    SEQUENTIAL
};

struct SensitivityTarget {
    EdgeType edge;
    std::string signal_name;
};

struct ProcessNode : public ASTNode {
    ProcessType type;
    std::vector<SensitivityTarget> sensitivity_list;
    std::vector<std::unique_ptr<StatementNode>> body;
    bool is_always_ff = false;
    bool is_always_comb = false;
    bool is_always_latch = false;

    ProcessNode() = default;
    explicit ProcessNode(ProcessType t) : type(t) {}
};

struct TaskNode : public ASTNode {
    std::string name;
    std::string scope = "";
    bool is_automatic = false;
    bool is_virtual = false;
    bool is_static = false;
    std::vector<PortNode> ports;
    std::vector<std::unique_ptr<StatementNode>> body;
    const std::vector<std::unique_ptr<StatementNode>>* shared_body = nullptr;

    const std::vector<std::unique_ptr<StatementNode>>& get_body() const {
        return shared_body ? *shared_body : body;
    }

    TaskNode() = default;
    explicit TaskNode(std::string n, bool autom = false)
        : name(std::move(n)), is_automatic(autom) {}
};

struct FunctionNode : public ASTNode {
    std::string name;
    std::string scope = "";
    bool is_automatic = false;
    bool is_virtual = false;
    bool is_static = false;
    uint32_t return_width = 32;
    bool return_signed = false;
    std::unique_ptr<ExpressionNode> return_msb_expr;
    std::unique_ptr<ExpressionNode> return_lsb_expr;
    std::vector<PortNode> inputs;
    std::vector<std::unique_ptr<StatementNode>> body;
    const std::vector<std::unique_ptr<StatementNode>>* shared_body = nullptr;

    const std::vector<std::unique_ptr<StatementNode>>& get_body() const {
        return shared_body ? *shared_body : body;
    }

    FunctionNode() = default;
};

struct PortConnection {
    std::string port_name;
    std::unique_ptr<ExpressionNode> expr;
};

struct InstantiationNode : public ASTNode {
    std::string module_type;
    std::string instance_name;
    std::vector<ParamOverride> param_overrides;
    std::vector<PortConnection> connections;
    bool is_positional = false;

    InstantiationNode() = default;

    InstantiationNode(std::string type, std::string inst)
        : module_type(std::move(type)), instance_name(std::move(inst)) {}

    InstantiationNode(std::string type, std::string inst, bool positional)
        : module_type(std::move(type)), instance_name(std::move(inst)), is_positional(positional) {}
};

struct ContinuousAssignNode : public ASTNode {
    std::string target_name;
    std::unique_ptr<ExpressionNode> rhs;

    ContinuousAssignNode() = default;
    ContinuousAssignNode(std::string target, std::unique_ptr<ExpressionNode> r)
        : target_name(std::move(target)), rhs(std::move(r)) {}
};

struct GatePrimitiveNode : public ASTNode {
    GateType type = GateType::AND;
    std::string instance_name = "";
    std::unique_ptr<ExpressionNode> out_expr;
    std::vector<std::unique_ptr<ExpressionNode>> in_exprs;

    GatePrimitiveNode() = default;

    GatePrimitiveNode(GateType t, std::unique_ptr<ExpressionNode> out,
                      std::vector<std::unique_ptr<ExpressionNode>> ins,
                      std::string inst = "")
        : type(t), instance_name(std::move(inst)), out_expr(std::move(out)), in_exprs(std::move(ins)) {}
};

struct EnumTypeNode : public ASTNode {
    uint32_t base_width = 32;
    bool is_signed = false;
    std::string base_type_name = "int";
    std::vector<std::pair<std::string, LogicVal>> members;
};

struct StructMemberNode : public ASTNode {
    std::string name;
    uint32_t width = 1;
    bool is_signed = false;
    std::string type_name = "";
};

struct StructTypeNode : public ASTNode {
    bool is_packed = true;
    bool is_signed = false;
    std::vector<StructMemberNode> members;
    uint32_t total_width = 0;
};

struct TypedefNode : public ASTNode {
    std::string type_name;
    std::shared_ptr<EnumTypeNode> enum_type;
    std::shared_ptr<StructTypeNode> struct_type;

    TypedefNode() = default;
    TypedefNode(std::string name, std::shared_ptr<EnumTypeNode> et)
        : type_name(std::move(name)), enum_type(std::move(et)) {}
    TypedefNode(std::string name, std::shared_ptr<StructTypeNode> st)
        : type_name(std::move(name)), struct_type(std::move(st)) {}
};

struct ModportPort {
    std::string direction; // "input", "output", "inout"
    std::string name;
};

struct ModportItemNode : public ASTNode {
    std::string name;
    std::vector<ModportPort> ports;
    std::unordered_map<std::string, std::string> signal_directions;

    ModportItemNode() = default;
    explicit ModportItemNode(std::string n) : name(std::move(n)) {}

    void add_port(const std::string& dir, const std::string& sig_name) {
        ports.push_back({dir, sig_name});
        signal_directions[sig_name] = dir;
    }
};

struct ClockingItem {
    std::string direction; // "input", "output", "inout"
    std::string signal_name;
    std::string skew = "";
};

struct ClockingBlockNode : public ASTNode {
    std::string name;
    bool is_default = false;
    EdgeType clock_edge = EdgeType::POSEDGE;
    std::string clock_signal;
    std::string default_input_skew = "1step";
    std::string default_output_skew = "0";
    std::vector<ClockingItem> items;
    std::unordered_map<std::string, ClockingItem> item_map;

    ClockingBlockNode() = default;
    ClockingBlockNode(std::string n, bool def = false)
        : name(std::move(n)), is_default(def) {}

    void add_item(const std::string& dir, const std::string& sig_name, const std::string& sk = "") {
        ClockingItem item{dir, sig_name, sk};
        items.push_back(item);
        item_map[sig_name] = item;
    }
};


struct ClassMemberVar {
    std::string name;
    uint32_t width = 32;
    bool is_signed = false;
    std::unique_ptr<ExpressionNode> default_expr;
    bool is_rand = false;
    bool is_randc = false;
    bool is_static = false;
    std::unique_ptr<ExpressionNode> msb_expr;
    std::unique_ptr<ExpressionNode> lsb_expr;

    ClassMemberVar() = default;
    ClassMemberVar(ClassMemberVar&&) = default;
    ClassMemberVar& operator=(ClassMemberVar&&) = default;
    ClassMemberVar(const ClassMemberVar& other) {
        name = other.name;
        width = other.width;
        is_signed = other.is_signed;
        if (other.default_expr) default_expr = other.default_expr->clone();
        is_rand = other.is_rand;
        is_randc = other.is_randc;
        is_static = other.is_static;
        if (other.msb_expr) msb_expr = other.msb_expr->clone();
        if (other.lsb_expr) lsb_expr = other.lsb_expr->clone();
    }
    ClassMemberVar& operator=(const ClassMemberVar& other) {
        if (this != &other) {
            name = other.name;
            width = other.width;
            is_signed = other.is_signed;
            default_expr = other.default_expr ? other.default_expr->clone() : nullptr;
            is_rand = other.is_rand;
            is_randc = other.is_randc;
            is_static = other.is_static;
            msb_expr = other.msb_expr ? other.msb_expr->clone() : nullptr;
            lsb_expr = other.lsb_expr ? other.lsb_expr->clone() : nullptr;
        }
        return *this;
    }

    ClassMemberVar clone() const {
        return ClassMemberVar(*this);
    }
};

struct ConstraintBlockNode : public ASTNode {
    std::string name;
    std::vector<std::unique_ptr<ExpressionNode>> exprs;

    ConstraintBlockNode(std::string n, std::vector<std::unique_ptr<ExpressionNode>> e)
        : name(std::move(n)), exprs(std::move(e)) {}
};

struct ClassNode : public ASTNode {
    std::string name;
    std::string base_class_name;
    ClassNode* base_class = nullptr;
    std::vector<ParameterDef> parameters;
    bool is_parameterized = false;
    std::unordered_map<std::string, uint64_t> param_values;
    std::string template_class_name;

    std::vector<ClassMemberVar> member_vars;
    std::vector<std::unique_ptr<FunctionNode>> functions;
    std::vector<std::unique_ptr<TaskNode>> tasks;
    std::unordered_map<std::string, FunctionNode*> function_map;
    std::unordered_map<std::string, TaskNode*> task_map;

    std::vector<std::unique_ptr<ConstraintBlockNode>> constraints;
    std::unordered_map<std::string, ConstraintBlockNode*> constraint_map;

    std::unordered_map<std::string, FunctionNode*> vtable_functions;
    std::unordered_map<std::string, TaskNode*> vtable_tasks;
    std::unordered_set<std::string> virtual_method_names;

    bool has_static_member(const std::string& mname) const {
        for (const auto& m : member_vars) {
            if (m.name == mname && m.is_static) return true;
        }
        if (base_class) return base_class->has_static_member(mname);
        return false;
    }

    ClassNode() = default;
    explicit ClassNode(std::string n, std::string base_n = "")
        : name(std::move(n)), base_class_name(std::move(base_n)) {}

    bool is_subclass_of(const std::string& target_type) const {
        if (name == target_type) return true;
        if (base_class) return base_class->is_subclass_of(target_type);
        return false;
    }

    void add_member_var(ClassMemberVar var) {
        member_vars.push_back(std::move(var));
    }

    void add_constraint(std::unique_ptr<ConstraintBlockNode> cb) {
        ConstraintBlockNode* ptr = cb.get();
        constraints.push_back(std::move(cb));
        constraint_map[ptr->name] = ptr;
    }

    void add_function(std::unique_ptr<FunctionNode> fn) {
        FunctionNode* ptr = fn.get();
        functions.push_back(std::move(fn));
        function_map[ptr->name] = ptr;
    }

    void add_task(std::unique_ptr<TaskNode> t) {
        TaskNode* ptr = t.get();
        tasks.push_back(std::move(t));
        task_map[ptr->name] = ptr;
    }

    FunctionNode* get_constructor() const {
        auto it = function_map.find("new");
        if (it != function_map.end()) return it->second;
        if (base_class) return base_class->get_constructor();
        return nullptr;
    }

    FunctionNode* find_function(const std::string& fname) const {
        auto it = function_map.find(fname);
        if (it != function_map.end()) return it->second;
        if (base_class) return base_class->find_function(fname);
        return nullptr;
    }

    FunctionNode* get_function(const std::string& fname) const {
        return find_function(fname);
    }

    TaskNode* find_task(const std::string& tname) const {
        auto it = task_map.find(tname);
        if (it != task_map.end()) return it->second;
        if (base_class) return base_class->find_task(tname);
        return nullptr;
    }

    TaskNode* get_task(const std::string& tname) const {
        return find_task(tname);
    }

    bool is_method_virtual(const std::string& mname) const {
        if (virtual_method_names.count(mname)) return true;
        if (base_class) return base_class->is_method_virtual(mname);
        return false;
    }

    FunctionNode* dispatch_function(const std::string& fname) const {
        auto it = vtable_functions.find(fname);
        if (it != vtable_functions.end()) return it->second;
        return find_function(fname);
    }

    TaskNode* dispatch_task(const std::string& tname) const {
        auto it = vtable_tasks.find(tname);
        if (it != vtable_tasks.end()) return it->second;
        return find_task(tname);
    }
};

inline bool ClassObject::is_instance_of(const std::string& target_type) const {
    if (class_type_name == target_type) return true;
    if (class_def) return class_def->is_subclass_of(target_type);
    return false;
}

struct CovergroupBinNode : public ASTNode {
    std::string name;
    std::vector<InsideRange> ranges;
    CovergroupBinNode(std::string n, std::vector<InsideRange> r)
        : name(std::move(n)), ranges(std::move(r)) {}
};

struct CoverpointNode : public ASTNode {
    std::string label;
    std::unique_ptr<ExpressionNode> target_expr;
    std::vector<std::unique_ptr<CovergroupBinNode>> bins;
    CoverpointNode(std::string lbl, std::unique_ptr<ExpressionNode> target)
        : label(std::move(lbl)), target_expr(std::move(target)) {}
};

struct CoverCrossNode : public ASTNode {
    std::string label;
    std::vector<std::string> crossed_coverpoints;

    CoverCrossNode(std::string lbl, std::vector<std::string> cps)
        : label(std::move(lbl)), crossed_coverpoints(std::move(cps)) {}
};

struct CovergroupNode : public ASTNode {
    std::string name;
    std::vector<std::unique_ptr<CoverpointNode>> coverpoints;
    std::unordered_map<std::string, CoverpointNode*> coverpoint_map;
    std::vector<std::unique_ptr<CoverCrossNode>> crosses;
    std::unordered_map<std::string, CoverCrossNode*> cross_map;

    std::vector<PortNode> arguments;
    bool has_event_trigger = false;
    EdgeType event_edge = EdgeType::POSEDGE;
    std::string event_signal = "";
    bool per_instance = false;

    explicit CovergroupNode(std::string n) : name(std::move(n)) {}

    void add_coverpoint(std::unique_ptr<CoverpointNode> cp) {
        CoverpointNode* ptr = cp.get();
        coverpoints.push_back(std::move(cp));
        if (!ptr->label.empty()) {
            coverpoint_map[ptr->label] = ptr;
        }
    }

    void add_cross(std::unique_ptr<CoverCrossNode> cr) {
        CoverCrossNode* ptr = cr.get();
        crosses.push_back(std::move(cr));
        if (!ptr->label.empty()) {
            cross_map[ptr->label] = ptr;
        }
    }
};

enum class ImplicationType {
    NONE,
    OVERLAPPING,     // |->
    NON_OVERLAPPING  // |=>
};

struct SvaLocalVar {
    std::string name;
    uint32_t width = 1;
    bool is_signed = false;
};

struct SequenceItem {
    uint32_t cycle_delay = 0;
    std::unique_ptr<ExpressionNode> expr;
};

struct SequenceNode : public ASTNode {
    std::string name;
    EdgeType clock_edge = EdgeType::POSEDGE;
    std::string clock_signal = "";
    std::vector<SvaLocalVar> local_vars;
    std::vector<SequenceItem> items;
};

struct PropertyNode : public ASTNode {
    std::string name;
    EdgeType clock_edge = EdgeType::POSEDGE;
    std::string clock_signal = "";
    std::unique_ptr<ExpressionNode> disable_expr;
    std::vector<SvaLocalVar> local_vars;
    ImplicationType imp_type = ImplicationType::NONE;
    std::unique_ptr<ExpressionNode> antecedent;
    std::unique_ptr<ExpressionNode> consequent;
};

struct AssertPropertyStatementNode : public StatementNode {
    std::string label;
    std::string property_name;
    std::shared_ptr<PropertyNode> inlined_property;
    std::vector<std::unique_ptr<StatementNode>> pass_statements;
    std::vector<std::unique_ptr<StatementNode>> fail_statements;
};

struct DpiImportNode : public ASTNode {
    std::string sv_name;
    std::string c_name;
    bool is_pure = false;
    bool is_context = false;
    bool is_task = false;
    std::string return_type = "int";
    uint32_t return_width = 32;
    bool return_signed = true;
    std::vector<PortNode> tf_ports;

    DpiImportNode() = default;
};

struct DpiExportNode : public ASTNode {
    std::string sv_name;
    std::string c_name;
    bool is_task = false;

    DpiExportNode() = default;
};

struct PackageNode : public ASTNode {
    std::string name;
    std::string defined_in_file = "";
    size_t defined_on_line = 0;
    std::vector<ParameterDef> parameters;
    std::unordered_map<std::string, uint64_t> param_constants;
    std::vector<std::unique_ptr<TypedefNode>> typedefs;
    std::unordered_map<std::string, std::shared_ptr<EnumTypeNode>> enum_typedefs;
    std::unordered_map<std::string, std::shared_ptr<StructTypeNode>> struct_typedefs;
    std::unordered_map<std::string, LogicVal> enum_constant_map;
    std::vector<std::unique_ptr<FunctionNode>> functions;
    std::unordered_map<std::string, FunctionNode*> function_map;
    std::vector<std::unique_ptr<TaskNode>> tasks;
    std::unordered_map<std::string, TaskNode*> task_map;
    std::vector<std::shared_ptr<ClassNode>> classes;
    std::unordered_map<std::string, ClassNode*> class_map;
    std::vector<std::shared_ptr<DpiImportNode>> dpi_imports;
    std::unordered_map<std::string, DpiImportNode*> dpi_import_map;
    std::vector<std::shared_ptr<DpiExportNode>> dpi_exports;
    std::unordered_map<std::string, DpiExportNode*> dpi_export_map;
    std::vector<std::string> wildcard_imports;
    std::unordered_map<std::string, std::pair<std::string, std::string>> explicit_imports;

    PackageNode() = default;
};

struct ModuleNode : public ASTNode {
    std::string name;
    bool is_interface = false;
    std::string defined_in_file = "";
    size_t defined_on_line = 0;
    std::vector<ParameterDef> parameters;
    std::vector<PortNode> ports;
    std::vector<PortNode> signals;
    std::vector<std::string> events;
    std::vector<std::unique_ptr<TypedefNode>> typedefs;
    std::vector<std::unique_ptr<ContinuousAssignNode>> continuous_assigns;
    std::vector<std::unique_ptr<GatePrimitiveNode>> gates;
    std::vector<std::unique_ptr<ProcessNode>> processes;
    std::vector<std::unique_ptr<TaskNode>> tasks;
    std::vector<std::unique_ptr<FunctionNode>> functions;
    std::vector<std::unique_ptr<InstantiationNode>> instantiations;
    std::vector<std::unique_ptr<ModportItemNode>> modports;
    std::unordered_map<std::string, ModportItemNode*> modport_map;
    std::vector<std::unique_ptr<ClockingBlockNode>> clocking_blocks;
    std::unordered_map<std::string, ClockingBlockNode*> clocking_block_map;
    std::string default_clocking_name = "";
    std::vector<std::shared_ptr<ClassNode>> classes;
    std::unordered_map<std::string, ClassNode*> class_map;
    std::vector<std::shared_ptr<CovergroupNode>> covergroups;
    std::unordered_map<std::string, CovergroupNode*> covergroup_map;
    std::vector<std::shared_ptr<SequenceNode>> sequences;
    std::unordered_map<std::string, SequenceNode*> sequence_map;
    std::vector<std::shared_ptr<PropertyNode>> properties;
    std::unordered_map<std::string, PropertyNode*> property_map;
    std::vector<std::unique_ptr<AssertPropertyStatementNode>> assertions;
    std::vector<std::shared_ptr<DpiImportNode>> dpi_imports;
    std::unordered_map<std::string, DpiImportNode*> dpi_import_map;
    std::vector<std::shared_ptr<DpiExportNode>> dpi_exports;
    std::unordered_map<std::string, DpiExportNode*> dpi_export_map;
    std::vector<std::string> wildcard_imports;
    std::unordered_map<std::string, std::pair<std::string, std::string>> explicit_imports;

    ModuleNode() = default;
};

struct InterfaceNode : public ModuleNode {
    InterfaceNode() { is_interface = true; }
};

enum class ConfigRuleType {
    INSTANCE,
    CELL
};

struct ConfigRuleNode : public ASTNode {
    ConfigRuleType rule_type = ConfigRuleType::INSTANCE;
    std::string target_path; // hierarchical path or cell identifier
    std::string lib_name;    // library name, e.g. "work" (optional)
    std::string cell_name;   // target cell name, e.g. "v3_19_0_0_fast_core"
};

struct ConfigNode : public ASTNode {
    std::string name;
    std::string defined_in_file = "";
    size_t defined_on_line = 0;
    std::string design_lib;        // library name for top design, e.g. "work"
    std::string design_top_module; // top design module identifier, e.g. "tb_v3_19_0_0_regression"
    std::vector<std::string> default_liblist;
    std::vector<std::unique_ptr<ConfigRuleNode>> rules;
    std::unordered_map<std::string, std::string> instance_cell_map; // hierarchical path -> rebound cell name
    std::unordered_map<std::string, std::string> cell_rebind_map;    // cell name -> rebound cell name

    ConfigNode() = default;
};