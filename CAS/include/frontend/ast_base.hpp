#pragma once
#include <string>
#include <memory>

struct ASTNode {
    virtual ~ASTNode() = default;
};

struct ExpressionNode : public ASTNode {
    virtual std::unique_ptr<ExpressionNode> clone() const = 0;
};

struct StatementNode : public ASTNode {
    virtual std::unique_ptr<StatementNode> clone() const { return nullptr; }
};