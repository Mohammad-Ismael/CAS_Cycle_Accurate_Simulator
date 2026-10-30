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
    std::unordered_map<std::string, std::shared_ptr<ClassNode>> class_map;
    std::vector<std::shared_ptr<ClassNode>> global_classes;
    std::unordered_map<std::string, std::shared_ptr<CovergroupNode>> covergroup_map;
    std::vector<std::shared_ptr<CovergroupNode>> global_covergroups;
    ModuleNode* current_parsing_module = nullptr;

    std::unordered_set<std::string> known_interfaces;
    bool is_class(const std::string& name) const {
        if (class_map.find(name) != class_map.end()) return true;
        auto hash_pos = name.find('#');
        if (hash_pos != std::string::npos) {
            std::string base = name.substr(0, hash_pos);
            while (!base.empty() && std::isspace(base.back())) base.pop_back();
            if (class_map.find(base) != class_map.end()) return true;
        }
        return false;
    }
    std::unique_ptr<ClassNode> parse_class();
    std::string parse_class_type_and_specialize();
    uint64_t eval_const_expr(ExpressionNode* expr);
    uint64_t eval_const_expr_with_params(ExpressionNode* expr, const std::unordered_map<std::string, uint64_t>& params);
    bool is_covergroup(const std::string& name) const {
        return covergroup_map.find(name) != covergroup_map.end();
    }
    std::unique_ptr<CovergroupNode> parse_covergroup();
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

    bool is_assoc_key_token(const Token& tok) const {
        if (tok.type == TokenType::TOKEN_LOGIC || tok.type == TokenType::TOKEN_REG ||
            tok.type == TokenType::TOKEN_WIRE || tok.type == TokenType::TOKEN_INTEGER ||
            tok.type == TokenType::TOKEN_STAR) {
            return true;
        }
        if (tok.type == TokenType::TOKEN_IDENTIFIER) {
            if (tok.text == "string" || tok.text == "int" || tok.text == "byte" ||
                tok.text == "shortint" || tok.text == "longint" || tok.text == "bit") {
                return true;
            }
            if (is_typedef(tok.text)) {
                return true;
            }
        }
        return false;
    }
    void parse_assoc_key_type(std::string& key_type, uint32_t& key_width);

    std::unordered_map<std::string, std::shared_ptr<PropertyNode>> property_map;
    std::unordered_map<std::string, std::shared_ptr<SequenceNode>> sequence_map;
    std::unordered_map<std::string, std::shared_ptr<DpiImportNode>> dpi_import_map;
    std::vector<std::shared_ptr<DpiImportNode>> global_dpi_imports;
    std::unordered_map<std::string, std::shared_ptr<DpiExportNode>> dpi_export_map;
    std::vector<std::shared_ptr<DpiExportNode>> global_dpi_exports;

    bool is_property(const std::string& name) const {
        return property_map.find(name) != property_map.end();
    }
    bool is_sequence(const std::string& name) const {
        return sequence_map.find(name) != sequence_map.end();
    }
    bool is_dpi_import(const std::string& name) const {
        return dpi_import_map.find(name) != dpi_import_map.end();
    }
    bool is_dpi_export(const std::string& name) const {
        return dpi_export_map.find(name) != dpi_export_map.end();
    }
    std::unique_ptr<PropertyNode> parse_property();
    std::unique_ptr<SequenceNode> parse_sequence();
    std::unique_ptr<AssertPropertyStatementNode> parse_assert_property(const std::string& label = "");
    std::unique_ptr<DpiImportNode> parse_dpi_import();
    std::unique_ptr<DpiExportNode> parse_dpi_export();

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
    std::unique_ptr<ExpressionNode> parse_sequence_element();
    std::unique_ptr<ExpressionNode> parse_sequence_expression();
};
