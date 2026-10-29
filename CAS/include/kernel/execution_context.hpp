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

    LocalVar(std::string n, uint32_t w, bool s = false, std::string t = "", bool q = false)
        : name(std::move(n)), width(w), is_signed(s),
          value(LogicVal::make_x(w)), type_name(std::move(t)), is_queue(q) {
        if (is_queue) queue_data = QueueObject(name, width, is_signed);
    }
};

struct ScopeFrame {
    std::string scope_name;
    std::unordered_map<std::string, LocalVar> locals;

    explicit ScopeFrame(std::string name) : scope_name(std::move(name)) {}

    bool has_var(const std::string& name) const {
        return locals.find(name) != locals.end();
    }

    LocalVar* get_var(const std::string& name) {
        auto it = locals.find(name);
        return (it != locals.end()) ? &it->second : nullptr;
    }

    void declare_var(const std::string& name, uint32_t width, bool is_signed, const std::string& type = "", bool is_q = false) {
        locals.emplace(name, LocalVar(name, width, is_signed, type, is_q));
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