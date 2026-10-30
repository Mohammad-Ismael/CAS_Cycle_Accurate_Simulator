#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "types.hpp"
#include "../frontend/ast_module.hpp"

enum class SvaThreadState {
    INACTIVE,
    PENDING_CONSEQUENT,
    MATCHED,
    FAILED
};

struct SvaThread {
    SvaThreadState state = SvaThreadState::INACTIVE;
    uint32_t remaining_cycles = 0;
};

struct SvaAssertionInstance {
    std::string label;
    std::string property_name;
    std::string scope;
    EdgeType clock_edge = EdgeType::POSEDGE;
    std::string clock_signal;
    size_t clock_net_id = (size_t)-1;

    ExpressionNode* disable_expr = nullptr;
    ImplicationType imp_type = ImplicationType::NONE;
    ExpressionNode* antecedent = nullptr;
    ExpressionNode* consequent = nullptr;

    std::vector<StatementNode*> pass_statements;
    std::vector<StatementNode*> fail_statements;

    std::vector<SvaThread> threads;
    bool fired_in_time_slot = false;
};
