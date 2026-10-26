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

    std::unique_ptr<ExpressionNode> parse_expression();
    std::unique_ptr<ExpressionNode> parse_primary();
    std::unique_ptr<StatementNode> parse_statement();

public:
    explicit Parser(Lexer& lex);
    std::vector<std::unique_ptr<ModuleNode>> parse_all();
};