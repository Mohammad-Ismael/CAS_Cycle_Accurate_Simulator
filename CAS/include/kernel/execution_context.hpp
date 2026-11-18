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
    bool is_unpacked_array = false;
    uint32_t elem_width = 8;
    uint32_t array_size = 1;
    int64_t arr_start_idx = 0;
    int64_t arr_end_idx = 0;
    std::vector<LogicVal> unpacked_elements;
    bool is_static = false;
    std::string static_key = "";
    bool is_2state = false;
    uint64_t change_counter = 0;

    LocalVar(std::string n, uint32_t w, bool s = false, std::string t = "", bool q = false, bool dyn = false, bool assoc = false, const std::string& key_t = "", uint32_t key_w = 32)
        : name(std::move(n)), width(w), is_signed(s),
          value(LogicVal::make_x(w)), type_name(std::move(t)), is_queue(q), is_dynamic_array(dyn), is_associative_array(assoc) {
        is_2state = (type_name == "bit" || type_name == "int" || type_name == "byte" || type_name == "shortint" || type_name == "longint");
        if (is_2state) {
            value = LogicVal(width, 0, 0, is_signed);
        }
        if (is_queue) queue_data = QueueObject(name, width, is_signed);
        if (is_dynamic_array) dyn_array_data = DynamicArrayObject(name, width, is_signed);
        if (is_associative_array) {
            bool is_2st = (type_name == "bit" || type_name == "int" || type_name == "byte" || type_name == "shortint" || type_name == "longint");
            assoc_array_data = AssociativeArrayObject(name, width, is_signed, key_t, key_w, is_2st);
        }
        if (type_name == "bit" || type_name == "int" || type_name == "byte" || type_name == "shortint" || type_name == "longint") {
            value = LogicVal(w, 0, 0, is_signed);
        }
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
        if (type_name == "string") {
            value = LogicVal::make_string("");
        }
    }
};

struct ScopeFrame {
    std::string scope_name;
    std::shared_ptr<std::unordered_map<std::string, LocalVar>> locals_ptr;
    bool returned = false;
    std::string func_return_name = "";

    ScopeFrame() : locals_ptr(std::make_shared<std::unordered_map<std::string, LocalVar>>()) {}
    explicit ScopeFrame(std::string name) : scope_name(std::move(name)), locals_ptr(std::make_shared<std::unordered_map<std::string, LocalVar>>()) {}

    bool has_var(const std::string& name) const {
        return locals_ptr && (locals_ptr->find(name) != locals_ptr->end());
    }

    LocalVar* get_var(const std::string& name) {
        if (!locals_ptr) return nullptr;
        auto it = locals_ptr->find(name);
        return (it != locals_ptr->end()) ? &it->second : nullptr;
    }

    const LocalVar* get_var(const std::string& name) const {
        if (!locals_ptr) return nullptr;
        auto it = locals_ptr->find(name);
        return (it != locals_ptr->end()) ? &it->second : nullptr;
    }

    void declare_var(const std::string& name, uint32_t width, bool is_signed, const std::string& type = "", bool is_q = false, bool is_dyn = false, bool is_assoc = false, const std::string& key_t = "", uint32_t key_w = 32) {
        if (!locals_ptr) locals_ptr = std::make_shared<std::unordered_map<std::string, LocalVar>>();
        auto it = locals_ptr->find(name);
        if (it != locals_ptr->end()) {
            it->second = LocalVar(name, width, is_signed, type, is_q, is_dyn, is_assoc, key_t, key_w);
        } else {
            locals_ptr->emplace(name, LocalVar(name, width, is_signed, type, is_q, is_dyn, is_assoc, key_t, key_w));
        }
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
