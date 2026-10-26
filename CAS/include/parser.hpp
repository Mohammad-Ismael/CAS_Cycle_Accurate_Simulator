#pragma once
#include "lexer.hpp"
#include <memory>

class Parser {
private:
    Lexer& lexer;
    Token current;

    void advance();
    bool match(TokenType t);
    void expect(TokenType t, const std::string& err);

    std::unique_ptr<ExpressionNode> parse_primary();
    std::unique_ptr<ExpressionNode> parse_unary();
    std::unique_ptr<ExpressionNode> parse_shift();
    std::unique_ptr<ExpressionNode> parse_additive();
    std::unique_ptr<ExpressionNode> parse_relational();
    std::unique_ptr<ExpressionNode> parse_equality();
    std::unique_ptr<ExpressionNode> parse_bitwise_and();
    std::unique_ptr<ExpressionNode> parse_bitwise_xor();
    std::unique_ptr<ExpressionNode> parse_bitwise_or();
    std::unique_ptr<ExpressionNode> parse_ternary();
    std::unique_ptr<ExpressionNode> parse_expression();

    std::unique_ptr<StatementNode> parse_statement();

public:
    explicit Parser(Lexer& lex);
    std::vector<std::unique_ptr<ModuleNode>> parse_all();
};