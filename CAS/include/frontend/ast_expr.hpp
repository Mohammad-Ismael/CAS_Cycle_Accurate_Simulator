#pragma once
#include "ast_base.hpp"
#include <string>
#include <cstdint>
#include <memory>
#include <vector>

struct NumberNode : public ExpressionNode {
    uint64_t value;
    uint32_t width;
    bool is_unknown = false;

    explicit NumberNode(uint64_t val, uint32_t w = 32, bool unk = false)
        : value(val), width(w), is_unknown(unk) {}

    std::unique_ptr<ExpressionNode> clone() const override {
        return std::make_unique<NumberNode>(value, width, is_unknown);
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