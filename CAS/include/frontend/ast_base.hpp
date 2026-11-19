#pragma once
#include <string>
#include <memory>
#include <cstdint>

enum class ExprKind : uint8_t {
    OTHER = 0,
    NUMBER,
    IDENTIFIER,
    BINARY_OP,
    UNARY_OP,
    CONCAT,
    REPLICATE,
    INDEX_SELECT,
    FUNCTION_CALL
};

struct ASTNode {
    virtual ~ASTNode() = default;
};

struct ExpressionNode : public ASTNode {
    ExprKind expr_kind = ExprKind::OTHER;
    virtual std::unique_ptr<ExpressionNode> clone() const = 0;
};

struct StatementNode : public ASTNode {
    virtual std::unique_ptr<StatementNode> clone() const { return nullptr; }
};
