#pragma once
#include "lexer.hpp"
#include "ast_base.hpp"
#include "ast_expr.hpp"
#include "ast_stmt.hpp"
#include "ast_module.hpp"
#include <memory>
#include <vector>
#include <string>

class Parser {
private:
    Lexer& lexer;
    Token current;
    std::vector<ParameterDef> global_parameters;

    void advance();
    bool match(TokenType type);
    void expect(TokenType type, const std::string& err_msg);

public:
    explicit Parser(Lexer& lex);

    std::vector<std::unique_ptr<ModuleNode>> parse_all();
    std::unique_ptr<FunctionNode> parse_function();
    std::unique_ptr<StatementNode> parse_statement();
    std::vector<std::unique_ptr<StatementNode>> parse_statement_or_block();
    std::unique_ptr<ProcessNode> parse_always_ff();

    std::unique_ptr<ExpressionNode> parse_expression();
    std::unique_ptr<ExpressionNode> parse_logical_or();
    std::unique_ptr<ExpressionNode> parse_logical_and();
    std::unique_ptr<ExpressionNode> parse_bitwise_or();
    std::unique_ptr<ExpressionNode> parse_bitwise_xor();
    std::unique_ptr<ExpressionNode> parse_bitwise_and();
    std::unique_ptr<ExpressionNode> parse_equality();
    std::unique_ptr<ExpressionNode> parse_relational();
    std::unique_ptr<ExpressionNode> parse_shift();
    std::unique_ptr<ExpressionNode> parse_additive();
    std::unique_ptr<ExpressionNode> parse_multiplicative();
    std::unique_ptr<ExpressionNode> parse_unary();
    std::unique_ptr<ExpressionNode> parse_primary();
};