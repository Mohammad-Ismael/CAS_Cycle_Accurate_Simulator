#pragma once
#include "../common/types.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

struct LocalVar {
    std::string name;
    uint32_t width;
    bool is_signed;
    LogicVal value;
    std::string type_name = "";
    bool is_queue = false;
    QueueObject queue_data;
    bool is_dynamic_array = false;
    DynamicArrayObject dyn_array_data;
    bool is_associative_array = false;
    AssociativeArrayObject assoc_array_data;
    bool is_class_handle = false;
    std::string class_type_name = "";
    bool is_covergroup_handle = false;
    std::string covergroup_type_name = "";
    bool is_chandle = false;
    bool is_semaphore = false;
    bool is_mailbox = false;
    bool is_virtual_interface = false;
    std::string interface_name = "";
    std::string modport_name = "";
    uint64_t vif_handle = 0;
    std::vector<ArrayDimInfo> dimensions;
    uint32_t unpacked_dims = 0;
    bool is_vector_or_array = false;

    LocalVar(std::string n, uint32_t w, bool s = false, std::string t = "", bool q = false, bool dyn = false, bool assoc = false, const std::string& key_t = "", uint32_t key_w = 32)
        : name(std::move(n)), width(w), is_signed(s),
          value(LogicVal::make_x(w)), type_name(std::move(t)), is_queue(q), is_dynamic_array(dyn), is_associative_array(assoc) {
        if (is_queue) queue_data = QueueObject(name, width, is_signed);
        if (is_dynamic_array) dyn_array_data = DynamicArrayObject(name, width, is_signed);
        if (is_associative_array) assoc_array_data = AssociativeArrayObject(name, width, is_signed, key_t, key_w);
        if (type_name == "chandle") {
            is_chandle = true;
            value = LogicVal(64, 0, 0);
        }
        if (type_name == "semaphore") {
            is_semaphore = true;
            value = LogicVal(64, 0, 0);
        }
        if (type_name == "mailbox") {
            is_mailbox = true;
            value = LogicVal(64, 0, 0);
        }
    }
};

struct ScopeFrame {
    std::string scope_name;
    std::unordered_map<std::string, LocalVar> locals;
    bool returned = false;
    std::string func_return_name = "";

    explicit ScopeFrame(std::string name) : scope_name(std::move(name)) {}

    bool has_var(const std::string& name) const {
        return locals.find(name) != locals.end();
    }

    LocalVar* get_var(const std::string& name) {
        auto it = locals.find(name);
        return (it != locals.end()) ? &it->second : nullptr;
    }

    const LocalVar* get_var(const std::string& name) const {
        auto it = locals.find(name);
        return (it != locals.end()) ? &it->second : nullptr;
    }

    void declare_var(const std::string& name, uint32_t width, bool is_signed, const std::string& type = "", bool is_q = false, bool is_dyn = false, bool is_assoc = false, const std::string& key_t = "", uint32_t key_w = 32) {
        locals.emplace(name, LocalVar(name, width, is_signed, type, is_q, is_dyn, is_assoc, key_t, key_w));
    }
};

class ExecutionContext {
private:
    std::vector<ScopeFrame> call_stack;

public:
    ExecutionContext() = default;

    void push_frame(const std::string& scope_name);
    void pop_frame();
    bool has_active_frame() const;
    size_t depth() const { return call_stack.size(); }
    ScopeFrame* current_frame();
    const ScopeFrame* current_frame() const;

    bool declare_local(const std::string& name, uint32_t width, bool is_signed, const std::string& type = "");
    LocalVar* lookup_local(const std::string& name);
    bool set_local_value(const std::string& name, const LogicVal& val);
};
