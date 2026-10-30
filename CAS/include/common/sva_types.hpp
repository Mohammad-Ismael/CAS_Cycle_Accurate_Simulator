#pragma once
#include <string>
#include <vector>
#include <map>
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

struct SvaSequenceStep {
    ExpressionNode* expr = nullptr;
    RepetitionType rep_type = RepetitionType::CONSECUTIVE;
    uint32_t min_count = 1;
    uint32_t max_count = 1;
    uint32_t cycle_delay = 0;
};

struct SvaThread {
    SvaThreadState state = SvaThreadState::INACTIVE;
    uint32_t remaining_delay = 0;
    size_t step_index = 0;
    uint32_t current_rep_count = 0;
    uint32_t target_rep_count = 1;
    RepetitionType rep_type = RepetitionType::CONSECUTIVE;
    std::map<std::string, LogicVal> local_vars;
};

struct PendingSubroutineCall {
    std::string name;
    std::vector<LogicVal> args;
    std::string scope;
};

inline void extract_sequence_steps(ExpressionNode* node, std::vector<SvaSequenceStep>& steps) {
    if (!node) return;
    if (auto concat = dynamic_cast<SequenceConcatNode*>(node)) {
        for (const auto& item : concat->elements) {
            if (auto rep = dynamic_cast<SequenceRepetitionNode*>(item.expr.get())) {
                steps.push_back({rep->expr.get(), rep->rep_type, rep->min_count, rep->max_count, item.delay});
            } else {
                steps.push_back({item.expr.get(), RepetitionType::CONSECUTIVE, 1, 1, item.delay});
            }
        }
    } else if (auto rep = dynamic_cast<SequenceRepetitionNode*>(node)) {
        steps.push_back({rep->expr.get(), rep->rep_type, rep->min_count, rep->max_count, 0});
    } else {
        steps.push_back({node, RepetitionType::CONSECUTIVE, 1, 1, 0});
    }
}

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
    std::vector<SvaLocalVar> local_vars;
    std::vector<SvaSequenceStep> consequent_steps;

    std::vector<StatementNode*> pass_statements;
    std::vector<StatementNode*> fail_statements;

    std::vector<SvaThread> threads;
    bool fired_in_time_slot = false;
};
