#pragma once
#include "../frontend/ast_expr.hpp"
#include "../common/sva_types.hpp"
#include "netlist.hpp"
#include "execution_context.hpp"
#include <string>
#include <deque>
#include <map>

class Engine;
struct BoundProcess;
struct FunctionNode;
struct DpiImportNode;

class ExprEvaluator {
private:
    Engine& engine;
    ExecutionContext context;
    BoundProcess* current_bp = nullptr;
    ClassObject* current_this = nullptr;
    const ClassNode* current_class_def = nullptr;
    const std::vector<LogicVal>* preponed_override = nullptr;
    const std::deque<std::vector<LogicVal>>* current_clock_history = nullptr;
    std::map<std::string, LogicVal>* current_sva_local_vars = nullptr;
    std::vector<PendingSubroutineCall>* pending_subroutines = nullptr;

public:
    explicit ExprEvaluator(Engine& eng);

    void set_pending_subroutines(std::vector<PendingSubroutineCall>* p) { pending_subroutines = p; }
    std::vector<PendingSubroutineCall>* get_pending_subroutines() const { return pending_subroutines; }

    void set_sva_local_vars(std::map<std::string, LogicVal>* vars) { current_sva_local_vars = vars; }
    std::map<std::string, LogicVal>* get_sva_local_vars() const { return current_sva_local_vars; }

    void set_preponed_override(const std::vector<LogicVal>* po) { preponed_override = po; }
    const std::vector<LogicVal>* get_preponed_override() const { return preponed_override; }

    void set_current_clock_history(const std::deque<std::vector<LogicVal>>* hist) { current_clock_history = hist; }
    const std::deque<std::vector<LogicVal>>* get_current_clock_history() const { return current_clock_history; }

    void set_current_process(BoundProcess* bp) { current_bp = bp; }
    BoundProcess* get_current_process() const { return current_bp; }
    ExecutionContext& get_context() { return context; }
    const ExecutionContext& get_context() const { return context; }

    void set_current_this(ClassObject* obj) { current_this = obj; }
    ClassObject* get_current_this() const { return current_this; }
    void set_current_class_def(const ClassNode* cd) { current_class_def = cd; }
    const ClassNode* get_current_class_def() const { return current_class_def; }

    LogicVal eval(ExpressionNode* expr, const std::string& scope);
    LogicVal eval_sampled_func(SysSampledFuncNode* func, const std::string& scope);
    LogicVal eval_dpi_call(DpiImportNode* dpi, const std::vector<std::unique_ptr<ExpressionNode>>& args, const std::string& scope);
    LogicVal eval_dpi_call(DpiImportNode* dpi, const std::vector<LogicVal>& evaluated_args, const std::string& scope = "");
    bool eval_function(const std::string& func_name, const std::vector<LogicVal>& arg_vals, LogicVal& result_out, const std::vector<ExpressionNode*>& caller_exprs = {});
    bool eval_class_method(ClassObject* obj, FunctionNode* fn, const std::vector<LogicVal>& arg_vals, LogicVal& result_out);
    bool eval_static_class_method(const std::string& class_type, FunctionNode* fn, const std::vector<LogicVal>& arg_vals, LogicVal& result_out);

    bool get_array_elements(const std::string& var_name, const std::string& scope, std::vector<LogicVal>& out_elements, uint32_t& out_elem_width, bool& out_is_signed);
    std::vector<LogicVal> eval_array_expr(ExpressionNode* expr, const std::string& scope);
    LogicVal eval_array_reduction(const std::string& method, const std::vector<LogicVal>& elements, uint32_t elem_width, bool is_signed, ExpressionNode* with_expr, const std::string& iter_name, const std::string& scope);
    std::vector<LogicVal> eval_array_locator(const std::string& method, const std::vector<LogicVal>& elements, uint32_t elem_width, bool is_signed, ExpressionNode* with_expr, const std::string& iter_name, const std::string& scope);
};
