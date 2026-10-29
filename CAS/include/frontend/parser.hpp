#pragma once
#include "lexer.hpp"
#include "ast_base.hpp"
#include "ast_expr.hpp"
#include "ast_stmt.hpp"
#include "ast_module.hpp"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

class Parser {
private:
    Lexer& lexer;
    Token current;
    std::vector<ParameterDef> global_parameters;

    bool has_peek = false;
    Token peeked;
    void advance();
    Token peek_token();
    bool match(TokenType type);
    void expect(TokenType type, const std::string& err_msg);

public:
    std::unordered_map<std::string, std::shared_ptr<EnumTypeNode>> typedef_map;
    std::unordered_map<std::string, std::shared_ptr<StructTypeNode>> struct_typedef_map;
    std::vector<std::unique_ptr<TypedefNode>> global_typedefs;

    std::unordered_set<std::string> known_interfaces;
    bool is_interface_type(const std::string& name) const {
        return known_interfaces.find(name) != known_interfaces.end();
    }
    bool is_typedef(const std::string& name) const {
        return typedef_map.find(name) != typedef_map.end() || struct_typedef_map.find(name) != struct_typedef_map.end();
    }
    bool is_enum_typedef(const std::string& name) const {
        return typedef_map.find(name) != typedef_map.end();
    }
    bool is_struct_typedef(const std::string& name) const {
        return struct_typedef_map.find(name) != struct_typedef_map.end();
    }
    std::unique_ptr<TypedefNode> parse_typedef();
    std::string parse_skew_spec();

    explicit Parser(Lexer& lex);

    std::vector<std::unique_ptr<ModuleNode>> parse_all();
    std::unique_ptr<FunctionNode> parse_function();
    std::unique_ptr<TaskNode> parse_task();
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