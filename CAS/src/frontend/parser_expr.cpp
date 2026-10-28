#include "../../include/frontend/parser.hpp"
#include <stdexcept>

std::unique_ptr<ExpressionNode> Parser::parse_primary() {
    if (current.type == TokenType::TOKEN_NUMBER) {
        uint64_t a = current.num_val;
        uint64_t b = current.bval;
        uint32_t w = current.width;
        advance();
        return std::make_unique<NumberNode>(a, b, w);
    }

    if (current.type == TokenType::TOKEN_IDENTIFIER) {
        std::string name = current.text;
        advance();

        // Function call: identifier followed immediately by '('
        if (match(TokenType::TOKEN_LPAREN)) {
            std::vector<std::unique_ptr<ExpressionNode>> args;
            if (current.type != TokenType::TOKEN_RPAREN) {
                while (true) {
                    args.push_back(parse_expression());
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing function call arguments");
            return std::make_unique<FunctionCallNode>(name, std::move(args));
        }

        if (match(TokenType::TOKEN_LBRACKET)) {
            auto msb = parse_expression();
            if (match(TokenType::TOKEN_COLON)) {
                auto lsb = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing part-select range");
                return std::make_unique<IndexSelectNode>(name, std::move(msb), std::move(lsb));
            }
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing bit-select index");
            return std::make_unique<IndexSelectNode>(name, std::move(msb));
        }

        return std::make_unique<IdentifierNode>(name);
    }

    if (match(TokenType::TOKEN_LPAREN)) {
        auto expr = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing parenthesized expression");
        return expr;
    }

    if (match(TokenType::TOKEN_LBRACE)) {
        auto first_expr = parse_expression();

        if (match(TokenType::TOKEN_LBRACE)) {
            auto target = parse_expression();
            expect(TokenType::TOKEN_RBRACE, "Expected '}' closing inner replication target");
            expect(TokenType::TOKEN_RBRACE, "Expected '}' closing replication expression");
            return std::make_unique<ReplicateNode>(std::move(first_expr), std::move(target));
        }

        std::vector<std::unique_ptr<ExpressionNode>> elements;
        elements.push_back(std::move(first_expr));

        while (match(TokenType::TOKEN_COMMA)) {
            elements.push_back(parse_expression());
        }

        expect(TokenType::TOKEN_RBRACE, "Expected '}' closing concatenation expression");
        return std::make_unique<ConcatNode>(std::move(elements));
    }

    std::string loc = current.filename.empty() 
        ? ("line " + std::to_string(current.line)) 
        : (current.filename + ":" + std::to_string(current.line));
    throw std::runtime_error(loc + ": error: Unexpected expression token '" + current.text + "'");
}

std::unique_ptr<ExpressionNode> Parser::parse_unary() {
    // 1. Two-character composite unary reduction: ~&, ~|, ~^
    if (current.type == TokenType::TOKEN_TILDE) {
        advance();
        if (current.type == TokenType::TOKEN_AND) {
            advance();
            return std::make_unique<UnaryOpNode>("~&", parse_unary());
        }
        if (current.type == TokenType::TOKEN_OR_BIT) {
            advance();
            return std::make_unique<UnaryOpNode>("~|", parse_unary());
        }
        if (current.type == TokenType::TOKEN_XOR) {
            advance();
            return std::make_unique<UnaryOpNode>("~^", parse_unary());
        }
        return std::make_unique<UnaryOpNode>("~", parse_unary());
    }

    // 2. Two-character composite unary reduction: ^~
    if (current.type == TokenType::TOKEN_XOR) {
        advance();
        if (current.type == TokenType::TOKEN_TILDE) {
            advance();
            return std::make_unique<UnaryOpNode>("^~", parse_unary());
        }
        return std::make_unique<UnaryOpNode>("^", parse_unary());
    }

    // 3. Single-character unary reductions and prefix operators: &, |, !, +, -
    if (current.type == TokenType::TOKEN_AND        ||
        current.type == TokenType::TOKEN_OR_BIT    ||
        current.type == TokenType::TOKEN_NOT       ||
        current.type == TokenType::TOKEN_PLUS      ||
        current.type == TokenType::TOKEN_MINUS) {
        
        std::string op = current.text;
        advance();

        if (op == "-" && current.type == TokenType::TOKEN_NUMBER) {
            uint64_t a = current.num_val;
            uint64_t b = current.bval;
            uint32_t w = current.width;
            advance();
            uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
            uint64_t neg_a = (~a + 1) & mask;
            return std::make_unique<NumberNode>(neg_a, b, w);
        }

        return std::make_unique<UnaryOpNode>(op, parse_unary());
    }

    return parse_primary();
}

std::unique_ptr<ExpressionNode> Parser::parse_multiplicative() {
    auto lhs = parse_unary();

    while (current.type == TokenType::TOKEN_STAR) {
        std::string op = current.text;
        advance();
        auto rhs = parse_unary();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
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

    while (current.type == TokenType::TOKEN_SHL || current.type == TokenType::TOKEN_SHR || current.type == TokenType::TOKEN_ASHR) {
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

    while (current.type == TokenType::TOKEN_EQ       || current.type == TokenType::TOKEN_NEQ ||
           current.type == TokenType::TOKEN_CASE_EQ  || current.type == TokenType::TOKEN_CASE_NEQ) {
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

std::unique_ptr<ExpressionNode> Parser::parse_expression() {
    auto cond = parse_logical_or();

    if (match(TokenType::TOKEN_QUESTION)) {
        auto true_expr = parse_expression();
        expect(TokenType::TOKEN_COLON, "Expected ':' in ternary conditional expression");
        auto false_expr = parse_expression();
        return std::make_unique<TernaryOpNode>(std::move(cond), std::move(true_expr), std::move(false_expr));
    }
    return cond;
}