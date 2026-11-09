#include "../../include/kernel/execution_context.hpp"
#include "../../include/kernel/logic_ops.hpp"

void ExecutionContext::push_frame(const std::string& scope_name) {
    call_stack.emplace_back(scope_name);
}

void ExecutionContext::pop_frame() {
    if (!call_stack.empty()) {
        call_stack.pop_back();
    }
}

bool ExecutionContext::has_active_frame() const {
    return !call_stack.empty();
}

ScopeFrame* ExecutionContext::current_frame() {
    if (call_stack.empty()) return nullptr;
    return &call_stack.back();
}

const ScopeFrame* ExecutionContext::current_frame() const {
    if (call_stack.empty()) return nullptr;
    return &call_stack.back();
}

bool ExecutionContext::declare_local(const std::string& name, uint32_t width, bool is_signed, const std::string& type) {
    if (call_stack.empty()) return false;
    call_stack.back().declare_var(name, width, is_signed, type);
    return true;
}

LocalVar* ExecutionContext::lookup_local(const std::string& name) {
    for (auto it = call_stack.rbegin(); it != call_stack.rend(); ++it) {
        if (it->has_var(name)) {
            return it->get_var(name);
        }
    }
    return nullptr;
}

bool ExecutionContext::set_local_value(const std::string& name, const LogicVal& val) {
    LocalVar* var = lookup_local(name);
    if (!var) return false;
    if (var->type_name == "string" || val.is_string) {
        var->value = val;
        return true;
    }
    var->value = LogicOps::cast_width(val, var->width, var->is_signed);
    if (var->type_name == "bit" || var->type_name == "int" || var->type_name == "byte" || var->type_name == "shortint" || var->type_name == "longint") {
        var->value.bval = 0;
        if (!var->value.wide_bval.empty()) {
            std::fill(var->value.wide_bval.begin(), var->value.wide_bval.end(), 0ULL);
        }
    }
    return true;
}