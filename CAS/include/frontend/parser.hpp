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

struct ParsedCallArgs {
    std::vector<std::unique_ptr<ExpressionNode>> args;
    std::vector<std::string> arg_names;
};

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
    static std::unordered_map<std::string, std::shared_ptr<ClassNode>> class_map;
    static std::vector<std::shared_ptr<ClassNode>> global_classes;
    std::unordered_map<std::string, std::shared_ptr<CovergroupNode>> covergroup_map;
    std::vector<std::shared_ptr<CovergroupNode>> global_covergroups;
    ModuleNode* current_parsing_module = nullptr;

    std::unordered_set<std::string> known_interfaces;
    static std::unordered_map<std::string, std::shared_ptr<PackageNode>> global_packages;
    static std::unordered_map<std::string, std::shared_ptr<ConfigNode>> global_configs;
    static std::unordered_map<std::string, std::shared_ptr<UdpDefinitionNode>> global_udp_defs;
    static std::vector<std::string> global_wildcard_imports;
    static std::unordered_map<std::string, std::pair<std::string, std::string>> global_explicit_imports;
    static void clear_global_packages() {
        global_packages.clear();
        global_wildcard_imports.clear();
        global_explicit_imports.clear();
        global_configs.clear();
        global_udp_defs.clear();
        global_let_decls.clear();
        class_map.clear();
        global_classes.clear();
        global_forward_classes.clear();
        global_forward_interface_classes.clear();
    }

    bool is_package(const std::string& name) const {
        return global_packages.find(name) != global_packages.end();
    }

    bool is_builtin_type_name(const std::string& name) const {
        return name == "int" || name == "byte" || name == "shortint" || name == "longint" ||
               name == "integer" || name == "time" || name == "realtime" || name == "real" ||
               name == "shortreal" || name == "string" || name == "chandle" || name == "semaphore" ||
               name == "mailbox" || name == "bit" || name == "logic" || name == "reg" ||
               name == "wire" || name == "wand" || name == "wor" || name == "tri" ||
               name == "signed" || name == "unsigned" || name == "void";
    }

    static std::unordered_set<std::string> global_forward_classes;
    static std::unordered_set<std::string> global_forward_interface_classes;
    bool is_forward_class(const std::string& name) const;

    bool is_class(const std::string& name) const {
        if (is_known_class(name)) return true;
        if (is_forward_class(name)) return true;
        if (class_map.find(name) != class_map.end()) return true;
        auto hash_pos = name.find('#');
        if (hash_pos != std::string::npos) {
            std::string base = name.substr(0, hash_pos);
            while (!base.empty() && std::isspace(base.back())) base.pop_back();
            if (is_known_class(base) || is_forward_class(base) || class_map.find(base) != class_map.end()) return true;
        }
        return false;
    }
    bool is_type_identifier(const std::string& name) const {
        return is_builtin_type_name(name) || is_typedef(name) || is_class(name) || is_known_class(name) || is_forward_class(name);
    }
    std::unique_ptr<ClassNode> parse_class(bool is_interface_class = false, bool is_abstract = false);
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
    ClassNode* find_class(const std::string& name) const;
    bool is_known_class(const std::string& name) const;

    std::shared_ptr<EnumTypeNode> get_enum_typedef(const std::string& name) const {
        auto it = typedef_map.find(name);
        if (it != typedef_map.end()) return it->second;

        auto colon_pos = name.find("::");
        if (colon_pos != std::string::npos) {
            std::string pkg = name.substr(0, colon_pos);
            std::string type = name.substr(colon_pos + 2);
            auto pit = global_packages.find(pkg);
            if (pit != global_packages.end()) {
                auto tit = pit->second->enum_typedefs.find(type);
                if (tit != pit->second->enum_typedefs.end()) return tit->second;
            }
        }

        if (current_parsing_module) {
            auto eit = current_parsing_module->explicit_imports.find(name);
            if (eit != current_parsing_module->explicit_imports.end()) {
                auto pit = global_packages.find(eit->second.first);
                if (pit != global_packages.end()) {
                    auto tit = pit->second->enum_typedefs.find(eit->second.second);
                    if (tit != pit->second->enum_typedefs.end()) return tit->second;
                }
            }
            for (const auto& pkg_name : current_parsing_module->wildcard_imports) {
                auto pit = global_packages.find(pkg_name);
                if (pit != global_packages.end()) {
                    auto tit = pit->second->enum_typedefs.find(name);
                    if (tit != pit->second->enum_typedefs.end()) return tit->second;
                }
            }
        }
        for (const auto& pkg_name : global_wildcard_imports) {
            auto pit = global_packages.find(pkg_name);
            if (pit != global_packages.end()) {
                auto tit = pit->second->enum_typedefs.find(name);
                if (tit != pit->second->enum_typedefs.end()) return tit->second;
            }
        }
        return nullptr;
    }

    std::shared_ptr<StructTypeNode> get_struct_typedef(const std::string& name) const {
        auto it = struct_typedef_map.find(name);
        if (it != struct_typedef_map.end()) return it->second;

        auto colon_pos = name.find("::");
        if (colon_pos != std::string::npos) {
            std::string pkg = name.substr(0, colon_pos);
            std::string type = name.substr(colon_pos + 2);
            auto pit = global_packages.find(pkg);
            if (pit != global_packages.end()) {
                auto tit = pit->second->struct_typedefs.find(type);
                if (tit != pit->second->struct_typedefs.end()) return tit->second;
            }
        }

        if (current_parsing_module) {
            auto eit = current_parsing_module->explicit_imports.find(name);
            if (eit != current_parsing_module->explicit_imports.end()) {
                auto pit = global_packages.find(eit->second.first);
                if (pit != global_packages.end()) {
                    auto tit = pit->second->struct_typedefs.find(eit->second.second);
                    if (tit != pit->second->struct_typedefs.end()) return tit->second;
                }
            }
            for (const auto& pkg_name : current_parsing_module->wildcard_imports) {
                auto pit = global_packages.find(pkg_name);
                if (pit != global_packages.end()) {
                    auto tit = pit->second->struct_typedefs.find(name);
                    if (tit != pit->second->struct_typedefs.end()) return tit->second;
                }
            }
        }
        for (const auto& pkg_name : global_wildcard_imports) {
            auto pit = global_packages.find(pkg_name);
            if (pit != global_packages.end()) {
                auto tit = pit->second->struct_typedefs.find(name);
                if (tit != pit->second->struct_typedefs.end()) return tit->second;
            }
        }
        return nullptr;
    }

    bool is_typedef(const std::string& name) const {
        return get_enum_typedef(name) != nullptr || get_struct_typedef(name) != nullptr;
    }
    bool is_enum_typedef(const std::string& name) const {
        return get_enum_typedef(name) != nullptr;
    }
    bool is_struct_typedef(const std::string& name) const {
        return get_struct_typedef(name) != nullptr;
    }
    std::shared_ptr<PackageNode> parse_package();
    std::shared_ptr<ConfigNode> parse_config();
    std::shared_ptr<UdpDefinitionNode> parse_udp();
    bool is_udp(const std::string& name) const {
        return global_udp_defs.find(name) != global_udp_defs.end();
    }
    void parse_packages_only();
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

    std::shared_ptr<LetDeclNode> parse_let_declaration();
    LetPortItem parse_let_port_item();
    const LetDeclNode* find_visible_let(const std::string& name) const;
    std::unique_ptr<ExpressionNode> expand_let(const LetDeclNode* let_decl, std::vector<std::unique_ptr<ExpressionNode>> actual_args, const std::vector<std::string>& arg_names);
    std::unique_ptr<ExpressionNode> substitute_let_in_expr(const ExpressionNode* expr, const std::unordered_map<std::string, const ExpressionNode*>& subst);

    static std::unordered_map<std::string, std::shared_ptr<LetDeclNode>> global_let_decls;
    std::shared_ptr<PackageNode> current_parsing_package = nullptr;
    std::unordered_set<std::string> active_let_expansions;

    explicit Parser(Lexer& lex);

    std::vector<std::unique_ptr<ModuleNode>> parse_all();
    std::unique_ptr<FunctionNode> parse_function(bool is_pure = false, bool is_extern = false);
    std::unique_ptr<TaskNode> parse_task(bool is_pure = false, bool is_extern = false);
    void bind_out_of_body_function(std::unique_ptr<FunctionNode> fn);
    void bind_out_of_body_task(std::unique_ptr<TaskNode> t);
    std::unique_ptr<StatementNode> parse_statement();
    std::unique_ptr<StatementNode> parse_if_statement(DecisionModifier mod = DecisionModifier::NONE, std::string mod_file = "", size_t mod_line = 0);
    std::unique_ptr<StatementNode> parse_case_statement(DecisionModifier mod = DecisionModifier::NONE, std::string mod_file = "", size_t mod_line = 0);
    std::vector<std::unique_ptr<StatementNode>> parse_statement_or_block();
    ParsedCallArgs parse_call_argument_list();
    std::unique_ptr<ProcessNode> parse_always_ff();
    std::unique_ptr<ExpressionNode> parse_net_lvalue();
    std::shared_ptr<LoopGenerateNode> parse_generate_for(ModuleNode* mod);
    std::shared_ptr<IfGenerateNode> parse_generate_if(ModuleNode* mod);
    std::shared_ptr<CaseGenerateNode> parse_generate_case(ModuleNode* mod);
    void parse_generate_item(ModuleNode* mod, GenerateBlock* blk);

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
