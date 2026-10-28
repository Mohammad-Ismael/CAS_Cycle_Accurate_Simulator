#pragma once
#include "../frontend/ast_stmt.hpp"
#include <vector>
#include <memory>
#include <utility>

class ProcessLowerer {
public:
    struct LoweredProcess {
        std::vector<std::unique_ptr<StatementNode>> instructions;
        bool is_looping = false;
        size_t loop_anchor_pc = 0;
    };

    static LoweredProcess lower(std::vector<std::unique_ptr<StatementNode>>& input_body, bool is_always) {
        LoweredProcess result;
        result.is_looping = is_always;
        result.loop_anchor_pc = 0;

        flatten_statements(input_body, result.instructions, result.is_looping, result.loop_anchor_pc);
        return result;
    }

private:
    static void flatten_statements(
        std::vector<std::unique_ptr<StatementNode>>& stmts,
        std::vector<std::unique_ptr<StatementNode>>& dest,
        bool& is_looping,
        size_t& loop_anchor
    ) {
        for (auto& stmt : stmts) {
            if (!stmt) continue;

            // 1. Unwrap nested block statements (begin ... end)
            if (auto blk = dynamic_cast<BlockStatementNode*>(stmt.get())) {
                flatten_statements(blk->statements, dest, is_looping, loop_anchor);
                continue;
            }

            // 2. Lower forever loops into pre-anchored loop sequences
            if (auto forever_stmt = dynamic_cast<ForeverStatementNode*>(stmt.get())) {
                is_looping = true;
                loop_anchor = dest.size();
                flatten_statements(forever_stmt->body, dest, is_looping, loop_anchor);
                continue;
            }

            // 3. Move statement ownership directly into linear IR
            dest.push_back(std::move(stmt));
        }
    }
};