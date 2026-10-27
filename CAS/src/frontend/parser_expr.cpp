#include "../../include/frontend/parser.hpp"
#include <stdexcept>

std::unique_ptr<ExpressionNode> Parser::parse_primary() {
    if (current.type == TokenType::TOKEN_NUMBER) {
        uint64_t val = current.num_val;
        uint32_t w = current.bit_width;
        bool is_unk = (current.text.find('x') != std::string::npos || 
                       current.text.find('X') != std::string::npos ||
                       current.text.find('z') != std::string::npos || 
                       current.text.find('Z') != std::string::npos);
        advance();
        return std::make_unique<NumberNode>(val, w, is_unk);
    }

    if (current.type == TokenType::TOKEN_IDENTIFIER) {
        std::string name = current.text;
        advance();

        if (match(TokenType::TOKEN_LBRACKET)) {
            auto msb = parse_expression();
            if (match(TokenType::TOKEN_COLON)) {
                auto lsb = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' after part-select range");
                return std::make_unique<IndexSelectNode>(name, std::move(msb), std::move(lsb));
            } else {
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' after index expression");
                return std::make_unique<IndexSelectNode>(name, std::move(msb));
            }
        }
        return std::make_unique<IdentifierNode>(name);
    }

    if (match(TokenType::TOKEN_LPAREN)) {
        auto expr = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' after nested expression");
        return expr;
    }

    if (match(TokenType::TOKEN_LBRACE)) {
        std::vector<std::unique_ptr<ExpressionNode>> elements;
        while (true) {
            elements.push_back(parse_expression());
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_RBRACE, "Expected '}' closing concatenation expression");
        return std::make_unique<ConcatNode>(std::move(elements));
    }

    std::string loc = current.filename.empty() 
        ? ("line " + std::to_string(current.line)) 
        : (current.filename + ":" + std::to_string(current.line));
    throw std::runtime_error(loc + ": error: Unexpected token '" + current.text + "' in expression");
}

std::unique_ptr<ExpressionNode> Parser::parse_unary() {
    if (current.type == TokenType::TOKEN_NOT || current.type == TokenType::TOKEN_TILDE) {
        char op = (current.type == TokenType::TOKEN_NOT) ? '!' : '~';
        advance();
        auto operand = parse_unary();
        return std::make_unique<UnaryOpNode>(op, std::move(operand));
    }
    return parse_primary();
}

std::unique_ptr<ExpressionNode> Parser::parse_multiplicative() {
    auto lhs = parse_unary();
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_additive() {
    auto lhs = parse_multiplicative();
    while (current.type == TokenType::TOKEN_PLUS || current.type == TokenType::TOKEN_MINUS) {
        std::string op = current.text;
        advance();
        auto rhs = parse_multiplicative();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_shift() {
    auto lhs = parse_additive();
    while (current.type == TokenType::TOKEN_SHL || current.type == TokenType::TOKEN_SHR) {
        std::string op = current.text;
        advance();
        auto rhs = parse_additive();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_relational() {
    auto lhs = parse_shift();
    while (current.type == TokenType::TOKEN_LT || current.type == TokenType::TOKEN_LE ||
           current.type == TokenType::TOKEN_GT || current.type == TokenType::TOKEN_GE) {
        std::string op = current.text;
        advance();
        auto rhs = parse_shift();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_equality() {
    auto lhs = parse_relational();
    while (current.type == TokenType::TOKEN_EQ || current.type == TokenType::TOKEN_NEQ) {
        std::string op = current.text;
        advance();
        auto rhs = parse_relational();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_bitwise_and() {
    auto lhs = parse_equality();
    while (current.type == TokenType::TOKEN_AND) {
        std::string op = current.text;
        advance();
        auto rhs = parse_equality();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_bitwise_xor() {
    auto lhs = parse_bitwise_and();
    while (current.type == TokenType::TOKEN_XOR) {
        std::string op = current.text;
        advance();
        auto rhs = parse_bitwise_and();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_bitwise_or() {
    auto lhs = parse_bitwise_xor();
    while (current.type == TokenType::TOKEN_OR_BIT) {
        std::string op = current.text;
        advance();
        auto rhs = parse_bitwise_xor();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_logical_and() {
    auto lhs = parse_bitwise_or();
    while (current.type == TokenType::TOKEN_LOGICAL_AND) {
        std::string op = current.text;
        advance();
        auto rhs = parse_bitwise_or();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_logical_or() {
    auto lhs = parse_logical_and();
    while (current.type == TokenType::TOKEN_LOGICAL_OR) {
        std::string op = current.text;
        advance();
        auto rhs = parse_logical_and();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_ternary() {
    auto cond = parse_logical_or();
    if (match(TokenType::TOKEN_QUESTION)) {
        auto true_expr = parse_expression();
        expect(TokenType::TOKEN_COLON, "Expected ':' in ternary conditional operator");
        auto false_expr = parse_ternary();
        return std::make_unique<TernaryOpNode>(std::move(cond), std::move(true_expr), std::move(false_expr));
    }
    return cond;
}

std::unique_ptr<ExpressionNode> Parser::parse_expression() {
    return parse_ternary();
}