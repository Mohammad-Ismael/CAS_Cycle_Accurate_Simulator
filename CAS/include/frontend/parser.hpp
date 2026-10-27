#pragma once
#include "lexer.hpp"
#include "ast.hpp"
#include <memory>
#include <vector>
#include <string>

class Parser {
private:
    Lexer& lexer;
    Token current;

    std::vector<ParameterDef> global_parameters;

    void advance();
    bool match(TokenType t);
    void expect(TokenType t, const std::string& err);

    std::unique_ptr<ExpressionNode> parse_primary();
    std::unique_ptr<ExpressionNode> parse_unary();
    std::unique_ptr<ExpressionNode> parse_multiplicative();
    std::unique_ptr<ExpressionNode> parse_additive();
    std::unique_ptr<ExpressionNode> parse_shift();
    std::unique_ptr<ExpressionNode> parse_relational();
    std::unique_ptr<ExpressionNode> parse_equality();
    std::unique_ptr<ExpressionNode> parse_bitwise_and();
    std::unique_ptr<ExpressionNode> parse_bitwise_xor();
    std::unique_ptr<ExpressionNode> parse_bitwise_or();
    std::unique_ptr<ExpressionNode> parse_logical_and();
    std::unique_ptr<ExpressionNode> parse_logical_or();
    std::unique_ptr<ExpressionNode> parse_ternary();
    std::unique_ptr<ExpressionNode> parse_expression();

    std::unique_ptr<StatementNode> parse_statement();
    std::vector<std::unique_ptr<StatementNode>> parse_statement_or_block();
    std::unique_ptr<ProcessNode> parse_always_ff();
    std::unique_ptr<TaskNode> parse_task();
    std::unique_ptr<ModuleNode> parse_module();

public:
    explicit Parser(Lexer& lex);
    std::vector<std::unique_ptr<ModuleNode>> parse_all();
    const std::vector<ParameterDef>& get_global_parameters() const { return global_parameters; }
};