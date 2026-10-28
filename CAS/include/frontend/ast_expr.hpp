#pragma once
#include "ast_base.hpp"
#include <string>
#include <cstdint>
#include <memory>
#include <vector>

struct NumberNode : public ExpressionNode {
    uint64_t aval; // 0/1 bits (or 1 for X)
    uint64_t bval; // 1 for X/Z
    uint32_t width;

    // Backward-compatible constructor: known value or all-X
    explicit NumberNode(uint64_t val, uint32_t w = 32, bool unk = false)
        : aval(unk ? ((w >= 64) ? ~0ULL : ((1ULL << w) - 1)) : val),
          bval(unk ? ((w >= 64) ? ~0ULL : ((1ULL << w) - 1)) : 0),
          width(w) {}

    // Precise 4-state constructor
    NumberNode(uint64_t a, uint64_t b, uint32_t w)
        : aval(a), bval(b), width(w) {}

    bool is_unknown() const { return bval != 0; }

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<NumberNode>(aval, bval, width);
    }
};

struct IdentifierNode : public ExpressionNode {
    std::string name;

    explicit IdentifierNode(std::string n) : name(std::move(n)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<IdentifierNode>(name);
    }
};

struct IndexSelectNode : public ExpressionNode {
    std::string name;
    std::unique_ptr<ExpressionNode> msb_expr;
    std::unique_ptr<ExpressionNode> lsb_expr;
    bool is_part_select = false;

    IndexSelectNode(std::string n, std::unique_ptr<ExpressionNode> idx)
        : name(std::move(n)), msb_expr(std::move(idx)), lsb_expr(nullptr), is_part_select(false) {}

    IndexSelectNode(std::string n, std::unique_ptr<ExpressionNode> msb, std::unique_ptr<ExpressionNode> lsb)
        : name(std::move(n)), msb_expr(std::move(msb)), lsb_expr(std::move(lsb)), is_part_select(true) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        if (!is_part_select) {
            return std::make_unique<IndexSelectNode>(name, msb_expr ? msb_expr->clone() : nullptr);
        } else {
            return std::make_unique<IndexSelectNode>(
                name,
                msb_expr ? msb_expr->clone() : nullptr,
                lsb_expr ? lsb_expr->clone() : nullptr
            );
        }
    }
};

struct ConcatNode : public ExpressionNode {
    std::vector<std::unique_ptr<ExpressionNode>> elements;

    ConcatNode() = default;
    explicit ConcatNode(std::vector<std::unique_ptr<ExpressionNode>> elems)
        : elements(std::move(elems)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        std::vector<std::unique_ptr<ExpressionNode>> cloned;
        for (const auto& elem : elements) {
            cloned.push_back(elem ? elem->clone() : nullptr);
        }
        return std::make_unique<ConcatNode>(std::move(cloned));
    }
};

struct ReplicateNode : public ExpressionNode {
    std::unique_ptr<ExpressionNode> count_expr;
    std::unique_ptr<ExpressionNode> target_expr;

    ReplicateNode(std::unique_ptr<ExpressionNode> c, std::unique_ptr<ExpressionNode> t)
        : count_expr(std::move(c)), target_expr(std::move(t)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<ReplicateNode>(
            count_expr ? count_expr->clone() : nullptr,
            target_expr ? target_expr->clone() : nullptr
        );
    }
};

struct UnaryOpNode : public ExpressionNode {
    char op;
    std::unique_ptr<ExpressionNode> operand;

    UnaryOpNode(char o, std::unique_ptr<ExpressionNode> opnd)
        : op(o), operand(std::move(opnd)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<UnaryOpNode>(op, operand ? operand->clone() : nullptr);
    }
};

struct BinaryOpNode : public ExpressionNode {
    std::string op;
    std::unique_ptr<ExpressionNode> lhs;
    std::unique_ptr<ExpressionNode> rhs;

    BinaryOpNode(std::string o, std::unique_ptr<ExpressionNode> l, std::unique_ptr<ExpressionNode> r)
        : op(std::move(o)), lhs(std::move(l)), rhs(std::move(r)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<BinaryOpNode>(
            op,
            lhs ? lhs->clone() : nullptr,
            rhs ? rhs->clone() : nullptr
        );
    }
};

struct TernaryOpNode : public ExpressionNode {
    std::unique_ptr<ExpressionNode> condition;
    std::unique_ptr<ExpressionNode> true_expr;
    std::unique_ptr<ExpressionNode> false_expr;

    TernaryOpNode(std::unique_ptr<ExpressionNode> c,
                  std::unique_ptr<ExpressionNode> t,
                  std::unique_ptr<ExpressionNode> f)
        : condition(std::move(c)), true_expr(std::move(t)), false_expr(std::move(f)) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<TernaryOpNode>(
            condition ? condition->clone() : nullptr,
            true_expr ? true_expr->clone() : nullptr,
            false_expr ? false_expr->clone() : nullptr
        );
    }
};