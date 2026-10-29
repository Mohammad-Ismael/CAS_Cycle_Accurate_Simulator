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

    LocalVar(std::string n, uint32_t w, bool s = false)
        : name(std::move(n)), width(w), is_signed(s),
          value(LogicVal::make_x(w)) {}
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

    void declare_var(const std::string& name, uint32_t width, bool is_signed) {
        locals.emplace(name, LocalVar(name, width, is_signed));
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

    bool declare_local(const std::string& name, uint32_t width, bool is_signed);
    LocalVar* lookup_local(const std::string& name);
    bool set_local_value(const std::string& name, const LogicVal& val);
};