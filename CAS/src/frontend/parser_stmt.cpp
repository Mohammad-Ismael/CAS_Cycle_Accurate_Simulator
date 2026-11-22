#include "../../include/frontend/parser.hpp"
#include <stdexcept>
#include <functional>
#include <cmath>

std::unique_ptr<StatementNode> Parser::parse_statement() {
    std::string stmt_label = "";
    while (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON) {
        stmt_label = current.text;
        advance(); // consume ident
        advance(); // consume ':'
    }

    // Null statement (§12.3)
    if (match(TokenType::TOKEN_SEMICOLON)) {
        return std::make_unique<BlockStatementNode>(std::vector<std::unique_ptr<StatementNode>>());
    }

    // Diagnostic [CAS-DTINPCIL]: defparam inside procedural scopes
    if (current.type == TokenType::TOKEN_DEFPARAM) {
        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: 'defparam' statement is illegal inside procedural scope per IEEE 1800-2023 section 23.10.1");
    }

    // Diagnostic [CAS-DTINPCIL]: alias inside procedural scopes
    if (current.type == TokenType::TOKEN_ALIAS) {
        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: 'alias' construct is not permitted in procedural context per IEEE 1800-2023 section 10.6");
    }

    if (current.type == TokenType::TOKEN_LET) {
        auto let_decl = parse_let_declaration();
        if (let_decl) {
            if (current_parsing_module) {
                current_parsing_module->let_decl_map[let_decl->name] = let_decl;
                current_parsing_module->let_decls.push_back(let_decl);
            } else {
                global_let_decls[let_decl->name] = let_decl;
            }
            return std::make_unique<LetStatementNode>(let_decl);
        }
    }

    if (current.type == TokenType::TOKEN_TYPEDEF) {
        auto td = parse_typedef();
        if (td && current_parsing_module) {
            current_parsing_module->typedefs.push_back(std::move(td));
        }
        return std::make_unique<BlockStatementNode>(std::vector<std::unique_ptr<StatementNode>>());
    }

    if (current.type == TokenType::TOKEN_IMPORT) {
        if (peek_token().type == TokenType::TOKEN_STRING) {
            auto dpi = parse_dpi_import();
            if (dpi && current_parsing_module) {
                std::shared_ptr<DpiImportNode> sdpi = std::move(dpi);
                current_parsing_module->dpi_imports.push_back(sdpi);
                current_parsing_module->dpi_import_map[sdpi->sv_name] = sdpi.get();
            }
            return std::make_unique<BlockStatementNode>(std::vector<std::unique_ptr<StatementNode>>());
        } else {
            advance(); // consume 'import'
            std::vector<std::pair<std::string, std::string>> imps;
            while (true) {
                std::string pkg_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected package name in import");
                expect(TokenType::TOKEN_COLON_COLON, "Expected '::' after package name in import");
                if (match(TokenType::TOKEN_STAR)) {
                    imps.push_back({pkg_name, "*"});
                    if (current_parsing_module) current_parsing_module->wildcard_imports.push_back(pkg_name);
                } else {
                    std::string item_name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected item name or '*' in import");
                    imps.push_back({pkg_name, item_name});
                    if (current_parsing_module) current_parsing_module->explicit_imports[item_name] = {pkg_name, item_name};
                }
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after import declaration");
            return std::make_unique<ImportStatementNode>(std::move(imps));
        }
    }

    // Diagnostic [CAS-DTINPCIL]: gate or UDP primitive inside procedural scopes
    if (current.type == TokenType::TOKEN_GATE_AND || current.type == TokenType::TOKEN_GATE_OR ||
        current.type == TokenType::TOKEN_GATE_XOR || current.type == TokenType::TOKEN_GATE_NAND ||
        current.type == TokenType::TOKEN_GATE_NOR || current.type == TokenType::TOKEN_GATE_XNOR ||
        current.type == TokenType::TOKEN_GATE_NOT || current.type == TokenType::TOKEN_GATE_BUF ||
        current.type == TokenType::TOKEN_GATE_BUFIF0 || current.type == TokenType::TOKEN_GATE_BUFIF1 ||
        current.type == TokenType::TOKEN_GATE_NOTIF0 || current.type == TokenType::TOKEN_GATE_NOTIF1 ||
        current.type == TokenType::TOKEN_PRIMITIVE ||
        (current.type == TokenType::TOKEN_IDENTIFIER && Parser::global_udp_defs.find(current.text) != Parser::global_udp_defs.end())) {
        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: gate or primitive instantiation is not permitted inside procedural scope per IEEE 1800-2023 section 28 & 29");
    }

    // Diagnostic [CAS-DTINPCIL]: specify block inside procedural scopes
    if (current.type == TokenType::TOKEN_SPECIFY) {
        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: 'specify' block is not permitted inside procedural scope per IEEE 1800-2023 section 32");
    }

    // Diagnostic [CAS-DTINPCIL]: timing check system tasks outside specify block
    if (current.type == TokenType::TOKEN_SYS_SETUP || current.type == TokenType::TOKEN_SYS_HOLD ||
        current.type == TokenType::TOKEN_SYS_WIDTH || current.type == TokenType::TOKEN_SYS_RECOVERY) {
        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: timing check system task '" + current.text + "' is not permitted outside specify block per IEEE 1800-2023 section 31.8 & 32.7");
    }

    // Decision Modifiers: unique, unique0, priority (§12.5)
    if (current.type == TokenType::TOKEN_UNIQUE ||
        current.type == TokenType::TOKEN_UNIQUE0 ||
        current.type == TokenType::TOKEN_PRIORITY) {
        DecisionModifier dec_mod = DecisionModifier::NONE;
        std::string mod_text = current.text;
        std::string mod_file = current.filename;
        size_t mod_line = current.line;
        if (match(TokenType::TOKEN_UNIQUE)) dec_mod = DecisionModifier::UNIQUE;
        else if (match(TokenType::TOKEN_UNIQUE0)) dec_mod = DecisionModifier::UNIQUE0;
        else if (match(TokenType::TOKEN_PRIORITY)) dec_mod = DecisionModifier::PRIORITY;

        if (current.type == TokenType::TOKEN_IF) {
            return parse_if_statement(dec_mod, mod_file, mod_line);
        } else if (current.type == TokenType::TOKEN_CASE ||
                   current.type == TokenType::TOKEN_CASEZ ||
                   current.type == TokenType::TOKEN_CASEX) {
            return parse_case_statement(dec_mod, mod_file, mod_line);
        } else {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: '" + mod_text + "' modifier can only prefix an 'if' or 'case' statement per IEEE 1800-2023 section 12.5");
        }
    }

    // 0. Handle 'begin ... end' block statements
    if (match(TokenType::TOKEN_BEGIN)) {
        std::string begin_name = "";
        if (match(TokenType::TOKEN_COLON)) {
            begin_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected block name after 'begin :'");
        }
        enter_scope();
        std::vector<std::unique_ptr<StatementNode>> stmts;
        while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
            stmts.push_back(parse_statement());
        }
        expect(TokenType::TOKEN_END, "Expected 'end' closing begin block");
        if (match(TokenType::TOKEN_COLON)) {
            std::string end_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected block name after 'end :'");
            if (!begin_name.empty() && end_name != begin_name) {
                throw std::runtime_error("Block end label '" + end_name + "' does not match begin label '" + begin_name + "'");
            }
        }
        match(TokenType::TOKEN_SEMICOLON);
        exit_scope();
        return std::make_unique<BlockStatementNode>(std::move(stmts), begin_name);
    }

    // Return Statement (§13.4.1)
    if (match(TokenType::TOKEN_RETURN)) {
        if (in_fork_depth > 0) {
            throw std::runtime_error("Return statement is not permitted inside a fork-join block per IEEE 1800-2023 9.3.3");
        }
        std::unique_ptr<ExpressionNode> ret_expr = nullptr;
        if (current.type != TokenType::TOKEN_SEMICOLON) {
            ret_expr = parse_expression();
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after return");
        if (current_parsing_function && (current_parsing_function->return_type_name == "void" || current_parsing_function->return_width == 0) && ret_expr != nullptr) {
            throw std::runtime_error("Void function cannot return an expression per IEEE 1800-2023 13.4.1");
        }
        if (current_parsing_function && dynamic_cast<NullNode*>(ret_expr.get())) {
            std::string rt = current_parsing_function->return_type_name;
            bool ok = (!rt.empty() && (is_class(rt) || is_forward_class(rt) || is_interface_type(rt) || rt == "chandle" || rt == "event"));
            if (!ok) {
                throw std::runtime_error("A non-class function cannot return 'null' per IEEE 1800-2023 §8.4");
            }
        }
        return std::make_unique<ReturnStatementNode>(std::move(ret_expr));
    }

    // Break Statement (§12.8)
    if (match(TokenType::TOKEN_BREAK)) {
        std::string f = current.filename;
        size_t l = current.line;
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after break");
        return std::make_unique<BreakStatementNode>(f, l);
    }

    // Continue Statement (§12.8)
    if (match(TokenType::TOKEN_CONTINUE)) {
        std::string f = current.filename;
        size_t l = current.line;
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after continue");
        return std::make_unique<ContinueStatementNode>(f, l);
    }

    // Optional Procedural Lifetime / Variable Qualifiers (§6.8, §8.4)
    bool is_stmt_automatic = false;
    bool is_stmt_static = false;
    bool is_stmt_const = false;
    if (match(TokenType::TOKEN_AUTOMATIC) || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "automatic")) {
        is_stmt_automatic = true;
        if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "automatic") advance();
    } else if (match(TokenType::TOKEN_STATIC) || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "static")) {
        is_stmt_static = true;
        if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "static") advance();
    }
    if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "const") {
        is_stmt_const = true;
        advance();
    }
    if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "var") {
        advance();
    }

    // Virtual Interface Variable Declaration (§25.9)
    if (match(TokenType::TOKEN_VIRTUAL)) {
        match(TokenType::TOKEN_INTERFACE);
        std::string iface_name = "";
        std::string modport_name = "";
        if (current.type == TokenType::TOKEN_IDENTIFIER || is_interface_type(current.text)) {
            iface_name = current.text;
            advance();
            if (match(TokenType::TOKEN_HASH)) {
                if (match(TokenType::TOKEN_LPAREN)) {
                    int pdepth = 1;
                    while (pdepth > 0 && current.type != TokenType::TOKEN_EOF) {
                        if (current.type == TokenType::TOKEN_LPAREN) pdepth++;
                        else if (current.type == TokenType::TOKEN_RPAREN) {
                            pdepth--;
                            if (pdepth == 0) {
                                advance();
                                break;
                            }
                        }
                        advance();
                    }
                }
            }
            if (match(TokenType::TOKEN_DOT)) {
                modport_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected modport name after '.'");
            }
        }
        std::string vname = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected variable identifier");
        while (match(TokenType::TOKEN_LBRACKET)) {
            while (!match(TokenType::TOKEN_RBRACKET) && current.type != TokenType::TOKEN_EOF) {
                advance();
            }
        }
        std::unique_ptr<ExpressionNode> init_expr = nullptr;
        if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
            init_expr = parse_expression();
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after variable declaration");
        auto var_stmt = std::make_unique<VarDeclStatementNode>(vname, 64, false, iface_name);
        var_stmt->is_virtual_interface = true;
        var_stmt->interface_name = iface_name;
        var_stmt->modport_name = modport_name;
        var_stmt->init_expr = std::move(init_expr);
        return var_stmt;
    }

    // Covergroup Handle Variable Declaration (§19)
    if (current.type == TokenType::TOKEN_IDENTIFIER && is_covergroup(current.text)) {
        std::string cg_type = current.text;
        advance();
        std::string vname = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected variable identifier");
        std::unique_ptr<ExpressionNode> init_expr = nullptr;
        if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
            init_expr = parse_expression();
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after variable declaration");
        auto var_stmt = std::make_unique<VarDeclStatementNode>(vname, 64, false, cg_type);
        var_stmt->is_covergroup_handle = true;
        var_stmt->covergroup_type_name = cg_type;
        var_stmt->init_expr = std::move(init_expr);
        return var_stmt;
    }

    // Subroutine call or assignment via package / class scope (e.g. pkg::task(...) or Cls::s_pub(); or Cls::Inner::task() or Pkg::var = ...)
    if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON_COLON) {
        size_t look = 1;
        while (peek_token_n(look).type == TokenType::TOKEN_COLON_COLON && peek_token_n(look + 1).type == TokenType::TOKEN_IDENTIFIER) {
            look += 2;
        }
        TokenType nxt = peek_token_n(look).type;
        if (nxt == TokenType::TOKEN_LPAREN || nxt == TokenType::TOKEN_SEMICOLON) {
            std::string full_name = current.text;
            advance();
            while (match(TokenType::TOKEN_COLON_COLON)) {
                full_name += "::" + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '::'");
            }
            size_t last_col = full_name.rfind("::");
            std::string scope_name = (last_col != std::string::npos) ? full_name.substr(0, last_col) : "";
            std::string task_name = (last_col != std::string::npos) ? full_name.substr(last_col + 2) : full_name;
            std::vector<std::unique_ptr<ExpressionNode>> args;
            std::vector<std::string> arg_names;
            if (match(TokenType::TOKEN_LPAREN)) {
                auto parsed = parse_call_argument_list();
                args = std::move(parsed.args);
                arg_names = std::move(parsed.arg_names);
            }
            if (match(TokenType::TOKEN_DOT)) {
                std::string chained = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.'");
                while (true) {
                    if (match(TokenType::TOKEN_LPAREN)) {
                        auto parsed_sub = parse_call_argument_list();
                    } else if (match(TokenType::TOKEN_LBRACKET)) {
                        parse_expression();
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in index select");
                    }
                    if (match(TokenType::TOKEN_DOT)) {
                        chained += "." + current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.'");
                        continue;
                    }
                    break;
                }
                if (match(TokenType::TOKEN_EQUAL)) {
                    auto rhs = parse_expression();
                    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assignment");
                    return std::make_unique<BlockingAssignNode>(full_name + "." + chained, std::move(rhs));
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after statement");
                return std::make_unique<TaskCallNode>(task_name + "." + chained, std::move(args), scope_name, std::move(arg_names));
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after subroutine call");
            return std::make_unique<TaskCallNode>(task_name, std::move(args), scope_name, std::move(arg_names));
        } else if (nxt == TokenType::TOKEN_EQUAL || nxt == TokenType::TOKEN_ASSIGN) {
            std::string full_name = current.text;
            advance();
            while (match(TokenType::TOKEN_COLON_COLON)) {
                full_name += "::" + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '::'");
            }
            advance(); // '='
            auto rhs = parse_expression();
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assignment");
            return std::make_unique<BlockingAssignNode>(full_name, std::move(rhs));
        } else if (nxt == TokenType::TOKEN_LE) {
            std::string full_name = current.text;
            advance();
            while (match(TokenType::TOKEN_COLON_COLON)) {
                full_name += "::" + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '::'");
            }
            advance(); // '<='
            auto rhs = parse_expression();
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after non-blocking assignment");
            return std::make_unique<NonBlockingAssignNode>(full_name, std::move(rhs));
        } else if (nxt == TokenType::TOKEN_DOT) {
            std::string full_name = current.text;
            advance();
            while (match(TokenType::TOKEN_COLON_COLON)) {
                full_name += "::" + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '::'");
            }
            while (match(TokenType::TOKEN_DOT)) {
                full_name += "." + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.'");
            }
            if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assignment");
                return std::make_unique<BlockingAssignNode>(full_name, std::move(rhs));
            } else if (match(TokenType::TOKEN_LE)) {
                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assignment");
                return std::make_unique<NonBlockingAssignNode>(full_name, std::move(rhs));
            }
        }
    }

    // Class Handle Variable Declaration (§8.4) or Static Scope Resolution (§8.25)
    // Or lookahead for user-defined types (e.g. unknown class / interface type declared in another file)
    std::string maybe_cls_ident = current.text;
    if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON_COLON) {
        maybe_cls_ident += "::" + peek_token_n(2).text;
    }
    bool is_type_param = false;
    if (current_parsing_class && current.type == TokenType::TOKEN_IDENTIFIER) {
        for (const auto& p : current_parsing_class->parameters) {
            if (p.name == current.text) {
                is_type_param = true;
                break;
            }
        }
    }
    if (current.type == TokenType::TOKEN_IDENTIFIER && is_module_type_parameter(current.text)) {
        is_type_param = true;
    }
    bool is_class_type = (current.type == TokenType::TOKEN_IDENTIFIER && (is_class(current.text) || is_known_class(current.text) || is_class(maybe_cls_ident) || is_known_class(maybe_cls_ident) || is_type_param));
    bool is_udt_decl = false;
    if (current.type == TokenType::TOKEN_IDENTIFIER && current.text != "var" && !is_type_identifier(current.text) &&
        !is_covergroup(current.text) && !is_interface_type(current.text)) {
        TokenType next_tok = peek_token().type;
        if (next_tok == TokenType::TOKEN_IDENTIFIER || next_tok == TokenType::TOKEN_HASH) {
            is_udt_decl = true;
        } else if (next_tok == TokenType::TOKEN_COLON_COLON && (peek_token_n(3).type == TokenType::TOKEN_IDENTIFIER || peek_token_n(3).type == TokenType::TOKEN_HASH)) {
            is_udt_decl = true;
        }
    }

    if (is_class_type || is_udt_decl) {
        std::string class_type = parse_class_type_and_specialize();
        if (class_type.find("::") != std::string::npos && match(TokenType::TOKEN_LPAREN)) {
            size_t cpos = class_type.rfind("::");
            std::string s_scope = class_type.substr(0, cpos);
            std::string s_method = class_type.substr(cpos + 2);
            auto parsed = parse_call_argument_list();
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after static method call");
            return std::make_unique<TaskCallNode>(s_method, std::move(parsed.args), s_scope, std::move(parsed.arg_names));
        }
        if (class_type.find("::") != std::string::npos && match(TokenType::TOKEN_SEMICOLON)) {
            size_t cpos = class_type.rfind("::");
            std::string s_scope = class_type.substr(0, cpos);
            std::string s_method = class_type.substr(cpos + 2);
            return std::make_unique<TaskCallNode>(s_method, std::vector<std::unique_ptr<ExpressionNode>>(), s_scope);
        }
        if (match(TokenType::TOKEN_COLON_COLON)) {
            std::string member = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected member name after '::'");
            if (current.type == TokenType::TOKEN_IDENTIFIER) {
                class_type = class_type + "::" + member;
            } else if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assignment");
                return std::make_unique<BlockingAssignNode>(class_type + "::" + member, std::move(rhs));
            } else if (match(TokenType::TOKEN_LPAREN)) {
                auto parsed = parse_call_argument_list();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after static method call");
                return std::make_unique<TaskCallNode>(member, std::move(parsed.args), class_type, std::move(parsed.arg_names));
            } else {
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after static member statement");
                return std::make_unique<TaskCallNode>(member, std::vector<std::unique_ptr<ExpressionNode>>(), class_type);
            }
        }
        std::vector<std::unique_ptr<StatementNode>> vstmts;
        while (true) {
            std::string vname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected variable identifier");
            uint32_t arr_size = 0;
            if (match(TokenType::TOKEN_LBRACKET)) {
                if (current.type == TokenType::TOKEN_NUMBER) {
                    arr_size = (uint32_t)current.num_val;
                    advance();
                }
                while (current.type != TokenType::TOKEN_RBRACKET && current.type != TokenType::TOKEN_EOF) advance();
                match(TokenType::TOKEN_RBRACKET);
            }
            std::unique_ptr<ExpressionNode> init_expr = nullptr;
            if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                init_expr = parse_expression();
            }
            bool is_actual_class = is_class(class_type) || is_known_class(class_type) || (current_parsing_class && is_type_param);
            if (!is_actual_class && class_type.find("::") != std::string::npos) {
                size_t p = class_type.rfind("::");
                std::string base_cls = class_type.substr(p + 2);
                if (is_class(base_cls) || is_known_class(base_cls)) is_actual_class = true;
            }
            uint32_t var_w = is_actual_class ? 64 : 0;
            if (!is_actual_class) {
                std::string res_t = resolve_imported_type_name(class_type);
                auto st = get_struct_typedef(res_t.empty() ? class_type : res_t);
                if (st) var_w = st->total_width;
                else {
                    SimpleTypedef st_info;
                    if (get_simple_typedef(res_t.empty() ? class_type : res_t, st_info)) {
                        var_w = st_info.width;
                    }
                }
                current_scope_var_types[vname] = class_type;
            }
            if (arr_size > 0) {
                for (uint32_t i = 0; i < arr_size; ++i) {
                    auto elem_stmt = std::make_unique<VarDeclStatementNode>(vname + "[" + std::to_string(i) + "]", var_w, false, class_type);
                    if (is_actual_class) {
                        elem_stmt->is_class_handle = true;
                        elem_stmt->class_type_name = class_type;
                    }
                    vstmts.push_back(std::move(elem_stmt));
                }
            }
            auto var_stmt = std::make_unique<VarDeclStatementNode>(vname, var_w, false, class_type);
            if (is_actual_class) {
                var_stmt->is_class_handle = true;
                var_stmt->class_type_name = class_type;
            }
            var_stmt->init_expr = std::move(init_expr);
            vstmts.push_back(std::move(var_stmt));
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after variable declaration");
        if (vstmts.size() == 1) return std::move(vstmts[0]);
        return std::make_unique<BlockStatementNode>(std::move(vstmts));
    }

    if (current.type == TokenType::TOKEN_STRUCT || current.type == TokenType::TOKEN_UNION) {
        auto struct_node = parse_struct_or_union_body();
        std::string anon_type = "__anon_struct_or_union_" + std::to_string(anon_struct_counter++);
        struct_typedef_map[anon_type] = struct_node;
        if (current_parsing_module) {
            current_parsing_module->struct_typedefs[anon_type] = struct_node;
            current_parsing_module->typedefs.push_back(std::make_unique<TypedefNode>(anon_type, struct_node));
            current_parsing_module->type_dimensions[anon_type] = VariableDimMetadata{anon_type, {ArrayDimInfo{(int64_t)struct_node->total_width - 1, 0, false}}, 0, true};
        }
        if (struct_node->is_packed && struct_node->total_width > 0) {
            VariableDimMetadata svdm;
            svdm.name = anon_type;
            svdm.dimensions.push_back(ArrayDimInfo{(int64_t)struct_node->total_width - 1, 0, false});
            svdm.unpacked_dims = 0;
            svdm.is_vector_or_array = true;
            global_type_dimensions[anon_type] = svdm;
        }

        std::vector<AstDimension> packed_dims;
        while (match(TokenType::TOKEN_LBRACKET)) {
            auto l = parse_expression();
            std::unique_ptr<ExpressionNode> r = nullptr;
            if (match(TokenType::TOKEN_COLON)) {
                r = parse_expression();
            }
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in packed range");
            AstDimension ad;
            ad.left_expr = l->clone();
            ad.right_expr = r ? r->clone() : nullptr;
            ad.is_single = (r == nullptr);
            packed_dims.push_back(std::move(ad));
        }

        std::vector<std::unique_ptr<StatementNode>> vstmts;
        while (true) {
            std::string vname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected variable identifier");
            auto var_stmt = std::make_unique<VarDeclStatementNode>(vname, struct_node->total_width, struct_node->is_signed, anon_type);
            var_stmt->packed_dimensions = packed_dims;
            while (match(TokenType::TOKEN_LBRACKET)) {
                if (match(TokenType::TOKEN_DOLLAR)) {
                    var_stmt->is_queue = true;
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                } else if (match(TokenType::TOKEN_RBRACKET)) {
                    var_stmt->is_dynamic_array = true;
                } else {
                    var_stmt->is_unpacked_array = true;
                    auto l = parse_expression();
                    std::unique_ptr<ExpressionNode> r = nullptr;
                    bool is_single = true;
                    if (match(TokenType::TOKEN_COLON)) {
                        r = parse_expression();
                        is_single = false;
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                    AstDimension ad;
                    ad.left_expr = std::move(l);
                    ad.right_expr = std::move(r);
                    ad.is_single = is_single;
                    var_stmt->unpacked_dimensions.push_back(std::move(ad));
                }
            }
            if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                var_stmt->init_expr = parse_expression();
            }
            vstmts.push_back(std::move(var_stmt));
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after variable declaration");
        if (vstmts.size() == 1) return std::move(vstmts[0]);
        return std::make_unique<BlockStatementNode>(std::move(vstmts));
    }

    // 1. Variable Declarations in Procedural Scopes (integer i; logic [7:0] temp; state_t s;)
    bool is_procedural_param = false;
    if (current.type == TokenType::TOKEN_PARAMETER || current.type == TokenType::TOKEN_LOCALPARAM) {
        is_procedural_param = true;
        advance();
        if (current.type == TokenType::TOKEN_TYPE || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "type")) {
            advance();
            while (true) {
                std::string pname = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter identifier");
                if (match(TokenType::TOKEN_EQUAL)) {
                    std::string def_type = current.text;
                    advance();
                    while (current.type == TokenType::TOKEN_DOT || current.type == TokenType::TOKEN_COLON_COLON || current.type == TokenType::TOKEN_HASH || current.type == TokenType::TOKEN_LBRACKET) {
                        if (match(TokenType::TOKEN_LBRACKET)) {
                            def_type += "[";
                            while (current.type != TokenType::TOKEN_RBRACKET && current.type != TokenType::TOKEN_EOF) {
                                def_type += current.text;
                                advance();
                            }
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in type parameter default");
                            def_type += "]";
                        } else {
                            def_type += current.text;
                            advance();
                        }
                    }
                    simple_typedef_map[pname] = SimpleTypedef(pname, def_type, 32, false);
                    global_simple_typedefs[pname] = 32;
                    if (current_parsing_module) {
                        current_parsing_module->simple_typedefs[pname] = SimpleTypedef(pname, def_type, 32, false);
                    }
                }
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after parameter declaration");
            return std::make_unique<BlockStatementNode>(std::vector<std::unique_ptr<StatementNode>>());
        }
    }

    bool is_procedural_static = is_stmt_static;
    bool is_procedural_automatic = is_stmt_automatic;
    if (match(TokenType::TOKEN_STATIC) || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "static")) {
        is_procedural_static = true;
        if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
    } else if (match(TokenType::TOKEN_AUTOMATIC) || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "automatic")) {
        is_procedural_automatic = true;
        if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
    }

    std::string proc_type_name = "";
    if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON_COLON) {
        std::string p_name = current.text;
        advance(); // consume package identifier
        advance(); // consume '::'
        std::string t_name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected type identifier after '::'");
        proc_type_name = p_name + "::" + t_name;
    } else if (current.type == TokenType::TOKEN_IDENTIFIER && (is_typedef(current.text) || is_simple_typedef(current.text) || is_unpacked_array_typedef(current.text) || find_type_dim_metadata(current.text) != nullptr || is_module_type_parameter(current.text))) {
        proc_type_name = current.text;
        advance();
    }

    if (!proc_type_name.empty()) {
        std::string type_name = resolve_imported_type_name(proc_type_name);
        auto enum_info = get_enum_typedef(type_name);
        auto struct_info = get_struct_typedef(type_name);
        SimpleTypedef simple_info;
        bool has_simple = get_simple_typedef(type_name, simple_info);
        const UnpackedArrayTypedef* uarr_info = get_unpacked_array_typedef(type_name);
        const VariableDimMetadata* dim_meta = find_type_dim_metadata(type_name);
        uint32_t width = uarr_info ? uarr_info->total_bit_width() : (struct_info ? struct_info->total_width : (enum_info ? enum_info->base_width : (has_simple ? simple_info.width : 32)));
        if (dim_meta) {
            uint32_t pw = 1;
            for (const auto& d : dim_meta->dimensions) {
                if (!d.is_unpacked) pw *= (uint32_t)d.size();
            }
            if (pw > 1) width = pw;
        }
        bool is_signed_var = uarr_info ? uarr_info->elem_signed : (struct_info ? struct_info->is_signed : (enum_info ? enum_info->is_signed : (has_simple ? simple_info.is_signed : false)));

        std::vector<AstDimension> packed_dims;
        while (match(TokenType::TOKEN_LBRACKET)) {
            auto msb = parse_expression();
            expect(TokenType::TOKEN_COLON, "Expected ':' in packed range");
            auto lsb = parse_expression();
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in packed range");
            AstDimension ad;
            ad.left_expr = msb->clone();
            ad.right_expr = lsb->clone();
            ad.is_single = false;
            packed_dims.push_back(std::move(ad));
            if (auto nm = dynamic_cast<NumberNode*>(msb.get())) {
                if (auto nl = dynamic_cast<NumberNode*>(lsb.get())) {
                    uint32_t dim_w = (nm->aval >= nl->aval) ? (uint32_t)(nm->aval - nl->aval + 1) : (uint32_t)(nl->aval - nm->aval + 1);
                    width *= dim_w;
                }
            }
        }

        std::vector<std::unique_ptr<StatementNode>> vstmts;
        while (true) {
            std::string vname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected variable identifier");
            bool is_q = false;
            bool is_dyn = (uarr_info && uarr_info->is_dynamic_array);
            bool is_assoc = false;
            std::string assoc_key_type = "";
            uint32_t assoc_key_width = 32;
            std::vector<AstDimension> unpacked_dims;
            while (current.type == TokenType::TOKEN_LBRACKET || current.type == TokenType::TOKEN_REP_CONSEC) {
                if (match(TokenType::TOKEN_REP_CONSEC)) {
                    is_assoc = true;
                    assoc_key_type = "*";
                    assoc_key_width = 64;
                    match(TokenType::TOKEN_RBRACKET);
                    continue;
                }
                advance(); // consume '['
                if (match(TokenType::TOKEN_DOLLAR)) {
                    is_q = true;
                    if (match(TokenType::TOKEN_COLON)) {
                        parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                } else if (match(TokenType::TOKEN_RBRACKET)) {
                    is_dyn = true;
                } else if (is_assoc_key_token(current)) {
                    is_assoc = true;
                    parse_assoc_key_type(assoc_key_type, assoc_key_width);
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in associative array dimension");
                } else {
                    auto l = parse_expression();
                    std::unique_ptr<ExpressionNode> r = nullptr;
                    bool is_single = true;
                    if (match(TokenType::TOKEN_COLON)) {
                        r = parse_expression();
                        is_single = false;
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                    AstDimension ad;
                    ad.left_expr = std::move(l);
                    ad.right_expr = std::move(r);
                    ad.is_single = is_single;
                    unpacked_dims.push_back(std::move(ad));
                }
            }
            std::unique_ptr<ExpressionNode> init_expr = nullptr;
            if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                init_expr = parse_expression();
            }
            uint32_t var_decl_w = is_dyn ? (uarr_info ? uarr_info->elem_width : width) : width;
            current_scope_var_types[vname] = type_name;
            auto vstmt = std::make_unique<VarDeclStatementNode>(vname, var_decl_w, is_signed_var, type_name, is_q, is_dyn, is_assoc, assoc_key_type, assoc_key_width);
            vstmt->is_static = is_procedural_static;
            vstmt->packed_dimensions = packed_dims;
            vstmt->unpacked_dimensions = std::move(unpacked_dims);
            if (uarr_info && !uarr_info->is_dynamic_array) {
                vstmt->is_unpacked_array = true;
                vstmt->elem_width = uarr_info->elem_width;
                vstmt->array_size = uarr_info->array_size;
                vstmt->arr_start_idx = uarr_info->start_idx;
                vstmt->arr_end_idx = uarr_info->end_idx;
            }
            vstmt->init_expr = std::move(init_expr);
            register_local_decl_var(vname, width, is_signed_var, type_name);
            vstmts.push_back(std::move(vstmt));
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after variable declaration");
        if (vstmts.size() == 1) return std::move(vstmts[0]);
        return std::make_unique<BlockStatementNode>(std::move(vstmts));
    }

    if (current.type == TokenType::TOKEN_TYPE) {
        TypeInferenceResult inf = parse_type_spec_from_type_op();
        std::vector<std::unique_ptr<StatementNode>> vstmts;
        while (true) {
            std::string vname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected variable identifier");
            bool is_q = false;
            bool is_dyn = false;
            bool is_assoc = false;
            std::string assoc_key_type = "";
            uint32_t assoc_key_width = 32;
            std::vector<AstDimension> unpacked_dims;
            while (match(TokenType::TOKEN_LBRACKET)) {
                if (match(TokenType::TOKEN_DOLLAR)) {
                    is_q = true;
                    if (match(TokenType::TOKEN_COLON)) parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                    continue;
                } else if (match(TokenType::TOKEN_RBRACKET)) {
                    is_dyn = true;
                    continue;
                } else if (is_assoc_key_token(current)) {
                    is_assoc = true;
                    parse_assoc_key_type(assoc_key_type, assoc_key_width);
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in associative array dimension");
                    continue;
                } else {
                    auto l = parse_expression();
                    std::unique_ptr<ExpressionNode> r = nullptr;
                    bool is_single = true;
                    if (match(TokenType::TOKEN_COLON)) {
                        r = parse_expression();
                        is_single = false;
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                    AstDimension ad;
                    ad.left_expr = std::move(l);
                    ad.right_expr = std::move(r);
                    ad.is_single = is_single;
                    unpacked_dims.push_back(std::move(ad));
                }
            }
            std::unique_ptr<ExpressionNode> init_expr = nullptr;
            if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                init_expr = parse_expression();
            }
            auto vnode = std::make_unique<VarDeclStatementNode>(vname, inf.width, inf.is_signed, inf.type_name, is_q, is_dyn, is_assoc, assoc_key_type, assoc_key_width);
            vnode->is_static = is_procedural_static;
            vnode->is_class_handle = inf.is_class_handle;
            vnode->class_type_name = inf.class_type_name;
            if (vnode->is_class_handle) vnode->width = 64;
            vnode->is_covergroup_handle = inf.is_covergroup_handle;
            vnode->covergroup_type_name = inf.covergroup_type_name;
            vnode->is_virtual_interface = inf.is_virtual_interface;
            vnode->interface_name = inf.interface_name;
            vnode->modport_name = inf.modport_name;
            if (inf.msb && inf.lsb) {
                AstDimension ad;
                ad.left_expr = inf.msb->clone();
                ad.right_expr = inf.lsb->clone();
                ad.is_single = false;
                vnode->packed_dimensions.push_back(std::move(ad));
            }
            vnode->unpacked_dimensions = std::move(unpacked_dims);
            vnode->init_expr = std::move(init_expr);

            register_local_decl_var(vname, inf.width, inf.is_signed, inf.type_name, inf.msb ? inf.msb->clone() : nullptr, inf.lsb ? inf.lsb->clone() : nullptr);

            vstmts.push_back(std::move(vnode));
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after variable declaration");
        if (vstmts.size() == 1) return std::move(vstmts[0]);
        return std::make_unique<BlockStatementNode>(std::move(vstmts));
    }

    bool has_var_kw = false;
    if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "var") {
        has_var_kw = true;
        advance();
    }

    bool is_shortreal_var = (current.type == TokenType::TOKEN_SHORTREAL ||
                            (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "shortreal"));
    bool is_real_var = (current.type == TokenType::TOKEN_REAL || is_shortreal_var ||
                        (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "real" || current.text == "realtime")));
    bool is_ident_int_type = (current.type == TokenType::TOKEN_IDENTIFIER &&
                              (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint" || current.text == "time"));
    bool is_ident_string_type = (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "string");
    bool is_ident_unsigned_type = (current.type == TokenType::TOKEN_UNSIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned"));
    bool is_chandle_stmt = (current.type == TokenType::TOKEN_CHANDLE ||
                            (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "chandle"));
    bool is_semaphore_stmt = (current.type == TokenType::TOKEN_SEMAPHORE ||
                             (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "semaphore"));
    bool is_mailbox_stmt = (current.type == TokenType::TOKEN_MAILBOX ||
                           (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "mailbox"));

    if (current.type == TokenType::TOKEN_EVENT || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "event")) {
        advance();
        std::vector<std::unique_ptr<StatementNode>> vstmts;
        while (true) {
            std::string ev_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected event identifier");
            while (match(TokenType::TOKEN_LBRACKET)) {
                while (current.type != TokenType::TOKEN_RBRACKET && current.type != TokenType::TOKEN_EOF) advance();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
            }
            current_scope_var_types[ev_name] = "event";
            if (current_parsing_module) {
                current_parsing_module->events.push_back(ev_name);
                PortNode pnode(ev_name, "internal", nullptr, nullptr, false, NetKind::VAR);
                pnode.type_name = "event";
                current_parsing_module->signals.push_back(std::move(pnode));
            }
            vstmts.push_back(std::make_unique<VarDeclStatementNode>(ev_name, 0, false, "event"));
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after event declaration");
        if (vstmts.size() == 1) return std::move(vstmts[0]);
        return std::make_unique<BlockStatementNode>(std::move(vstmts));
    }

    if (has_var_kw || is_procedural_param || is_procedural_static || is_procedural_automatic ||
        current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_LOGIC ||
        current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_WIRE ||
        current.type == TokenType::TOKEN_BIT || is_real_var ||
        current.type == TokenType::TOKEN_UNSIGNED ||
        is_ident_int_type || is_ident_string_type || is_ident_unsigned_type || is_chandle_stmt || is_semaphore_stmt || is_mailbox_stmt) {

        bool explicit_signed = false;
        bool explicit_unsigned = false;
        if (current.type == TokenType::TOKEN_SIGNED) {
            explicit_signed = true;
            advance();
        } else if (current.type == TokenType::TOKEN_UNSIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned")) {
            explicit_unsigned = true;
            advance();
        }

        bool has_type_kw = (current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_LOGIC ||
                            current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_WIRE ||
                            current.type == TokenType::TOKEN_BIT || is_real_var ||
                            is_ident_int_type || is_ident_string_type || is_chandle_stmt || is_semaphore_stmt || is_mailbox_stmt);
        bool is_int = (current.type == TokenType::TOKEN_INTEGER);
        std::string int_type_name = "";
        if (current.type == TokenType::TOKEN_IDENTIFIER &&
            (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint" || current.text == "time")) {
            int_type_name = current.text;
            is_int = true;
        }
        if (has_type_kw) {
            advance();
        }
        if (is_mailbox_stmt && match(TokenType::TOKEN_HASH)) {
            expect(TokenType::TOKEN_LPAREN, "Expected '(' in mailbox parameter");
            while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) advance();
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing mailbox parameter");
        }
        if (current.type == TokenType::TOKEN_SIGNED) {
            explicit_signed = true;
            advance();
        } else if (current.type == TokenType::TOKEN_UNSIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned")) {
            explicit_unsigned = true;
            advance();
        }

        bool is_signed_var = false;
        if (explicit_signed) is_signed_var = true;
        else if (explicit_unsigned) is_signed_var = false;
        else if (is_int) is_signed_var = (int_type_name != "time");

        uint32_t width = (is_chandle_stmt || is_semaphore_stmt || is_mailbox_stmt) ? 64 :
                         (is_ident_string_type ? 64 :
                         (is_int ? 32 :
                         (is_shortreal_var ? 32 :
                         (is_real_var ? 64 :
                         (is_procedural_param ? 32 : 1)))));
        if (int_type_name == "byte") width = 8;
        else if (int_type_name == "shortint") width = 16;
        else if (int_type_name == "longint" || int_type_name == "time") width = 64;

        std::vector<AstDimension> packed_dims;
        while (match(TokenType::TOKEN_LBRACKET)) {
            auto msb_e = parse_expression();
            expect(TokenType::TOKEN_COLON, "Expected ':' in range");
            auto lsb_e = parse_expression();
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in range");
            if (auto nm = dynamic_cast<NumberNode*>(msb_e.get())) {
                if (auto nl = dynamic_cast<NumberNode*>(lsb_e.get())) {
                    width = (nm->aval >= nl->aval) ? (uint32_t)(nm->aval - nl->aval + 1) : (uint32_t)(nl->aval - nm->aval + 1);
                }
            }
            AstDimension ad;
            ad.left_expr = msb_e->clone();
            ad.right_expr = lsb_e->clone();
            ad.is_single = false;
            packed_dims.push_back(std::move(ad));
        }

        std::vector<std::unique_ptr<StatementNode>> vstmts;
        std::string tname = is_chandle_stmt ? "chandle" : (is_semaphore_stmt ? "semaphore" : (is_mailbox_stmt ? "mailbox" : (is_shortreal_var ? "shortreal" : (is_real_var ? "real" : (is_ident_string_type ? "string" : (!int_type_name.empty() ? int_type_name : ""))))));
        while (true) {
            std::string vname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected variable identifier");
            bool is_q = false;
            bool is_dyn = false;
            bool is_assoc = false;
            std::string assoc_key_type = "";
            uint32_t assoc_key_width = 32;
            std::vector<AstDimension> unpacked_dims;
            while (current.type == TokenType::TOKEN_LBRACKET || current.type == TokenType::TOKEN_REP_CONSEC) {
                if (match(TokenType::TOKEN_REP_CONSEC)) {
                    is_assoc = true;
                    assoc_key_type = "*";
                    assoc_key_width = 64;
                    match(TokenType::TOKEN_RBRACKET);
                    continue;
                }
                advance(); // consume '['
                if (match(TokenType::TOKEN_DOLLAR)) {
                    is_q = true;
                    if (match(TokenType::TOKEN_COLON)) {
                        parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                    continue;
                } else if (match(TokenType::TOKEN_RBRACKET)) {
                    is_dyn = true;
                    continue;
                } else if (is_assoc_key_token(current)) {
                    is_assoc = true;
                    parse_assoc_key_type(assoc_key_type, assoc_key_width);
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in associative array dimension");
                    continue;
                } else {
                    auto l = parse_expression();
                    std::unique_ptr<ExpressionNode> r = nullptr;
                    bool is_single = true;
                    if (match(TokenType::TOKEN_COLON)) {
                        r = parse_expression();
                        is_single = false;
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                    AstDimension ad;
                    ad.left_expr = std::move(l);
                    ad.right_expr = std::move(r);
                    ad.is_single = is_single;
                    unpacked_dims.push_back(std::move(ad));
                }
            }
            std::unique_ptr<ExpressionNode> init_expr = nullptr;
            if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                init_expr = parse_expression();
            }
            current_scope_var_types[vname] = tname;
            auto vnode = std::make_unique<VarDeclStatementNode>(vname, width, is_signed_var, tname, is_q, is_dyn, is_assoc, assoc_key_type, assoc_key_width);
            vnode->is_static = is_procedural_static;
            std::vector<AstDimension> cloned_packed;
            for (const auto& pd : packed_dims) {
                AstDimension cad;
                cad.left_expr = pd.left_expr ? pd.left_expr->clone() : nullptr;
                cad.right_expr = pd.right_expr ? pd.right_expr->clone() : nullptr;
                cad.is_single = pd.is_single;
                cloned_packed.push_back(std::move(cad));
            }
            vnode->packed_dimensions = std::move(cloned_packed);
            if (!unpacked_dims.empty() && !is_dyn && !is_q && !is_assoc) {
                vnode->is_unpacked_array = true;
                vnode->elem_width = width;
                uint32_t tot_sz = 1;
                for (const auto& ud : unpacked_dims) {
                    if (auto nm = dynamic_cast<NumberNode*>(ud.left_expr.get())) {
                        if (ud.is_single) {
                            tot_sz *= (nm->aval > 0 ? (uint32_t)nm->aval : 1);
                        } else if (ud.right_expr) {
                            if (auto nr = dynamic_cast<NumberNode*>(ud.right_expr.get())) {
                                uint32_t dsz = (nm->aval >= nr->aval) ? (uint32_t)(nm->aval - nr->aval + 1) : (uint32_t)(nr->aval - nm->aval + 1);
                                tot_sz *= dsz;
                            }
                        }
                    }
                }
                vnode->array_size = tot_sz;
            }
            vnode->unpacked_dimensions = std::move(unpacked_dims);
            vnode->is_chandle = is_chandle_stmt;
            vnode->is_semaphore = is_semaphore_stmt;
            vnode->is_mailbox = is_mailbox_stmt;
            vnode->init_expr = std::move(init_expr);
            std::unique_ptr<ExpressionNode> d_msb = packed_dims.empty() ? nullptr : packed_dims[0].left_expr->clone();
            std::unique_ptr<ExpressionNode> d_lsb = packed_dims.empty() ? nullptr : packed_dims[0].right_expr->clone();
            register_local_decl_var(vname, width, is_signed_var, tname, std::move(d_msb), std::move(d_lsb));
            vstmts.push_back(std::move(vnode));
            if (match(TokenType::TOKEN_COMMA)) {
                if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "var") {
                    advance();
                }
                if (current.type == TokenType::TOKEN_BIT || current.type == TokenType::TOKEN_LOGIC ||
                    current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_REG ||
                    (current.type == TokenType::TOKEN_IDENTIFIER &&
                     (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint"))) {
                    tname = current.text;
                    if (current.type == TokenType::TOKEN_BIT) { width = 1; is_signed_var = false; }
                    else if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG) { width = 1; is_signed_var = false; }
                    else if (current.type == TokenType::TOKEN_INTEGER || current.text == "int") { width = 32; is_signed_var = true; }
                    else if (current.text == "byte") { width = 8; is_signed_var = true; }
                    else if (current.text == "shortint") { width = 16; is_signed_var = true; }
                    else if (current.text == "longint") { width = 64; is_signed_var = true; }
                    advance();
                    packed_dims.clear();
                    while (match(TokenType::TOKEN_LBRACKET)) {
                        auto msb = parse_expression();
                        expect(TokenType::TOKEN_COLON, "Expected ':' in range");
                        auto lsb = parse_expression();
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in range");
                        AstDimension dim;
                        dim.left_expr = std::move(msb);
                        dim.right_expr = std::move(lsb);
                        dim.is_single = false;
                        packed_dims.push_back(std::move(dim));
                    }
                }
                continue;
            }
            break;
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after variable declaration");
        if (vstmts.size() == 1) return std::move(vstmts[0]);
        return std::make_unique<BlockStatementNode>(std::move(vstmts));
    }

    // Cycle Delay Statement (##N; or ##(expr);)
    if (match(TokenType::TOKEN_CYCLE_DELAY)) {
        std::unique_ptr<ExpressionNode> cycle_expr = nullptr;
        if (current.type == TokenType::TOKEN_NUMBER) {
            uint64_t n = current.num_val;
            advance();
            cycle_expr = std::make_unique<NumberNode>(n, 32);
        } else if (match(TokenType::TOKEN_LPAREN)) {
            cycle_expr = parse_expression();
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing cycle delay expression");
        } else if (current.type == TokenType::TOKEN_IDENTIFIER) {
            std::string id_name = current.text;
            advance();
            cycle_expr = std::make_unique<IdentifierNode>(id_name);
        } else {
            cycle_expr = parse_expression();
        }

        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after cycle delay statement");
        return std::make_unique<CycleDelayStatementNode>(std::move(cycle_expr));
    }

    // 2. Delay Statement (#5; or #delay_ticks; or #5 clk = ~clk;)
    if (match(TokenType::TOKEN_HASH)) {
        std::unique_ptr<ExpressionNode> delay_expr = nullptr;
        uint64_t d = 0;
        bool is_time_lit = false;
        double lit_time_fs = 0.0;
        double raw_d = -1.0;
        if (current.type == TokenType::TOKEN_NUMBER) {
            if (current.is_time_literal && (current.time_fs > 0 || current.time_fs_real > 0.0)) {
                is_time_lit = true;
                lit_time_fs = (current.time_fs_real > 0.0) ? current.time_fs_real : (double)current.time_fs;
                d = (uint64_t)std::round(lit_time_fs / 1000000.0);
                if (d == 0 && lit_time_fs > 0.0) d = 1;
            } else if (current.is_real) {
                raw_d = current.real_val;
                d = static_cast<uint64_t>(std::round(current.real_val * Lexer::get_timescale_ratio()));
            } else {
                raw_d = (double)current.num_val;
                d = current.num_val * Lexer::get_timescale_ratio();
            }
            advance();
        } else if (match(TokenType::TOKEN_LPAREN)) {
            delay_expr = parse_expression();
            if (match(TokenType::TOKEN_COLON)) {
                auto typ_e = parse_expression();
                delay_expr = std::move(typ_e);
                if (match(TokenType::TOKEN_COLON)) {
                    parse_expression();
                }
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing delay expression");
        } else if (current.type == TokenType::TOKEN_IDENTIFIER) {
            std::string id_name = current.text;
            advance();
            delay_expr = std::make_unique<IdentifierNode>(id_name);
        } else {
            delay_expr = parse_expression();
        }

        auto make_delay_node = [&]() {
            auto dnode = std::make_unique<DelayStatementNode>(d, std::move(delay_expr));
            dnode->is_time_literal = is_time_lit;
            dnode->time_fs = lit_time_fs;
            dnode->raw_delay = raw_d;
            return dnode;
        };

        if (match(TokenType::TOKEN_SEMICOLON)) {
            return make_delay_node();
        }

        auto target_stmt = parse_statement();
        std::vector<std::unique_ptr<StatementNode>> stmts;
        stmts.push_back(make_delay_node());
        stmts.push_back(std::move(target_stmt));
        return std::make_unique<BlockStatementNode>(std::move(stmts));
    }

    // 3. Procedural Event Control: @(posedge clk), @(negedge clk), @(signal), @event, @(a or b)
    if (match(TokenType::TOKEN_AT)) {
        std::vector<std::pair<EdgeType, std::string>> events;
        std::vector<std::unique_ptr<ExpressionNode>> first_indices;
        if (match(TokenType::TOKEN_LPAREN)) {
            while (true) {
                EdgeType edge = EdgeType::LEVEL;
                bool is_edge_kw = false;
                if (match(TokenType::TOKEN_POSEDGE)) edge = EdgeType::POSEDGE;
                else if (match(TokenType::TOKEN_NEGEDGE)) edge = EdgeType::NEGEDGE;
                else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "edge") {
                    advance();
                    is_edge_kw = true;
                }

                std::string sig = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier in event control");
                std::vector<std::unique_ptr<ExpressionNode>> cur_indices;
                while (true) {
                    if (match(TokenType::TOKEN_DOT)) {
                        sig += "." + current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in event control");
                    } else if (match(TokenType::TOKEN_COLON_COLON)) {
                        sig += "::" + current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '::' in event control");
                    } else if (match(TokenType::TOKEN_LBRACKET)) {
                        cur_indices.push_back(parse_expression());
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in event control index");
                    } else {
                        break;
                    }
                }
                if (first_indices.empty() && !cur_indices.empty()) {
                    first_indices = std::move(cur_indices);
                }
                if (match(TokenType::TOKEN_IFF)) {
                    parse_expression();
                } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "iff") {
                    advance();
                    parse_expression();
                }
                if (is_edge_kw) {
                    events.push_back({EdgeType::POSEDGE, sig});
                    events.push_back({EdgeType::NEGEDGE, sig});
                } else {
                    events.push_back({edge, sig});
                }
                if (match(TokenType::TOKEN_OR) || match(TokenType::TOKEN_GATE_OR) || match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing event control");
        } else if (current.type == TokenType::TOKEN_IDENTIFIER) {
            std::string sig = current.text;
            advance();
            while (true) {
                if (match(TokenType::TOKEN_DOT)) {
                    sig += "." + current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in event control");
                } else if (match(TokenType::TOKEN_COLON_COLON)) {
                    sig += "::" + current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '::' in event control");
                } else if (match(TokenType::TOKEN_LBRACKET)) {
                    first_indices.push_back(parse_expression());
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in event control index");
                } else {
                    break;
                }
            }
            events.push_back({EdgeType::LEVEL, sig});
        } else {
            throw std::runtime_error("Expected identifier or '(' after '@'");
        }

        EdgeType primary_edge = events.empty() ? EdgeType::LEVEL : events[0].first;
        std::string primary_sig = events.empty() ? "" : events[0].second;
        auto ev_node = std::make_unique<EventControlStatementNode>(primary_edge, primary_sig);
        ev_node->index_exprs = std::move(first_indices);
        ev_node->events = std::move(events);

        if (match(TokenType::TOKEN_SEMICOLON)) {
            return ev_node;
        }

        auto target_stmt = parse_statement();
        std::vector<std::unique_ptr<StatementNode>> stmts;
        stmts.push_back(std::move(ev_node));
        stmts.push_back(std::move(target_stmt));
        return std::make_unique<BlockStatementNode>(std::move(stmts));
    }

    // 4. Forever Loop
    if (match(TokenType::TOKEN_FOREVER)) {
        auto body = parse_statement_or_block();
        return std::make_unique<ForeverStatementNode>(std::move(body));
    }

    // Fork-Join Parallel Block (IEEE 1800-2023 §9.3.2)
    if (match(TokenType::TOKEN_FORK)) {
        std::string fork_label = "";
        if (match(TokenType::TOKEN_COLON)) {
            fork_label = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected fork block label");
        }

        in_fork_depth++;
        std::vector<std::unique_ptr<StatementNode>> branches;
        while (current.type != TokenType::TOKEN_JOIN &&
               current.type != TokenType::TOKEN_JOIN_ANY &&
               current.type != TokenType::TOKEN_JOIN_NONE &&
               current.type != TokenType::TOKEN_EOF) {
            branches.push_back(parse_statement());
        }
        in_fork_depth--;

        JoinType jt = JoinType::JOIN;
        if (match(TokenType::TOKEN_JOIN)) {
            jt = JoinType::JOIN;
        } else if (match(TokenType::TOKEN_JOIN_ANY)) {
            jt = JoinType::JOIN_ANY;
        } else if (match(TokenType::TOKEN_JOIN_NONE)) {
            jt = JoinType::JOIN_NONE;
        } else {
            throw std::runtime_error("Expected 'join', 'join_any', or 'join_none'");
        }

        if (current_parsing_function != nullptr && jt != JoinType::JOIN_NONE) {
            throw std::runtime_error("Functions cannot contain fork-join or fork-join_any blocks per IEEE 1800-2023 13.4.4");
        }

        if (match(TokenType::TOKEN_COLON)) {
            std::string join_label = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected join block label");
            if (!fork_label.empty() && join_label != fork_label) {
                throw std::runtime_error("Fork end label '" + join_label + "' does not match fork label '" + fork_label + "'");
            }
        }
        match(TokenType::TOKEN_SEMICOLON);

        return std::make_unique<ForkStatementNode>(jt, std::move(branches), fork_label);
    }

    // disable statement (IEEE 1800-2023 §9.6.2)
    if (match(TokenType::TOKEN_DISABLE)) {
        if (match(TokenType::TOKEN_FORK)) {
            match(TokenType::TOKEN_SEMICOLON);
            return std::make_unique<DisableForkStatementNode>();
        }
        std::string target = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected block or task identifier after 'disable'");
        while (match(TokenType::TOKEN_DOT)) {
            target += "." + current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in disable target");
        }
        match(TokenType::TOKEN_SEMICOLON);
        return std::make_unique<DisableStatementNode>(target);
    }

    // wait fork (IEEE 1800-2023 §9.6.3), wait (e.triggered) (IEEE 1800-2023 §15.5), or wait (expr) [stmt] (IEEE 1800-2023 §9.4.3)
    if (match(TokenType::TOKEN_WAIT)) {
        if (match(TokenType::TOKEN_FORK)) {
            match(TokenType::TOKEN_SEMICOLON);
            return std::make_unique<WaitForkStatementNode>();
        }
        if (match(TokenType::TOKEN_LPAREN)) {
            auto cond = parse_expression();
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing wait(...)");
            if (auto ma = dynamic_cast<MemberAccessNode*>(cond.get())) {
                if (ma->member_name == "triggered") {
                    match(TokenType::TOKEN_SEMICOLON);
                    return std::make_unique<WaitEventTriggeredStatementNode>(ma->var_name);
                }
            }
            std::unique_ptr<StatementNode> body = nullptr;
            if (!match(TokenType::TOKEN_SEMICOLON)) {
                body = parse_statement();
            }
            return std::make_unique<WaitConditionStatementNode>(std::move(cond), std::move(body));
        }
        throw std::runtime_error("Expected 'fork' or '(' after 'wait'");
    }

    // Event Trigger: -> e; or ->> e; (IEEE 1800-2023 §15.4)
    if (match(TokenType::TOKEN_TRIGGER)) {
        std::string ev_name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected event identifier after '->'");
        std::vector<std::unique_ptr<ExpressionNode>> indices;
        while (true) {
            if (match(TokenType::TOKEN_DOT)) {
                ev_name += "." + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in event trigger");
            } else if (match(TokenType::TOKEN_COLON_COLON)) {
                ev_name += "::" + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '::' in event trigger");
            } else if (match(TokenType::TOKEN_LBRACKET)) {
                indices.push_back(parse_expression());
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in event trigger index");
            } else {
                break;
            }
        }
        match(TokenType::TOKEN_SEMICOLON);
        auto node = std::make_unique<EventTriggerStatementNode>(ev_name, /*is_nonblocking=*/false);
        node->index_exprs = std::move(indices);
        return node;
    }
    if (match(TokenType::TOKEN_NONBLOCKING_TRIGGER)) {
        std::string ev_name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected event identifier after '->>'");
        std::vector<std::unique_ptr<ExpressionNode>> indices;
        while (true) {
            if (match(TokenType::TOKEN_DOT)) {
                ev_name += "." + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in event trigger");
            } else if (match(TokenType::TOKEN_COLON_COLON)) {
                ev_name += "::" + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '::' in event trigger");
            } else if (match(TokenType::TOKEN_LBRACKET)) {
                indices.push_back(parse_expression());
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in event trigger index");
            } else {
                break;
            }
        }
        match(TokenType::TOKEN_SEMICOLON);
        auto node = std::make_unique<EventTriggerStatementNode>(ev_name, /*is_nonblocking=*/true);
        node->index_exprs = std::move(indices);
        return node;
    }

    // Foreach Loop (§12.7.3)
    if (match(TokenType::TOKEN_FOREACH)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'foreach'");
        std::string arr_name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected array identifier in foreach");
        while (current.type == TokenType::TOKEN_DOT || current.type == TokenType::TOKEN_LBRACKET) {
            if (match(TokenType::TOKEN_DOT)) {
                arr_name += "." + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in foreach");
            } else if (current.type == TokenType::TOKEN_LBRACKET) {
                advance(); // consume '['
                std::vector<std::string> loop_vars;
                std::string idx_content = "";
                while (current.type != TokenType::TOKEN_RBRACKET && current.type != TokenType::TOKEN_EOF) {
                    if (current.type == TokenType::TOKEN_IDENTIFIER) {
                        loop_vars.push_back(current.text);
                    } else if (current.type != TokenType::TOKEN_COMMA) {
                        loop_vars.push_back("");
                    }
                    if (!idx_content.empty()) idx_content += " ";
                    idx_content += current.text;
                    advance();
                    if (match(TokenType::TOKEN_COMMA)) {
                        idx_content += ",";
                    }
                }
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in foreach");
                if (current.type == TokenType::TOKEN_DOT || current.type == TokenType::TOKEN_LBRACKET) {
                    arr_name += "[" + idx_content + "]";
                } else {
                    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing foreach header");
                    auto body = parse_statement_or_block();
                    return std::make_unique<ForeachStatementNode>(arr_name, std::move(loop_vars), std::move(body));
                }
            }
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing foreach header");
        auto body = parse_statement_or_block();
        return std::make_unique<ForeachStatementNode>(arr_name, std::vector<std::string>{}, std::move(body));
    }

    // 5. For Loop
    if (match(TokenType::TOKEN_FOR)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'for'");

        auto parse_for_single_assign = [&]() -> std::unique_ptr<StatementNode> {
            std::unique_ptr<StatementNode> cur_assign = nullptr;
            if (current.type == TokenType::TOKEN_PLUS_PLUS || current.type == TokenType::TOKEN_MINUS_MINUS) {
                std::string inc_op = current.text;
                advance();
                std::string target = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected loop variable in for step/init");
                auto lhs_id = std::make_unique<IdentifierNode>(target);
                auto one = std::make_unique<NumberNode>(1ULL, 32, false, true);
                auto op_expr = (inc_op == "++")
                    ? std::make_unique<BinaryOpNode>("+", std::move(lhs_id), std::move(one))
                    : std::make_unique<BinaryOpNode>("-", std::move(lhs_id), std::move(one));
                cur_assign = std::make_unique<BlockingAssignNode>(target, std::move(op_expr));
            } else if (match(TokenType::TOKEN_LBRACE)) {
                std::vector<std::string> targets;
                std::vector<std::unique_ptr<ExpressionNode>> target_exprs;
                while (true) {
                    std::string t = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier in concatenation target list");
                    while (match(TokenType::TOKEN_DOT)) {
                        t += "." + current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in concatenation target");
                    }
                    std::unique_ptr<ExpressionNode> target_expr = nullptr;
                    if (match(TokenType::TOKEN_LBRACKET)) {
                        auto idx_expr = parse_expression();
                        if (match(TokenType::TOKEN_PLUS_COLON)) {
                            auto w_expr = parse_expression();
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                            auto inode = std::make_unique<IndexSelectNode>(t, std::move(idx_expr), std::move(w_expr));
                            inode->is_plus_colon = true;
                            target_expr = std::move(inode);
                        } else if (match(TokenType::TOKEN_MINUS_COLON)) {
                            auto w_expr = parse_expression();
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                            auto inode = std::make_unique<IndexSelectNode>(t, std::move(idx_expr), std::move(w_expr));
                            inode->is_minus_colon = true;
                            target_expr = std::move(inode);
                        } else if (match(TokenType::TOKEN_COLON)) {
                            auto lsb_expr = parse_expression();
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                            auto inode = std::make_unique<IndexSelectNode>(t, std::move(idx_expr), std::move(lsb_expr));
                            inode->is_part_select = true;
                            target_expr = std::move(inode);
                        } else {
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                            target_expr = std::make_unique<IndexSelectNode>(t, std::move(idx_expr));
                        }
                    } else {
                        target_expr = std::make_unique<IdentifierNode>(t);
                    }
                    targets.push_back(t);
                    target_exprs.push_back(std::move(target_expr));
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_RBRACE, "Expected '}' closing concatenation target list");
                std::string op_str = "";
                if (match(TokenType::TOKEN_PLUS_EQUAL)) op_str = "+";
                else if (match(TokenType::TOKEN_MINUS_EQUAL)) op_str = "-";
                else if (match(TokenType::TOKEN_STAR_EQUAL)) op_str = "*";
                else if (match(TokenType::TOKEN_SLASH_EQUAL)) op_str = "/";
                else if (match(TokenType::TOKEN_PERCENT_EQUAL)) op_str = "%";
                else if (match(TokenType::TOKEN_AND_EQUAL)) op_str = "&";
                else if (match(TokenType::TOKEN_OR_EQUAL)) op_str = "|";
                else if (match(TokenType::TOKEN_XOR_EQUAL)) op_str = "^";
                else if (match(TokenType::TOKEN_SHL_EQUAL)) op_str = "<<";
                else if (match(TokenType::TOKEN_SHR_EQUAL)) op_str = ">>";
                else if (match(TokenType::TOKEN_ASHR_EQUAL)) op_str = ">>>";
                else if (match(TokenType::TOKEN_ASHL_EQUAL)) op_str = "<<<";
                else {
                    expect(TokenType::TOKEN_EQUAL, "Expected '=' in concatenation blocking assignment");
                }
                auto rhs = parse_expression();
                if (!op_str.empty()) {
                    std::vector<std::unique_ptr<ExpressionNode>> lhs_elems;
                    for (const auto& te : target_exprs) {
                        lhs_elems.push_back(te->clone());
                    }
                    auto lhs_cat = std::make_unique<ConcatNode>(std::move(lhs_elems));
                    auto bnode = std::make_unique<BinaryOpNode>(op_str, std::move(lhs_cat), std::move(rhs));
                    bnode->is_compound_assign = true;
                    rhs = std::move(bnode);
                }
                auto node = std::make_unique<BlockingAssignNode>(targets, std::move(rhs), 0);
                node->lhs_target_exprs = std::move(target_exprs);
                cur_assign = std::move(node);
            } else {
                std::string target = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier in for loop");
                while (match(TokenType::TOKEN_DOT)) {
                    target += "." + current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.'");
                }
                std::unique_ptr<ExpressionNode> target_idx = nullptr;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    target_idx = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in index select");
                }
                if (match(TokenType::TOKEN_PLUS_PLUS)) {
                    std::unique_ptr<ExpressionNode> lhs_id = target_idx ? std::unique_ptr<ExpressionNode>(std::make_unique<IndexSelectNode>(target, target_idx->clone())) : std::unique_ptr<ExpressionNode>(std::make_unique<IdentifierNode>(target));
                    auto one = std::make_unique<NumberNode>(1ULL, 32, false, true);
                    auto add = std::make_unique<BinaryOpNode>("+", std::move(lhs_id), std::move(one));
                    cur_assign = std::make_unique<BlockingAssignNode>(target, std::move(add), std::move(target_idx));
                } else if (match(TokenType::TOKEN_MINUS_MINUS)) {
                    std::unique_ptr<ExpressionNode> lhs_id = target_idx ? std::unique_ptr<ExpressionNode>(std::make_unique<IndexSelectNode>(target, target_idx->clone())) : std::unique_ptr<ExpressionNode>(std::make_unique<IdentifierNode>(target));
                    auto one = std::make_unique<NumberNode>(1ULL, 32, false, true);
                    auto sub = std::make_unique<BinaryOpNode>("-", std::move(lhs_id), std::move(one));
                    cur_assign = std::make_unique<BlockingAssignNode>(target, std::move(sub), std::move(target_idx));
                } else {
                    std::string comp_op = "";
                    if (match(TokenType::TOKEN_PLUS_EQUAL)) comp_op = "+";
                    else if (match(TokenType::TOKEN_MINUS_EQUAL)) comp_op = "-";
                    else if (match(TokenType::TOKEN_STAR_EQUAL)) comp_op = "*";
                    else if (match(TokenType::TOKEN_SLASH_EQUAL)) comp_op = "/";
                    else if (match(TokenType::TOKEN_PERCENT_EQUAL)) comp_op = "%";
                    else if (match(TokenType::TOKEN_AND_EQUAL)) comp_op = "&";
                    else if (match(TokenType::TOKEN_OR_EQUAL)) comp_op = "|";
                    else if (match(TokenType::TOKEN_XOR_EQUAL)) comp_op = "^";
                    else if (match(TokenType::TOKEN_SHL_EQUAL)) comp_op = "<<";
                    else if (match(TokenType::TOKEN_SHR_EQUAL)) comp_op = ">>";
                    else if (match(TokenType::TOKEN_ASHL_EQUAL)) comp_op = "<<<";
                    else if (match(TokenType::TOKEN_ASHR_EQUAL)) comp_op = ">>>";
                    else {
                        expect(TokenType::TOKEN_EQUAL, "Expected '=' in for assignment");
                    }
                    auto rhs = parse_expression();
                    if (!comp_op.empty()) {
                        std::unique_ptr<ExpressionNode> lhs_id = target_idx ? std::unique_ptr<ExpressionNode>(std::make_unique<IndexSelectNode>(target, target_idx->clone())) : std::unique_ptr<ExpressionNode>(std::make_unique<IdentifierNode>(target));
                        auto op = std::make_unique<BinaryOpNode>(comp_op, std::move(lhs_id), std::move(rhs));
                        op->is_compound_assign = true;
                        rhs = std::move(op);
                    }
                    cur_assign = std::make_unique<BlockingAssignNode>(target, std::move(rhs), std::move(target_idx));
                }
            }
            return cur_assign;
        };

        std::unique_ptr<StatementNode> init_stmt = nullptr;
        if (current.type == TokenType::TOKEN_SEMICOLON) {
            advance();
        } else {
            bool is_var_decl = ((current.type == TokenType::TOKEN_IDENTIFIER && current.text == "var") ||
                               current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_LOGIC ||
                               current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_WIRE ||
                               current.type == TokenType::TOKEN_BIT || current.type == TokenType::TOKEN_SHORTREAL ||
                               current.type == TokenType::TOKEN_REAL ||
                               (current.type == TokenType::TOKEN_IDENTIFIER &&
                                (current.text == "int" || current.text == "byte" || current.text == "shortint" ||
                                 current.text == "longint" || current.text == "string" || current.text == "chandle" ||
                                 is_typedef(current.text) || is_simple_typedef(current.text))));
            if (is_var_decl) {
                init_stmt = parse_statement();
            } else {
                std::vector<std::unique_ptr<StatementNode>> init_stmts;
                while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_EOF) {
                    auto cur_init = parse_for_single_assign();
                    if (cur_init) init_stmts.push_back(std::move(cur_init));
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after for initialization");
                if (init_stmts.size() == 1) {
                    init_stmt = std::move(init_stmts[0]);
                } else if (!init_stmts.empty()) {
                    auto blk = std::make_unique<BlockStatementNode>();
                    blk->statements = std::move(init_stmts);
                    init_stmt = std::move(blk);
                }
            }
        }
        std::unique_ptr<ExpressionNode> cond_expr = nullptr;
        if (current.type != TokenType::TOKEN_SEMICOLON) {
            cond_expr = parse_expression();
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after for condition");
        
        std::vector<std::unique_ptr<StatementNode>> step_stmts;
        while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
            auto cur_step = parse_for_single_assign();
            if (cur_step) step_stmts.push_back(std::move(cur_step));
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        std::unique_ptr<StatementNode> step_stmt = nullptr;
        if (step_stmts.size() == 1) {
            step_stmt = std::move(step_stmts[0]);
        } else if (step_stmts.size() > 1) {
            auto blk = std::make_unique<BlockStatementNode>();
            blk->statements = std::move(step_stmts);
            step_stmt = std::move(blk);
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing for header");

        auto body = parse_statement_or_block();
        return std::make_unique<ForStatementNode>(std::move(init_stmt), std::move(cond_expr), std::move(step_stmt), std::move(body));
    }

    // 6. While Loop
    if (match(TokenType::TOKEN_WHILE)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'while'");
        auto cond = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing while condition");
        auto body = parse_statement_or_block();
        return std::make_unique<WhileStatementNode>(std::move(cond), std::move(body));
    }

    // Do-While Loop (IEEE 1800-2023 §12.7.5)
    if (match(TokenType::TOKEN_DO)) {
        auto body = parse_statement_or_block();
        expect(TokenType::TOKEN_WHILE, "Expected 'while' after 'do' body");
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'while'");
        auto cond = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing while condition");
        match(TokenType::TOKEN_SEMICOLON);
        return std::make_unique<DoWhileStatementNode>(std::move(cond), std::move(body));
    }

    // 7. Repeat Loop
    if (match(TokenType::TOKEN_REPEAT)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'repeat'");
        auto count = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing repeat count");
        auto body = parse_statement_or_block();

        std::function<bool(const StatementNode*)> has_timing_control = [&](const StatementNode* s) -> bool {
            if (!s) return false;
            if (dynamic_cast<const EventControlStatementNode*>(s) || dynamic_cast<const DelayStatementNode*>(s)) return true;
            if (auto blk = dynamic_cast<const BlockStatementNode*>(s)) {
                for (const auto& child : blk->statements) {
                    if (has_timing_control(child.get())) return true;
                }
            }
            return false;
        };

        std::function<std::unique_ptr<StatementNode>(const StatementNode*)> clone_timing_stmt = [&](const StatementNode* s) -> std::unique_ptr<StatementNode> {
            if (!s) return nullptr;
            if (auto ev = dynamic_cast<const EventControlStatementNode*>(s)) {
                return std::make_unique<EventControlStatementNode>(ev->edge, ev->signal_name);
            }
            if (auto del = dynamic_cast<const DelayStatementNode*>(s)) {
                return std::make_unique<DelayStatementNode>(del->delay, del->delay_expr ? del->delay_expr->clone() : nullptr);
            }
            if (auto blk = dynamic_cast<const BlockStatementNode*>(s)) {
                std::vector<std::unique_ptr<StatementNode>> cloned_list;
                for (const auto& child : blk->statements) {
                    cloned_list.push_back(clone_timing_stmt(child.get()));
                }
                return std::make_unique<BlockStatementNode>(std::move(cloned_list));
            }
            return nullptr;
        };

        bool timing = false;
        for (const auto& s : body) {
            if (has_timing_control(s.get())) {
                timing = true;
                break;
            }
        }
        if (timing) {
            if (auto num = dynamic_cast<NumberNode*>(count.get())) {
                uint64_t n = num->aval;
                std::vector<std::unique_ptr<StatementNode>> unrolled;
                for (uint64_t i = 0; i < n; ++i) {
                    for (const auto& s : body) {
                        auto c = clone_timing_stmt(s.get());
                        if (c) unrolled.push_back(std::move(c));
                    }
                }
                return std::make_unique<BlockStatementNode>(std::move(unrolled));
            }
        }

        return std::make_unique<RepeatStatementNode>(std::move(count), std::move(body));
    }

    // 8. System Tasks ($display / $monitor / $monitoron / $monitoroff)
    if (current.type == TokenType::TOKEN_MONITORON || current.type == TokenType::TOKEN_MONITOROFF) {
        bool mon_en = (current.type == TokenType::TOKEN_MONITORON);
        std::string origin_file = current.filename;
        size_t origin_line = current.line;
        advance();
        if (match(TokenType::TOKEN_LPAREN)) {
            expect(TokenType::TOKEN_RPAREN, "Expected ')' after $monitoron / $monitoroff");
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $monitoron / $monitoroff");
        return std::make_unique<DisplayStatementNode>("", std::vector<std::unique_ptr<ExpressionNode>>(), false, false, origin_file, origin_line, 'd', true, mon_en);
    }

    if (current.type == TokenType::TOKEN_DISPLAY || current.type == TokenType::TOKEN_MONITOR ||
        current.type == TokenType::TOKEN_DISPLAYB || current.type == TokenType::TOKEN_DISPLAYO || current.type == TokenType::TOKEN_DISPLAYH ||
        current.type == TokenType::TOKEN_MONITORB || current.type == TokenType::TOKEN_MONITORO || current.type == TokenType::TOKEN_MONITORH) {
        bool is_mon = (current.type == TokenType::TOKEN_MONITOR ||
                       current.type == TokenType::TOKEN_MONITORB ||
                       current.type == TokenType::TOKEN_MONITORO ||
                       current.type == TokenType::TOKEN_MONITORH);
        char default_radix = 'd';
        if (current.type == TokenType::TOKEN_DISPLAYB || current.type == TokenType::TOKEN_MONITORB) default_radix = 'b';
        else if (current.type == TokenType::TOKEN_DISPLAYO || current.type == TokenType::TOKEN_MONITORO) default_radix = 'o';
        else if (current.type == TokenType::TOKEN_DISPLAYH || current.type == TokenType::TOKEN_MONITORH) default_radix = 'h';

        std::string origin_file = current.filename;
        size_t origin_line = current.line;

        advance();
        bool has_lead_time = false;
        std::string fmt = "";
        std::vector<std::unique_ptr<ExpressionNode>> args;

        if (match(TokenType::TOKEN_LPAREN)) {
            if (!match(TokenType::TOKEN_RPAREN)) {
                auto parse_arg = [&]() -> std::unique_ptr<ExpressionNode> {
                    if (current.type == TokenType::TOKEN_COMMA || current.type == TokenType::TOKEN_RPAREN) {
                        return std::make_unique<StringNode>(" ");
                    }
                    return parse_expression();
                };

                if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "$time") {
                    has_lead_time = true;
                    advance();
                    if (match(TokenType::TOKEN_COMMA)) {
                        if (current.type == TokenType::TOKEN_STRING) {
                            fmt = current.text;
                            advance();
                        } else {
                            args.push_back(parse_arg());
                        }
                    }
                } else if (current.type == TokenType::TOKEN_STRING) {
                    fmt = current.text;
                    advance();
                } else {
                    args.push_back(parse_arg());
                }

                while (match(TokenType::TOKEN_COMMA)) {
                    args.push_back(parse_arg());
                }
                expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $display / $monitor argument list");
            }
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $display / $monitor");
        return std::make_unique<DisplayStatementNode>(fmt, std::move(args), is_mon, has_lead_time, origin_file, origin_line, default_radix);
    }

    // System Task ($sformat)
    if (current.type == TokenType::TOKEN_SYS_SFORMAT) {
        std::string origin_file = current.filename;
        size_t origin_line = current.line;
        advance();
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after $sformat");
        if (current.type == TokenType::TOKEN_RPAREN) {
            throw std::runtime_error("[CAS-DTINPCIL] $sformat requires at least 2 arguments per IEEE 1800-2023 section 20.14");
        }
        auto target_expr = parse_expression();
        if (!match(TokenType::TOKEN_COMMA) || current.type == TokenType::TOKEN_RPAREN) {
            throw std::runtime_error("[CAS-DTINPCIL] $sformat requires at least 2 arguments per IEEE 1800-2023 section 20.14");
        }
        auto fmt_expr = parse_expression();
        std::vector<std::unique_ptr<ExpressionNode>> args;
        while (match(TokenType::TOKEN_COMMA)) {
            args.push_back(parse_expression());
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $sformat argument list");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $sformat");
        return std::make_unique<SFormatStatementNode>(std::move(target_expr), std::move(fmt_expr), std::move(args), origin_file, origin_line);
    }

    // 9. $dumpfile
    if (match(TokenType::TOKEN_DUMPFILE)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after $dumpfile");
        std::string fname = current.text;
        expect(TokenType::TOKEN_STRING, "Expected string literal inside $dumpfile(\"...\")");
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $dumpfile");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $dumpfile");
        return std::make_unique<DumpFileStatementNode>(fname);
    }

    // 10. $dumpvars
    if (match(TokenType::TOKEN_DUMPVARS)) {
        uint32_t levels = 0;
        if (match(TokenType::TOKEN_LPAREN)) {
            if (current.type == TokenType::TOKEN_NUMBER) {
                levels = (uint32_t)current.num_val;
                advance();
            }
            while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                advance();
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $dumpvars");
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $dumpvars");
        return std::make_unique<DumpVarsStatementNode>(levels);
    }

    // 11. $finish / $stop
    if (match(TokenType::TOKEN_FINISH) || match(TokenType::TOKEN_STOP)) {
        std::string origin_file = current.filename;
        size_t origin_line = current.line;
        if (match(TokenType::TOKEN_LPAREN)) {
            if (current.type != TokenType::TOKEN_RPAREN) {
                parse_expression();
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $finish/$stop");
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $finish/$stop");
        return std::make_unique<FinishStatementNode>(origin_file, origin_line);
    }

    if (match(TokenType::TOKEN_EXIT)) {
        std::string origin_file = current.filename;
        size_t origin_line = current.line;
        if (match(TokenType::TOKEN_LPAREN)) {
            if (current.type != TokenType::TOKEN_RPAREN) {
                parse_expression();
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $exit");
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $exit");
        return std::make_unique<ExitStatementNode>(origin_file, origin_line);
    }

    // Severity System Tasks: $fatal, $error, $warning, $info (§20.10)
    if (current.type == TokenType::TOKEN_FATAL || current.type == TokenType::TOKEN_ERROR ||
        current.type == TokenType::TOKEN_WARNING || current.type == TokenType::TOKEN_INFO) {
        TokenType task_tok = current.type;
        std::string origin_file = current.filename;
        size_t origin_line = current.line;
        advance();

        SeverityLevel lvl = SeverityLevel::INFO;
        if (task_tok == TokenType::TOKEN_FATAL) lvl = SeverityLevel::FATAL;
        else if (task_tok == TokenType::TOKEN_ERROR) lvl = SeverityLevel::ERROR;
        else if (task_tok == TokenType::TOKEN_WARNING) lvl = SeverityLevel::WARNING;
        else if (task_tok == TokenType::TOKEN_INFO) lvl = SeverityLevel::INFO;

        std::unique_ptr<ExpressionNode> finish_num = nullptr;
        std::string fmt = "";
        std::vector<std::unique_ptr<ExpressionNode>> args;

        if (match(TokenType::TOKEN_LPAREN)) {
            if (current.type != TokenType::TOKEN_RPAREN) {
                if (lvl == SeverityLevel::FATAL) {
                    if (current.type == TokenType::TOKEN_STRING) {
                        fmt = current.text;
                        advance();
                    } else {
                        finish_num = parse_expression();
                        if (match(TokenType::TOKEN_COMMA)) {
                            if (current.type == TokenType::TOKEN_STRING) {
                                fmt = current.text;
                                advance();
                            } else {
                                args.push_back(parse_expression());
                            }
                        }
                    }
                } else {
                    if (current.type == TokenType::TOKEN_STRING) {
                        fmt = current.text;
                        advance();
                    } else {
                        args.push_back(parse_expression());
                    }
                }

                while (match(TokenType::TOKEN_COMMA)) {
                    args.push_back(parse_expression());
                }
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing severity task argument list");
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after severity task");
        return std::make_unique<SeverityTaskStatementNode>(lvl, std::move(finish_num), fmt, std::move(args), origin_file, origin_line);
    }

    // 12. If Statement
    if (current.type == TokenType::TOKEN_IF) {
        return parse_if_statement(DecisionModifier::NONE, current.filename, current.line);
    }

    // 13. Case / Casez / Casex Statement
    if (current.type == TokenType::TOKEN_CASE ||
        current.type == TokenType::TOKEN_CASEZ ||
        current.type == TokenType::TOKEN_CASEX) {
        return parse_case_statement(DecisionModifier::NONE, current.filename, current.line);
    }

    // 13.1. Randcase Statement (IEEE Std 1800-2023 §18.16)
    if (match(TokenType::TOKEN_RANDCASE)) {
        std::vector<RandCaseItem> items;
        std::string rc_file = current.filename;
        size_t rc_line = current.line;
        while (current.type != TokenType::TOKEN_ENDCASE && current.type != TokenType::TOKEN_EOF) {
            auto weight_expr = parse_expression();
            expect(TokenType::TOKEN_COLON, "Expected ':' after randcase weight expression");
            auto stmt = parse_statement();
            items.push_back({std::move(weight_expr), std::move(stmt)});
        }
        expect(TokenType::TOKEN_ENDCASE, "Expected 'endcase' closing randcase statement");
        match(TokenType::TOKEN_SEMICOLON); // consume optional ';'
        auto rc_node = std::make_unique<RandCaseStatementNode>(std::move(items));
        rc_node->src_file = rc_file;
        rc_node->src_line = rc_line;
        return rc_node;
    }

    // 13.2. Randsequence Statement (IEEE Std 1800-2023 §18.17)
    if (match(TokenType::TOKEN_RANDSEQUENCE)) {
        in_randsequence_depth++;
        struct RSGuard {
            int& d;
            RSGuard(int& depth) : d(depth) {}
            ~RSGuard() { d--; }
        } rsg(in_randsequence_depth);

        auto rseq = std::make_unique<RandSequenceStatementNode>();
        rseq->src_file = current.filename;
        rseq->src_line = current.line;

        if (match(TokenType::TOKEN_LPAREN)) {
            if (current.type == TokenType::TOKEN_IDENTIFIER) {
                rseq->top_production = current.text;
                advance();
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' after randsequence main production");
        }

        auto parse_prod_item = [&]() -> RandSeqProdItem {
            RandSeqProdItem item;
            if (match(TokenType::TOKEN_LBRACE)) {
                item.is_action_block = true;
                while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                    item.action_stmts.push_back(parse_statement());
                }
                expect(TokenType::TOKEN_RBRACE, "Expected '}' closing action block");
            } else {
                item.is_action_block = false;
                item.prod_name = current.text;
                advance();
                if (match(TokenType::TOKEN_LPAREN)) {
                    while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                        item.args.push_back(parse_expression());
                        if (!match(TokenType::TOKEN_COMMA)) break;
                    }
                    expect(TokenType::TOKEN_RPAREN, "Expected ')' after production arguments");
                }
            }
            return item;
        };

        while (current.type != TokenType::TOKEN_ENDSEQUENCE && current.type != TokenType::TOKEN_EOF) {
            RandSeqRule rule;
            std::string first_id = current.text;
            advance();

            if (first_id == "void" || first_id == "int" || first_id == "logic" || first_id == "bit" ||
                first_id == "byte" || first_id == "shortint" || first_id == "longint" || first_id == "string") {
                rule.return_type = first_id;
                rule.name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected production name after return type");
            } else {
                rule.name = first_id;
            }

            if (match(TokenType::TOKEN_LPAREN)) {
                while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                    std::string ptype = current.text;
                    advance();
                    std::string pname = current.text;
                    advance();
                    rule.formals.push_back({ptype, pname});
                    if (!match(TokenType::TOKEN_COMMA)) break;
                }
                expect(TokenType::TOKEN_RPAREN, "Expected ')' after production formal parameters");
            }

            expect(TokenType::TOKEN_COLON, "Expected ':' after production name");

            if (match(TokenType::TOKEN_IF)) {
                expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'if' in production");
                rule.if_cond = parse_expression();
                expect(TokenType::TOKEN_RPAREN, "Expected ')' after 'if' condition in production");
                rule.then_items.push_back(parse_prod_item());
                if (match(TokenType::TOKEN_ELSE)) {
                    rule.else_items.push_back(parse_prod_item());
                }
                match(TokenType::TOKEN_SEMICOLON);
                rule.kind = RandSeqRuleKind::IF_ELSE;
            } else if (match(TokenType::TOKEN_CASE)) {
                expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'case' in production");
                rule.case_expr = parse_expression();
                expect(TokenType::TOKEN_RPAREN, "Expected ')' after 'case' expression in production");
                while (current.type != TokenType::TOKEN_ENDCASE && current.type != TokenType::TOKEN_EOF) {
                    RandSeqCaseItem ci;
                    if (match(TokenType::TOKEN_DEFAULT)) {
                        ci.is_default = true;
                        match(TokenType::TOKEN_COLON);
                        while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_ENDCASE && current.type != TokenType::TOKEN_EOF) {
                            ci.items.push_back(parse_prod_item());
                        }
                        match(TokenType::TOKEN_SEMICOLON);
                    } else {
                        ci.is_default = false;
                        ci.exprs.push_back(parse_expression());
                        while (match(TokenType::TOKEN_COMMA)) {
                            ci.exprs.push_back(parse_expression());
                        }
                        expect(TokenType::TOKEN_COLON, "Expected ':' after case expression");
                        while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_ENDCASE && current.type != TokenType::TOKEN_EOF) {
                            ci.items.push_back(parse_prod_item());
                        }
                        match(TokenType::TOKEN_SEMICOLON);
                    }
                    rule.case_items.push_back(std::move(ci));
                }
                expect(TokenType::TOKEN_ENDCASE, "Expected 'endcase' closing case production");
                match(TokenType::TOKEN_SEMICOLON);
                rule.kind = RandSeqRuleKind::CASE;
            } else if (match(TokenType::TOKEN_REPEAT)) {
                expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'repeat' in production");
                rule.repeat_count = parse_expression();
                expect(TokenType::TOKEN_RPAREN, "Expected ')' after 'repeat' count in production");
                while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_ENDSEQUENCE && current.type != TokenType::TOKEN_EOF) {
                    rule.repeat_items.push_back(parse_prod_item());
                }
                match(TokenType::TOKEN_SEMICOLON);
                rule.kind = RandSeqRuleKind::REPEAT;
            } else {
                rule.kind = RandSeqRuleKind::NORMAL;
                while (true) {
                    RandSeqBranch branch;
                    if (current.type == TokenType::TOKEN_RAND && peek_token().type == TokenType::TOKEN_JOIN) {
                        advance(); advance();
                        branch.is_rand_join = true;
                        if (match(TokenType::TOKEN_LPAREN)) {
                            branch.rand_join_prob = parse_expression();
                            expect(TokenType::TOKEN_RPAREN, "Expected ')' after rand join probability");
                        }
                    }
                    while (current.type != TokenType::TOKEN_SEMICOLON &&
                           current.type != TokenType::TOKEN_OR_BIT &&
                           current.type != TokenType::TOKEN_COLON_EQ &&
                           current.type != TokenType::TOKEN_ENDSEQUENCE &&
                           current.type != TokenType::TOKEN_EOF) {
                        branch.items.push_back(parse_prod_item());
                    }
                    if (match(TokenType::TOKEN_COLON_EQ)) {
                        branch.weight = parse_unary();
                    }
                    rule.branches.push_back(std::move(branch));
                    if (match(TokenType::TOKEN_OR_BIT)) {
                        continue;
                    }
                    break;
                }
                match(TokenType::TOKEN_SEMICOLON);
            }
            rseq->rules.push_back(std::move(rule));
        }

        expect(TokenType::TOKEN_ENDSEQUENCE, "Expected 'endsequence' closing randsequence statement");
        match(TokenType::TOKEN_SEMICOLON);

        std::unordered_set<std::string> defined_prods;
        for (const auto& r : rseq->rules) {
            defined_prods.insert(r.name);
        }
        for (const auto& r : rseq->rules) {
            auto check_items = [&](const std::vector<RandSeqProdItem>& items) {
                for (const auto& it : items) {
                    if (!it.is_action_block) {
                        if (!defined_prods.count(it.prod_name)) {
                            throw std::runtime_error("Undefined production '" + it.prod_name + "' in randsequence per IEEE 1800-2023 18.17");
                        }
                    }
                }
            };
            if (r.kind == RandSeqRuleKind::IF_ELSE) {
                check_items(r.then_items);
                check_items(r.else_items);
            } else if (r.kind == RandSeqRuleKind::CASE) {
                for (const auto& ci : r.case_items) check_items(ci.items);
            } else if (r.kind == RandSeqRuleKind::REPEAT) {
                check_items(r.repeat_items);
            } else {
                for (const auto& b : r.branches) check_items(b.items);
            }
        }

        auto check_expr_vars = [&](ExpressionNode* expr) {
            std::function<void(ExpressionNode*)> walk = [&](ExpressionNode* e) {
                if (!e) return;
                if (auto id = dynamic_cast<IdentifierNode*>(e)) {
                    if (!is_declared_variable(id->name)) {
                        throw std::runtime_error("Variable '" + id->name + "' not declared in randsequence per IEEE 1800-2023 18.17");
                    }
                } else if (auto bin = dynamic_cast<BinaryOpNode*>(e)) {
                    walk(bin->lhs.get()); walk(bin->rhs.get());
                } else if (auto un = dynamic_cast<UnaryOpNode*>(e)) {
                    walk(un->operand.get());
                }
            };
            walk(expr);
        };
        for (const auto& r : rseq->rules) {
            if (r.if_cond) check_expr_vars(r.if_cond.get());
            if (r.case_expr) check_expr_vars(r.case_expr.get());
            if (r.repeat_count) check_expr_vars(r.repeat_count.get());
        }

        return rseq;
    }

    // 13.5. Procedural Continuous Assignments (IEEE Std 1800-2023 §10.4.2)
    if (match(TokenType::TOKEN_ASSIGN)) {
        std::string target = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected target variable identifier in procedural assign");
        while (match(TokenType::TOKEN_DOT)) {
            target += "." + current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in procedural assign target");
        }
        expect(TokenType::TOKEN_EQUAL, "Expected '=' in procedural assign");
        auto rhs = parse_expression();
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after procedural assign");
        return std::make_unique<ProceduralAssignNode>(target, std::move(rhs));
    }

    if (match(TokenType::TOKEN_DEASSIGN)) {
        std::string target = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected target variable identifier in procedural deassign");
        while (match(TokenType::TOKEN_DOT)) {
            target += "." + current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in procedural deassign target");
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after procedural deassign");
        return std::make_unique<ProceduralDeassignNode>(target);
    }

    if (match(TokenType::TOKEN_FORCE)) {
        std::string target = "";
        if (match(TokenType::TOKEN_LBRACE)) {
            target = "{";
            while (true) {
                std::string t = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected target variable identifier in force concatenation");
                while (match(TokenType::TOKEN_DOT)) {
                    t += "." + current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in force concatenation target");
                }
                target += t;
                if (match(TokenType::TOKEN_COMMA)) {
                    target += ",";
                    continue;
                }
                break;
            }
            expect(TokenType::TOKEN_RBRACE, "Expected '}' closing force concatenation");
        } else {
            target = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected target variable identifier in force");
            while (current.type == TokenType::TOKEN_DOT || current.type == TokenType::TOKEN_LBRACKET) {
                if (match(TokenType::TOKEN_DOT)) {
                    target += "." + current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in force target");
                } else if (match(TokenType::TOKEN_LBRACKET)) {
                    auto idx = parse_expression();
                    if (match(TokenType::TOKEN_COLON) || match(TokenType::TOKEN_PLUS_COLON) || match(TokenType::TOKEN_MINUS_COLON)) {
                        auto lsb = parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in force target");
                }
            }
        }
        expect(TokenType::TOKEN_EQUAL, "Expected '=' in force");
        auto rhs = parse_expression();
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after force");
        return std::make_unique<ProceduralAssignNode>(target, std::move(rhs));
    }

    if (match(TokenType::TOKEN_RELEASE)) {
        std::string target = "";
        if (match(TokenType::TOKEN_LBRACE)) {
            target = "{";
            while (true) {
                std::string t = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected target variable identifier in release concatenation");
                while (match(TokenType::TOKEN_DOT)) {
                    t += "." + current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in release concatenation target");
                }
                target += t;
                if (match(TokenType::TOKEN_COMMA)) {
                    target += ",";
                    continue;
                }
                break;
            }
            expect(TokenType::TOKEN_RBRACE, "Expected '}' closing release concatenation");
        } else {
            target = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected target variable identifier in release");
            while (current.type == TokenType::TOKEN_DOT || current.type == TokenType::TOKEN_LBRACKET) {
                if (match(TokenType::TOKEN_DOT)) {
                    target += "." + current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in release target");
                } else if (match(TokenType::TOKEN_LBRACKET)) {
                    auto idx = parse_expression();
                    if (match(TokenType::TOKEN_COLON) || match(TokenType::TOKEN_PLUS_COLON) || match(TokenType::TOKEN_MINUS_COLON)) {
                        auto lsb = parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in release target");
                }
            }
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after release");
        return std::make_unique<ProceduralDeassignNode>(target);
    }

    auto parse_intra_timing = [&](uint64_t& intra_delay, bool& has_intra_ev, EdgeType& intra_edge, std::string& intra_sig) {
        if (match(TokenType::TOKEN_HASH)) {
            intra_delay = current.num_val;
            expect(TokenType::TOKEN_NUMBER, "Expected numerical intra-assignment delay value");
        } else if (match(TokenType::TOKEN_CYCLE_DELAY)) {
            if (current.type == TokenType::TOKEN_NUMBER) {
                intra_delay = current.num_val;
                advance();
            } else if (match(TokenType::TOKEN_LPAREN)) {
                int pdepth = 1;
                while (pdepth > 0 && current.type != TokenType::TOKEN_EOF) {
                    if (current.type == TokenType::TOKEN_LPAREN) pdepth++;
                    else if (current.type == TokenType::TOKEN_RPAREN) pdepth--;
                    advance();
                }
                intra_delay = 1;
            } else if (current.type == TokenType::TOKEN_IDENTIFIER) {
                advance();
                intra_delay = 1;
            }
        } else if (match(TokenType::TOKEN_REPEAT)) {
            expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'repeat' in intra-assignment event control");
            auto rpt_cnt = parse_expression();
            expect(TokenType::TOKEN_RPAREN, "Expected ')' after repeat expression");
            expect(TokenType::TOKEN_AT, "Expected '@' after repeat(...) in intra-assignment event control");
            expect(TokenType::TOKEN_LPAREN, "Expected '(' after '@' in intra-assignment event control");
            if (match(TokenType::TOKEN_POSEDGE)) intra_edge = EdgeType::POSEDGE;
            else if (match(TokenType::TOKEN_NEGEDGE)) intra_edge = EdgeType::NEGEDGE;
            intra_sig = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier in intra-assignment event control");
            while (match(TokenType::TOKEN_DOT)) {
                intra_sig += "." + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in intra-assignment event control");
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing intra-assignment event control");
            has_intra_ev = true;
        } else if (match(TokenType::TOKEN_AT)) {
            expect(TokenType::TOKEN_LPAREN, "Expected '(' after '@' in intra-assignment event control");
            if (match(TokenType::TOKEN_POSEDGE)) intra_edge = EdgeType::POSEDGE;
            else if (match(TokenType::TOKEN_NEGEDGE)) intra_edge = EdgeType::NEGEDGE;
            intra_sig = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier in intra-assignment event control");
            while (match(TokenType::TOKEN_DOT)) {
                intra_sig += "." + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in intra-assignment event control");
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing intra-assignment event control");
            has_intra_ev = true;
        }
    };

    // 14. Multi-target concatenation assignment ({a, b} = expr; or { >> {a, b}} = expr;)
    if (match(TokenType::TOKEN_LBRACE)) {
        if (current.type == TokenType::TOKEN_SHR || current.type == TokenType::TOKEN_SHL) {
            bool is_shr = (current.type == TokenType::TOKEN_SHR);
            advance();
            StreamDirection dir = is_shr ? StreamDirection::LEFT_TO_RIGHT : StreamDirection::RIGHT_TO_LEFT;
            std::unique_ptr<ExpressionNode> slice_sz = nullptr;
            if (current.type != TokenType::TOKEN_LBRACE) {
                if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "byte") {
                    advance();
                    slice_sz = std::make_unique<NumberNode>(8ULL, 32);
                } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "shortint") {
                    advance();
                    slice_sz = std::make_unique<NumberNode>(16ULL, 32);
                } else if (current.type == TokenType::TOKEN_INTEGER || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "int")) {
                    advance();
                    slice_sz = std::make_unique<NumberNode>(32ULL, 32);
                } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "longint") {
                    advance();
                    slice_sz = std::make_unique<NumberNode>(64ULL, 32);
                } else if (current.type == TokenType::TOKEN_BIT || current.type == TokenType::TOKEN_LOGIC) {
                    advance();
                    slice_sz = std::make_unique<NumberNode>(1ULL, 32);
                } else {
                    slice_sz = parse_expression();
                }
            } else {
                slice_sz = std::make_unique<NumberNode>(1ULL, 32);
            }
            expect(TokenType::TOKEN_LBRACE, "Expected '{' in streaming concatenation");
            std::vector<std::unique_ptr<ExpressionNode>> items;
            while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                auto item_expr = parse_expression();
                if (match(TokenType::TOKEN_WITH) || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "with")) {
                    if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
                    expect(TokenType::TOKEN_LBRACKET, "Expected '[' after 'with' in stream expression");
                    parse_expression();
                    if (match(TokenType::TOKEN_PLUS_COLON) || match(TokenType::TOKEN_MINUS_COLON) || match(TokenType::TOKEN_COLON)) {
                        parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in with range");
                }
                items.push_back(std::move(item_expr));
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_RBRACE, "Expected '}' closing streaming concatenation items");
            expect(TokenType::TOKEN_RBRACE, "Expected '}' closing streaming concatenation");

            auto stream_node = std::make_unique<StreamingConcatNode>(dir, std::move(slice_sz), std::move(items));

            if (match(TokenType::TOKEN_LE)) {
            if (in_final_procedure) {
                throw std::runtime_error("Nonblocking assignment '<=' is illegal in a final procedure per IEEE 1800-2023 section 9.2.3");
            }
                if (in_final_procedure) {
                    throw std::runtime_error("Nonblocking assignment '<=' is illegal in a final procedure per IEEE 1800-2023 section 9.2.3");
                }
                uint64_t intra_delay = 0;
                bool has_intra_ev = false;
                EdgeType intra_edge = EdgeType::LEVEL;
                std::string intra_sig = "";
                parse_intra_timing(intra_delay, has_intra_ev, intra_edge, intra_sig);
                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after streaming assignment");
                auto node = std::make_unique<NonBlockingAssignNode>("", std::move(rhs), intra_delay);
                node->stream_lhs = std::move(stream_node);
                if (has_intra_ev) node->set_intra_event(intra_edge, std::move(intra_sig));
                return node;
            } else {
                expect(TokenType::TOKEN_EQUAL, "Expected '=' in streaming assignment");
                uint64_t intra_delay = 0;
                bool has_intra_ev = false;
                EdgeType intra_edge = EdgeType::LEVEL;
                std::string intra_sig = "";
                parse_intra_timing(intra_delay, has_intra_ev, intra_edge, intra_sig);
                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after streaming assignment");
                auto node = std::make_unique<BlockingAssignNode>("", std::move(rhs), nullptr, intra_delay);
                node->stream_lhs = std::move(stream_node);
                if (has_intra_ev) node->set_intra_event(intra_edge, std::move(intra_sig));
                return node;
            }
        }

        std::vector<std::string> targets;
        std::vector<std::unique_ptr<ExpressionNode>> target_exprs;
        while (true) {
            std::string t = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier in concatenation target list");
            while (match(TokenType::TOKEN_DOT)) {
                t += "." + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in concatenation target");
            }
            std::unique_ptr<ExpressionNode> target_expr = nullptr;
            if (match(TokenType::TOKEN_LBRACKET)) {
                auto idx_expr = parse_expression();
                if (match(TokenType::TOKEN_PLUS_COLON)) {
                    auto w_expr = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                    auto inode = std::make_unique<IndexSelectNode>(t, std::move(idx_expr), std::move(w_expr));
                    inode->is_plus_colon = true;
                    target_expr = std::move(inode);
                } else if (match(TokenType::TOKEN_MINUS_COLON)) {
                    auto w_expr = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                    auto inode = std::make_unique<IndexSelectNode>(t, std::move(idx_expr), std::move(w_expr));
                    inode->is_minus_colon = true;
                    target_expr = std::move(inode);
                } else if (match(TokenType::TOKEN_COLON)) {
                    auto lsb_expr = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                    auto inode = std::make_unique<IndexSelectNode>(t, std::move(idx_expr), std::move(lsb_expr));
                    inode->is_part_select = true;
                    target_expr = std::move(inode);
                } else {
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                    target_expr = std::make_unique<IndexSelectNode>(t, std::move(idx_expr));
                }
            } else {
                target_expr = std::make_unique<IdentifierNode>(t);
            }
            targets.push_back(t);
            target_exprs.push_back(std::move(target_expr));
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_RBRACE, "Expected '}' closing concatenation target list");
        bool is_nba = false;
        std::string op_str = "";
        if (match(TokenType::TOKEN_LE)) {
            is_nba = true;
        } else if (match(TokenType::TOKEN_PLUS_EQUAL)) op_str = "+";
        else if (match(TokenType::TOKEN_MINUS_EQUAL)) op_str = "-";
        else if (match(TokenType::TOKEN_STAR_EQUAL)) op_str = "*";
        else if (match(TokenType::TOKEN_SLASH_EQUAL)) op_str = "/";
        else if (match(TokenType::TOKEN_PERCENT_EQUAL)) op_str = "%";
        else if (match(TokenType::TOKEN_AND_EQUAL)) op_str = "&";
        else if (match(TokenType::TOKEN_OR_EQUAL)) op_str = "|";
        else if (match(TokenType::TOKEN_XOR_EQUAL)) op_str = "^";
        else if (match(TokenType::TOKEN_SHL_EQUAL)) op_str = "<<";
        else if (match(TokenType::TOKEN_SHR_EQUAL)) op_str = ">>";
        else if (match(TokenType::TOKEN_ASHR_EQUAL)) op_str = ">>>";
        else if (match(TokenType::TOKEN_ASHL_EQUAL)) op_str = "<<<";
        else {
            expect(TokenType::TOKEN_EQUAL, "Expected '=' or '<=' in concatenation assignment");
        }
        
        uint64_t intra_delay = 0;
        bool has_intra_ev = false;
        EdgeType intra_edge = EdgeType::LEVEL;
        std::string intra_sig = "";
        parse_intra_timing(intra_delay, has_intra_ev, intra_edge, intra_sig);

        auto rhs = parse_expression();
        if (!op_str.empty()) {
            std::vector<std::unique_ptr<ExpressionNode>> lhs_elems;
            for (const auto& te : target_exprs) {
                lhs_elems.push_back(te->clone());
            }
            auto lhs_cat = std::make_unique<ConcatNode>(std::move(lhs_elems));
            auto bnode = std::make_unique<BinaryOpNode>(op_str, std::move(lhs_cat), std::move(rhs));
            bnode->is_compound_assign = true;
            rhs = std::move(bnode);
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after concatenation assignment");
        if (is_nba) {
            auto node = std::make_unique<NonBlockingAssignNode>(targets, std::move(rhs), intra_delay);
            node->lhs_target_exprs = std::move(target_exprs);
            if (has_intra_ev) node->set_intra_event(intra_edge, std::move(intra_sig));
            return node;
        } else {
            auto node = std::make_unique<BlockingAssignNode>(targets, std::move(rhs), intra_delay);
            node->lhs_target_exprs = std::move(target_exprs);
            if (has_intra_ev) node->set_intra_event(intra_edge, std::move(intra_sig));
            return node;
        }
    }

    if (current.type == TokenType::TOKEN_ASSERT || current.type == TokenType::TOKEN_ASSUME || current.type == TokenType::TOKEN_EXPECT || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "cover")) {
        return parse_assert_property(stmt_label);
    }

    if (match(TokenType::TOKEN_CAST)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after $cast");
        auto dest = parse_expression();
        expect(TokenType::TOKEN_COMMA, "Expected ',' in $cast(dest, src)");
        auto src = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $cast");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $cast statement");
        return std::make_unique<SysCastStatementNode>(std::move(dest), std::move(src));
    }

    if (match(TokenType::TOKEN_VOID)) {
        expect(TokenType::TOKEN_APOSTROPHE, "Expected ''' after void in cast");
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after void'");
        auto expr = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing void'(...)");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after void'(...)");
        return std::make_unique<VoidCastStatementNode>(std::move(expr));
    }

    if (match(TokenType::TOKEN_PLUS_PLUS)) {
        std::string target = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '++'");
        if (match(TokenType::TOKEN_LBRACKET)) {
            auto idx_expr = parse_expression();
            std::unique_ptr<ExpressionNode> lsb_expr = nullptr;
            bool is_plus_c = false, is_minus_c = false;
            if (match(TokenType::TOKEN_COLON)) {
                lsb_expr = parse_expression();
            } else if (match(TokenType::TOKEN_PLUS_COLON)) {
                is_plus_c = true;
                lsb_expr = parse_expression();
            } else if (match(TokenType::TOKEN_MINUS_COLON)) {
                is_minus_c = true;
                lsb_expr = parse_expression();
            }
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in bit select");
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after '++<ident>[...]'");
            auto lhs_idx = std::make_unique<IndexSelectNode>(target, idx_expr->clone());
            if (lsb_expr) {
                lhs_idx->lsb_expr = lsb_expr->clone();
                lhs_idx->is_part_select = true;
                lhs_idx->is_plus_colon = is_plus_c;
                lhs_idx->is_minus_colon = is_minus_c;
            }
            auto one = std::make_unique<NumberNode>(1ULL, 32, false, true);
            auto add = std::make_unique<BinaryOpNode>("+", std::move(lhs_idx), std::move(one));
            auto node = std::make_unique<BlockingAssignNode>(target, std::move(add), std::move(idx_expr));
            node->bit_lsb = std::move(lsb_expr);
            node->is_plus_colon = is_plus_c;
            node->is_minus_colon = is_minus_c;
            return node;
        } else {
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after '++<ident>'");
            auto lhs_id = std::make_unique<IdentifierNode>(target);
            auto one = std::make_unique<NumberNode>(1ULL, 32, false, true);
            auto add = std::make_unique<BinaryOpNode>("+", std::move(lhs_id), std::move(one));
            return std::make_unique<BlockingAssignNode>(target, std::move(add));
        }
    }

    if (match(TokenType::TOKEN_MINUS_MINUS)) {
        std::string target = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '--'");
        if (match(TokenType::TOKEN_LBRACKET)) {
            auto idx_expr = parse_expression();
            std::unique_ptr<ExpressionNode> lsb_expr = nullptr;
            bool is_plus_c = false, is_minus_c = false;
            if (match(TokenType::TOKEN_COLON)) {
                lsb_expr = parse_expression();
            } else if (match(TokenType::TOKEN_PLUS_COLON)) {
                is_plus_c = true;
                lsb_expr = parse_expression();
            } else if (match(TokenType::TOKEN_MINUS_COLON)) {
                is_minus_c = true;
                lsb_expr = parse_expression();
            }
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in bit select");
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after '--<ident>[...]'");
            auto lhs_idx = std::make_unique<IndexSelectNode>(target, idx_expr->clone());
            if (lsb_expr) {
                lhs_idx->lsb_expr = lsb_expr->clone();
                lhs_idx->is_part_select = true;
                lhs_idx->is_plus_colon = is_plus_c;
                lhs_idx->is_minus_colon = is_minus_c;
            }
            auto one = std::make_unique<NumberNode>(1ULL, 32, false, true);
            auto sub = std::make_unique<BinaryOpNode>("-", std::move(lhs_idx), std::move(one));
            auto node = std::make_unique<BlockingAssignNode>(target, std::move(sub), std::move(idx_expr));
            node->bit_lsb = std::move(lsb_expr);
            node->is_plus_colon = is_plus_c;
            node->is_minus_colon = is_minus_c;
            return node;
        } else {
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after '--<ident>'");
            auto lhs_id = std::make_unique<IdentifierNode>(target);
            auto one = std::make_unique<NumberNode>(1ULL, 32, false, true);
            auto sub = std::make_unique<BinaryOpNode>("-", std::move(lhs_id), std::move(one));
            return std::make_unique<BlockingAssignNode>(target, std::move(sub));
        }
    }

    // 15. Identifiers (Assignments, Bit-Selects, Memory Writes, Task Calls)
    if (current.type == TokenType::TOKEN_IDENTIFIER || current.type == TokenType::TOKEN_THIS || current.type == TokenType::TOKEN_SUPER) {
        std::string ident = current.text;
        advance();
        while (match(TokenType::TOKEN_DOT)) {
            if (current.type == TokenType::TOKEN_NEW) {
                ident += ".new";
                advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER ||
                       current.type == TokenType::TOKEN_GATE_AND ||
                       current.type == TokenType::TOKEN_GATE_OR ||
                       current.type == TokenType::TOKEN_GATE_XOR ||
                       current.type == TokenType::TOKEN_OR ||
                       current.type == TokenType::TOKEN_UNIQUE) {
                ident += "." + current.text;
                advance();
            } else {
                ident += "." + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.'");
            }
        }

        if (current_parsing_module && current_parsing_module->genvars.count(ident)) {
            bool is_declared_var = false;
            for (const auto& s : current_parsing_module->signals) {
                if (s.name == ident) { is_declared_var = true; break; }
            }
            if (!is_declared_var) {
                for (const auto& p : current_parsing_module->ports) {
                    if (p.name == ident) { is_declared_var = true; break; }
                }
            }
            if (!is_declared_var && lookup_local_decl_var(ident) != nullptr) {
                is_declared_var = true;
            }
            if (!is_declared_var) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: genvar '" + ident + "' modified in procedural context per IEEE 1800-2023 section 27.4");
            }
        }

        if (match(TokenType::TOKEN_LBRACKET)) {
            auto idx_expr = parse_expression();
            std::unique_ptr<ExpressionNode> lsb_expr = nullptr;
            bool is_plus_c = false;
            bool is_minus_c = false;
            if (match(TokenType::TOKEN_COLON)) {
                lsb_expr = parse_expression();
            } else if (match(TokenType::TOKEN_PLUS_COLON)) {
                lsb_expr = parse_expression();
                is_plus_c = true;
            } else if (match(TokenType::TOKEN_MINUS_COLON)) {
                lsb_expr = parse_expression();
                is_minus_c = true;
            }
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in index select");

            std::unique_ptr<ExpressionNode> idx_expr2 = nullptr;
            std::unique_ptr<ExpressionNode> lsb_expr2 = nullptr;
            bool is_plus_c2 = false;
            bool is_minus_c2 = false;
            bool has_dim2 = false;
            if (match(TokenType::TOKEN_LBRACKET)) {
                has_dim2 = true;
                idx_expr2 = parse_expression();
                if (match(TokenType::TOKEN_COLON)) {
                    lsb_expr2 = parse_expression();
                } else if (match(TokenType::TOKEN_PLUS_COLON)) {
                    lsb_expr2 = parse_expression();
                    is_plus_c2 = true;
                } else if (match(TokenType::TOKEN_MINUS_COLON)) {
                    lsb_expr2 = parse_expression();
                    is_minus_c2 = true;
                }
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in index select");
            }

            std::vector<std::unique_ptr<ExpressionNode>> extra_indices;
            while (match(TokenType::TOKEN_LBRACKET)) {
                auto extra_idx = parse_expression();
                if (match(TokenType::TOKEN_COLON) || match(TokenType::TOKEN_PLUS_COLON) || match(TokenType::TOKEN_MINUS_COLON)) {
                    parse_expression();
                }
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in index select");
                extra_indices.push_back(std::move(extra_idx));
            }

            std::string post_member_name = "";
            while (match(TokenType::TOKEN_DOT)) {
                std::string m = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.'");
                if (!post_member_name.empty()) post_member_name += ".";
                post_member_name += m;
                while (match(TokenType::TOKEN_LBRACKET)) {
                    post_member_name += "[";
                    auto sub_e = parse_expression();
                    if (auto num = dynamic_cast<NumberNode*>(sub_e.get())) {
                        post_member_name += std::to_string(num->aval);
                    } else if (auto id = dynamic_cast<IdentifierNode*>(sub_e.get())) {
                        post_member_name += id->name;
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing index in member select");
                    post_member_name += "]";
                }
            }
            if (!post_member_name.empty()) {
                std::unique_ptr<ExpressionNode> mem_msb = nullptr;
                std::unique_ptr<ExpressionNode> mem_lsb = nullptr;
                bool has_mem_sel = false;
                bool is_part_sel = false;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    has_mem_sel = true;
                    mem_msb = parse_expression();
                    if (match(TokenType::TOKEN_COLON)) {
                        is_part_sel = true;
                        mem_lsb = parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing index in member select");
                }
                if (match(TokenType::TOKEN_EQUAL)) {
                    uint64_t intra_delay = 0;
                    bool has_intra_ev = false;
                    EdgeType intra_edge = EdgeType::LEVEL;
                    std::string intra_sig = "";
                    parse_intra_timing(intra_delay, has_intra_ev, intra_edge, intra_sig);

                    auto rhs = parse_expression();
                    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assignment");
                    auto node = std::make_unique<MemAssignNode>(ident, std::move(idx_expr), std::move(rhs), false, intra_delay);
                    node->member_name = post_member_name;
                    node->has_member_select = has_mem_sel;
                    node->is_part_select = is_part_sel;
                    node->member_msb_expr = std::move(mem_msb);
                    node->member_lsb_expr = std::move(mem_lsb);
                    if (has_intra_ev) node->set_intra_event(intra_edge, std::move(intra_sig));
                    return node;
                }
                if (match(TokenType::TOKEN_LE)) {
                    if (in_final_procedure) {
                        throw std::runtime_error("Nonblocking assignment '<=' is illegal in a final procedure per IEEE 1800-2023 section 9.2.3");
                    }
                    uint64_t intra_delay = 0;
                    bool has_intra_ev = false;
                    EdgeType intra_edge = EdgeType::LEVEL;
                    std::string intra_sig = "";
                    parse_intra_timing(intra_delay, has_intra_ev, intra_edge, intra_sig);

                    auto rhs = parse_expression();
                    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after non-blocking assignment");
                    auto node = std::make_unique<MemAssignNode>(ident, std::move(idx_expr), std::move(rhs), true, intra_delay);
                    node->member_name = post_member_name;
                    node->has_member_select = has_mem_sel;
                    node->is_part_select = is_part_sel;
                    node->member_msb_expr = std::move(mem_msb);
                    node->member_lsb_expr = std::move(mem_lsb);
                    if (has_intra_ev) node->set_intra_event(intra_edge, std::move(intra_sig));
                    return node;
                }
                std::string idx_str = "0";
                if (auto num = dynamic_cast<NumberNode*>(idx_expr.get())) idx_str = std::to_string(num->aval);
                ident += "[" + idx_str + "]." + post_member_name;
                idx_expr = nullptr;
            } else if (match(TokenType::TOKEN_EQUAL)) {
                uint64_t intra_delay = 0;
                bool has_intra_ev = false;
                EdgeType intra_edge = EdgeType::LEVEL;
                std::string intra_sig = "";
                parse_intra_timing(intra_delay, has_intra_ev, intra_edge, intra_sig);

                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assignment");
                auto node = std::make_unique<BlockingAssignNode>(ident, std::move(rhs), std::move(idx_expr), intra_delay);
                node->bit_lsb = std::move(lsb_expr);
                node->is_plus_colon = is_plus_c;
                node->is_minus_colon = is_minus_c;
                node->bit_idx2 = std::move(idx_expr2);
                node->bit_lsb2 = std::move(lsb_expr2);
                node->is_plus_colon2 = is_plus_c2;
                node->is_minus_colon2 = is_minus_c2;
                node->extra_indices = std::move(extra_indices);
                if (has_intra_ev) node->set_intra_event(intra_edge, std::move(intra_sig));
                return node;
            }
            if (match(TokenType::TOKEN_PLUS_PLUS)) {
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after '++'");
                auto lhs_idx = std::make_unique<IndexSelectNode>(ident, idx_expr ? idx_expr->clone() : nullptr);
                if (lsb_expr) {
                    lhs_idx->lsb_expr = lsb_expr->clone();
                    lhs_idx->is_part_select = true;
                    lhs_idx->is_plus_colon = is_plus_c;
                    lhs_idx->is_minus_colon = is_minus_c;
                }
                auto one = std::make_unique<NumberNode>(1ULL, 32, false, true);
                auto add = std::make_unique<BinaryOpNode>("+", std::move(lhs_idx), std::move(one));
                auto node = std::make_unique<BlockingAssignNode>(ident, std::move(add), std::move(idx_expr));
                node->bit_lsb = std::move(lsb_expr);
                node->is_plus_colon = is_plus_c;
                node->is_minus_colon = is_minus_c;
                return node;
            }
            if (match(TokenType::TOKEN_MINUS_MINUS)) {
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after '--'");
                auto lhs_idx = std::make_unique<IndexSelectNode>(ident, idx_expr ? idx_expr->clone() : nullptr);
                if (lsb_expr) {
                    lhs_idx->lsb_expr = lsb_expr->clone();
                    lhs_idx->is_part_select = true;
                    lhs_idx->is_plus_colon = is_plus_c;
                    lhs_idx->is_minus_colon = is_minus_c;
                }
                auto one = std::make_unique<NumberNode>(1ULL, 32, false, true);
                auto sub = std::make_unique<BinaryOpNode>("-", std::move(lhs_idx), std::move(one));
                auto node = std::make_unique<BlockingAssignNode>(ident, std::move(sub), std::move(idx_expr));
                node->bit_lsb = std::move(lsb_expr);
                node->is_plus_colon = is_plus_c;
                node->is_minus_colon = is_minus_c;
                return node;
            }
            std::string comp_op = "";
            if (match(TokenType::TOKEN_PLUS_EQUAL)) comp_op = "+=";
            else if (match(TokenType::TOKEN_MINUS_EQUAL)) comp_op = "-=";
            else if (match(TokenType::TOKEN_STAR_EQUAL)) comp_op = "*=";
            else if (match(TokenType::TOKEN_SLASH_EQUAL)) comp_op = "/=";
            else if (match(TokenType::TOKEN_PERCENT_EQUAL)) comp_op = "%=";
            else if (match(TokenType::TOKEN_AND_EQUAL)) comp_op = "&=";
            else if (match(TokenType::TOKEN_OR_EQUAL)) comp_op = "|=";
            else if (match(TokenType::TOKEN_XOR_EQUAL)) comp_op = "^=";
            else if (match(TokenType::TOKEN_SHL_EQUAL)) comp_op = "<<=";
            else if (match(TokenType::TOKEN_SHR_EQUAL)) comp_op = ">>=";
            else if (match(TokenType::TOKEN_ASHL_EQUAL)) comp_op = "<<<=";
            else if (match(TokenType::TOKEN_ASHR_EQUAL)) comp_op = ">>>=";
            if (!comp_op.empty()) {
                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assignment");
                auto node = std::make_unique<BlockingAssignNode>(ident, std::move(rhs), std::move(idx_expr));
                node->op = comp_op;
                node->bit_lsb = std::move(lsb_expr);
                node->is_plus_colon = is_plus_c;
                node->is_minus_colon = is_minus_c;
                node->bit_idx2 = std::move(idx_expr2);
                node->bit_lsb2 = std::move(lsb_expr2);
                node->is_plus_colon2 = is_plus_c2;
                node->is_minus_colon2 = is_minus_c2;
                node->extra_indices = std::move(extra_indices);
                return node;
            }
            if (match(TokenType::TOKEN_LE)) {
            if (in_final_procedure) {
                throw std::runtime_error("Nonblocking assignment '<=' is illegal in a final procedure per IEEE 1800-2023 section 9.2.3");
            }
                if (in_final_procedure) {
                    throw std::runtime_error("Nonblocking assignment '<=' is illegal in a final procedure per IEEE 1800-2023 section 9.2.3");
                }
                uint64_t intra_delay = 0;
                bool has_intra_ev = false;
                EdgeType intra_edge = EdgeType::LEVEL;
                std::string intra_sig = "";
                parse_intra_timing(intra_delay, has_intra_ev, intra_edge, intra_sig);

                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after non-blocking assignment");
                if (lsb_expr || has_dim2 || !extra_indices.empty()) {
                    auto node = std::make_unique<NonBlockingAssignNode>(ident, std::move(rhs), intra_delay);
                    node->bit_idx = std::move(idx_expr);
                    node->bit_lsb = std::move(lsb_expr);
                    node->is_plus_colon = is_plus_c;
                    node->is_minus_colon = is_minus_c;
                    node->bit_idx2 = std::move(idx_expr2);
                    node->bit_lsb2 = std::move(lsb_expr2);
                    node->is_plus_colon2 = is_plus_c2;
                    node->is_minus_colon2 = is_minus_c2;
                    node->extra_indices = std::move(extra_indices);
                    if (has_intra_ev) node->set_intra_event(intra_edge, std::move(intra_sig));
                    return node;
                } else {
                    auto node = std::make_unique<MemAssignNode>(ident, std::move(idx_expr), std::move(rhs), true, intra_delay);
                    if (has_intra_ev) node->set_intra_event(intra_edge, std::move(intra_sig));
                    return node;
                }
            }
        }

        if (match(TokenType::TOKEN_PLUS_PLUS)) {
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after '++'");
            auto lhs_id = std::make_unique<IdentifierNode>(ident);
            auto one = std::make_unique<NumberNode>(1ULL, 32, false, true);
            auto add = std::make_unique<BinaryOpNode>("+", std::move(lhs_id), std::move(one));
            return std::make_unique<BlockingAssignNode>(ident, std::move(add));
        }

        if (match(TokenType::TOKEN_MINUS_MINUS)) {
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after '--'");
            auto lhs_id = std::make_unique<IdentifierNode>(ident);
            auto one = std::make_unique<NumberNode>(1ULL, 32, false, true);
            auto sub = std::make_unique<BinaryOpNode>("-", std::move(lhs_id), std::move(one));
            return std::make_unique<BlockingAssignNode>(ident, std::move(sub));
        }

        {
            std::string comp_op = "";
            if (match(TokenType::TOKEN_PLUS_EQUAL)) comp_op = "+";
            else if (match(TokenType::TOKEN_MINUS_EQUAL)) comp_op = "-";
            else if (match(TokenType::TOKEN_STAR_EQUAL)) comp_op = "*";
            else if (match(TokenType::TOKEN_SLASH_EQUAL)) comp_op = "/";
            else if (match(TokenType::TOKEN_PERCENT_EQUAL)) comp_op = "%";
            else if (match(TokenType::TOKEN_AND_EQUAL)) comp_op = "&";
            else if (match(TokenType::TOKEN_OR_EQUAL)) comp_op = "|";
            else if (match(TokenType::TOKEN_XOR_EQUAL)) comp_op = "^";
            else if (match(TokenType::TOKEN_SHL_EQUAL)) comp_op = "<<";
            else if (match(TokenType::TOKEN_SHR_EQUAL)) comp_op = ">>";
            else if (match(TokenType::TOKEN_ASHL_EQUAL)) comp_op = "<<<";
            else if (match(TokenType::TOKEN_ASHR_EQUAL)) comp_op = ">>>";
            if (!comp_op.empty()) {
                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assignment");
                if (current_scope_var_types.count(ident) && get_enum_typedef(current_scope_var_types[ident]) != nullptr) {
                    throw std::runtime_error("Compound assignment to enum variable '" + ident + "' requires explicit cast per IEEE 1800-2023 6.19.4");
                }
                auto lhs_id = std::make_unique<IdentifierNode>(ident);
                auto op_expr = std::make_unique<BinaryOpNode>(comp_op, std::move(lhs_id), std::move(rhs));
                op_expr->is_compound_assign = true;
                return std::make_unique<BlockingAssignNode>(ident, std::move(op_expr));
            }
        }

        if (match(TokenType::TOKEN_EQUAL)) {
            uint64_t intra_delay = 0;
            bool has_intra_ev = false;
            EdgeType intra_edge = EdgeType::LEVEL;
            std::string intra_sig = "";
            parse_intra_timing(intra_delay, has_intra_ev, intra_edge, intra_sig);

            auto rhs = parse_expression();
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after blocking assignment");
            std::string var_type = "";
            bool is_dyn = false;
            if (current_scope_var_types.count(ident)) {
                var_type = current_scope_var_types[ident];
            } else if (current_parsing_module) {
                for (const auto& s : current_parsing_module->signals) {
                    if (s.name == ident) {
                        var_type = s.type_name;
                        if (s.is_dynamic_array || s.is_queue || s.is_array) is_dyn = true;
                        break;
                    }
                }
                if (var_type.empty()) {
                    for (const auto& p : current_parsing_module->ports) {
                        if (p.name == ident) {
                            var_type = p.type_name;
                            if (p.is_dynamic_array || p.is_queue || p.is_array) is_dyn = true;
                            break;
                        }
                    }
                }
            }
            if (!var_type.empty() && get_enum_typedef(var_type) != nullptr) {
                if (dynamic_cast<NumberNode*>(rhs.get()) || dynamic_cast<BinaryOpNode*>(rhs.get())) {
                    throw std::runtime_error("Assigning numerical expression to enum variable '" + ident + "' without explicit cast is illegal per IEEE 1800-2023 6.19.3/6.19.4");
                }
            }
            if (is_dyn) {
                if (dynamic_cast<NumberNode*>(rhs.get()) || dynamic_cast<BinaryOpNode*>(rhs.get())) {
                    throw std::runtime_error("[CAS-DYN] Cannot assign scalar expression to array variable '" + ident + "' per IEEE 1800-2023 §7.5");
                }
            }
            auto node = std::make_unique<BlockingAssignNode>(ident, std::move(rhs), nullptr, intra_delay);
            if (has_intra_ev) node->set_intra_event(intra_edge, std::move(intra_sig));
            return node;
        }

        if (match(TokenType::TOKEN_LE)) {
            if (in_final_procedure) {
                throw std::runtime_error("Nonblocking assignment '<=' is illegal in a final procedure per IEEE 1800-2023 section 9.2.3");
            }
            uint64_t intra_delay = 0;
            bool has_intra_ev = false;
            EdgeType intra_edge = EdgeType::LEVEL;
            std::string intra_sig = "";
            parse_intra_timing(intra_delay, has_intra_ev, intra_edge, intra_sig);

            auto rhs = parse_expression();
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after non-blocking assignment");
            auto node = std::make_unique<NonBlockingAssignNode>(ident, std::move(rhs), intra_delay);
            if (has_intra_ev) node->set_intra_event(intra_edge, std::move(intra_sig));
            return node;
        }

        bool is_rand_call = (ident == "randomize" || (ident.size() > 10 && ident.substr(ident.size() - 10) == ".randomize"));
        if (is_rand_call) {
            if (match(TokenType::TOKEN_LPAREN)) {
                if (current.type != TokenType::TOKEN_RPAREN) {
                    while (true) {
                        parse_expression();
                        if (match(TokenType::TOKEN_COMMA)) continue;
                        break;
                    }
                }
                expect(TokenType::TOKEN_RPAREN, "Expected ')' in randomize call");
            }
            std::vector<std::unique_ptr<ExpressionNode>> inline_constraints;
            if (match(TokenType::TOKEN_WITH)) {
                if (match(TokenType::TOKEN_LPAREN)) {
                    while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                        advance();
                    }
                    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing with argument list");
                }
                inline_constraints = parse_inline_constraint_block();
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after statement");
            std::unique_ptr<ExpressionNode> target_handle = nullptr;
            if (ident == "randomize" || ident == "this.randomize") {
                target_handle = std::make_unique<ThisNode>();
            } else {
                std::string obj_name = ident.substr(0, ident.size() - 10);
                target_handle = std::make_unique<IdentifierNode>(obj_name);
            }
            auto rcall = std::make_unique<RandomizeCallNode>(std::move(target_handle), std::move(inline_constraints));
            return std::make_unique<RandomizeStatementNode>(std::move(rcall));
        }

        std::vector<std::unique_ptr<ExpressionNode>> task_args;
        std::vector<std::string> task_arg_names;
        if (match(TokenType::TOKEN_LPAREN)) {
            auto parsed = parse_call_argument_list();
            task_args = std::move(parsed.args);
            task_arg_names = std::move(parsed.arg_names);
        }
        if (match(TokenType::TOKEN_DOT)) {
            std::string chained = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.'");
            while (true) {
                if (match(TokenType::TOKEN_LPAREN)) {
                    auto parsed_sub = parse_call_argument_list();
                } else if (match(TokenType::TOKEN_LBRACKET)) {
                    parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in index select");
                }
                if (match(TokenType::TOKEN_DOT)) {
                    chained += "." + current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.'");
                    continue;
                }
                break;
            }
            if (match(TokenType::TOKEN_EQUAL)) {
                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assignment");
                return std::make_unique<BlockingAssignNode>(ident + "." + chained, std::move(rhs));
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after statement");
            return std::make_unique<TaskCallNode>(ident + "." + chained, std::move(task_args));
        }
        std::unique_ptr<ExpressionNode> with_expr = nullptr;
        if (match(TokenType::TOKEN_WITH)) {
            expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'with'");
            with_expr = parse_expression();
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing with expression");
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after statement");
        return std::make_unique<TaskCallNode>(ident, std::move(task_args), "", std::move(task_arg_names), std::move(with_expr));
    }

    std::string loc = current.filename.empty() 
        ? ("line " + std::to_string(current.line)) 
        : (current.filename + ":" + std::to_string(current.line));
    throw std::runtime_error(loc + ": error: Unexpected statement beginning with '" + current.text + "'");
}

std::unique_ptr<StatementNode> Parser::parse_if_statement(DecisionModifier mod, std::string mod_file, size_t mod_line) {
    std::string origin_file = current.filename;
    size_t origin_line = current.line;
    expect(TokenType::TOKEN_IF, "Expected 'if'");
    expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'if'");
    auto cond = parse_expression();
    expect(TokenType::TOKEN_RPAREN, "Expected ')' after 'if' condition");

    if (in_randsequence_depth > 0) {
        if (auto id = dynamic_cast<IdentifierNode*>(cond.get())) {
            if (!is_declared_variable(id->name)) {
                throw std::runtime_error("Variable '" + id->name + "' not declared in randsequence per IEEE 1800-2023 18.17");
            }
        }
    }

    auto if_node = std::make_unique<IfStatementNode>(std::move(cond));
    if_node->modifier = mod;
    if_node->src_file = (mod != DecisionModifier::NONE && !mod_file.empty()) ? mod_file : origin_file;
    if_node->src_line = (mod != DecisionModifier::NONE && mod_line != 0) ? mod_line : origin_line;
    if_node->then_branch = parse_statement_or_block();

    if (match(TokenType::TOKEN_ELSE)) {
        if_node->else_branch = parse_statement_or_block();
    }
    return if_node;
}

std::unique_ptr<StatementNode> Parser::parse_case_statement(DecisionModifier mod, std::string mod_file, size_t mod_line) {
    TokenType case_tok = current.type;
    std::string origin_file = current.filename;
    size_t origin_line = current.line;
    advance();
    CaseType ct = CaseType::CASE;
    if (case_tok == TokenType::TOKEN_CASEZ) ct = CaseType::CASEZ;
    else if (case_tok == TokenType::TOKEN_CASEX) ct = CaseType::CASEX;

    expect(TokenType::TOKEN_LPAREN, "Expected '(' after case keyword");
    auto sel = parse_expression();
    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing case selector");

    if (match(TokenType::TOKEN_INSIDE)) {
        ct = CaseType::CASE_INSIDE;
    } else if (match(TokenType::TOKEN_MATCHES)) {
        ct = CaseType::CASE_MATCHES;
    }

    auto case_node = std::make_unique<CaseStatementNode>(std::move(sel), ct);
    case_node->modifier = mod;
    case_node->src_file = (mod != DecisionModifier::NONE && !mod_file.empty()) ? mod_file : origin_file;
    case_node->src_line = (mod != DecisionModifier::NONE && mod_line != 0) ? mod_line : origin_line;

    while (current.type != TokenType::TOKEN_ENDCASE && current.type != TokenType::TOKEN_EOF) {
        CaseItem item;
        if (match(TokenType::TOKEN_DEFAULT)) {
            item.is_default = true;
            match(TokenType::TOKEN_COLON);
        } else {
            while (true) {
                if (ct == CaseType::CASE_INSIDE && match(TokenType::TOKEN_LBRACKET)) {
                    auto low = parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in case inside range");
                    auto high = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in case inside range");
                    item.range_exprs.push_back({std::move(low), std::move(high)});
                } else if (ct == CaseType::CASE_MATCHES && match(TokenType::TOKEN_TAGGED)) {
                    std::string tag_name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected member name after 'tagged'");
                    std::unique_ptr<ExpressionNode> pat_val = nullptr;
                    if (current.type == TokenType::TOKEN_APOSTROPHE || current.type == TokenType::TOKEN_LBRACE || (current.type != TokenType::TOKEN_COLON && current.type != TokenType::TOKEN_COMMA)) {
                        pat_val = parse_expression();
                    }
                    item.match_exprs.push_back(std::make_unique<TaggedUnionExprNode>(tag_name, std::move(pat_val)));
                } else {
                    auto expr = parse_expression();
                    if (!item.match_expr) {
                        item.match_expr = expr->clone();
                    }
                    item.match_exprs.push_back(std::move(expr));
                }
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_COLON, "Expected ':' after case match expression");
        }
        item.statements = parse_statement_or_block();
        case_node->items.push_back(std::move(item));
    }

    expect(TokenType::TOKEN_ENDCASE, "Expected 'endcase'");
    return case_node;
}

std::vector<std::unique_ptr<StatementNode>> Parser::parse_statement_or_block() {
    std::vector<std::unique_ptr<StatementNode>> stmts;
    if (match(TokenType::TOKEN_BEGIN)) {
        std::string begin_name = "";
        if (match(TokenType::TOKEN_COLON)) {
            begin_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected block name after 'begin :'");
        }
        enter_scope();
        while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
            auto s = parse_statement();
            if (auto blk = dynamic_cast<BlockStatementNode*>(s.get())) {
                for (auto& sub : blk->statements) {
                    stmts.push_back(std::move(sub));
                }
            } else {
                stmts.push_back(std::move(s));
            }
        }
        expect(TokenType::TOKEN_END, "Expected 'end' closing begin block");
        if (match(TokenType::TOKEN_COLON)) {
            std::string end_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected block name after 'end :'");
            if (!begin_name.empty() && end_name != begin_name) {
                throw std::runtime_error("Block end label '" + end_name + "' does not match begin label '" + begin_name + "'");
            }
        }
        match(TokenType::TOKEN_SEMICOLON);
        exit_scope();
        if (!begin_name.empty()) {
            auto wrapped = std::make_unique<BlockStatementNode>(std::move(stmts), begin_name);
            stmts.clear();
            stmts.push_back(std::move(wrapped));
        }
    } else {
        auto s = parse_statement();
        if (auto blk = dynamic_cast<BlockStatementNode*>(s.get())) {
            for (auto& sub : blk->statements) {
                stmts.push_back(std::move(sub));
            }
        } else {
            stmts.push_back(std::move(s));
        }
    }
    return stmts;
}

std::unique_ptr<ProcessNode> Parser::parse_always_ff() {
    expect(TokenType::TOKEN_AT, "Expected '@' after always_ff");
    expect(TokenType::TOKEN_LPAREN, "Expected '(' in sensitivity list");

    auto proc = std::make_unique<ProcessNode>(ProcessType::SEQUENTIAL);
    proc->is_always_ff = true;

    while (true) {
        EdgeType edge = EdgeType::LEVEL;
        if (match(TokenType::TOKEN_POSEDGE)) edge = EdgeType::POSEDGE;
        else if (match(TokenType::TOKEN_NEGEDGE)) edge = EdgeType::NEGEDGE;

        int paren_count = 0;
        while (match(TokenType::TOKEN_LPAREN)) paren_count++;

        if (current.type == TokenType::TOKEN_NOT || current.type == TokenType::TOKEN_TILDE ||
            current.type == TokenType::TOKEN_AND || current.type == TokenType::TOKEN_OR_BIT) {
            advance();
        }

        std::string sig = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected signal name in sensitivity list");
        while (match(TokenType::TOKEN_DOT)) {
            sig += "." + current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in sensitivity list");
        }
        while (match(TokenType::TOKEN_LBRACKET)) {
            parse_expression();
            if (match(TokenType::TOKEN_COLON)) parse_expression();
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in sensitivity list select");
        }
        while (paren_count > 0 && match(TokenType::TOKEN_RPAREN)) paren_count--;
        proc->sensitivity_list.push_back({edge, sig});

        if (match(TokenType::TOKEN_OR) || match(TokenType::TOKEN_GATE_OR) || match(TokenType::TOKEN_COMMA)) continue;
        break;
    }

    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing sensitivity list");
    proc->body = parse_statement_or_block();
    return proc;
}

ParsedCallArgs Parser::parse_call_argument_list() {
    ParsedCallArgs result;
    if (match(TokenType::TOKEN_RPAREN)) {
        return result;
    }

    bool seen_named = false;
    while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
        if (match(TokenType::TOKEN_DOT)) {
            // IEEE Std 1800-2023 §13.5.1: Named argument .name( [expr] )
            seen_named = true;
            std::string port_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected argument name after '.' in named argument call");
            expect(TokenType::TOKEN_LPAREN, "Expected '(' after argument name in named argument call");
            std::unique_ptr<ExpressionNode> arg_expr = nullptr;
            if (current.type != TokenType::TOKEN_RPAREN) {
                arg_expr = parse_expression();
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing named argument");
            result.args.push_back(std::move(arg_expr));
            result.arg_names.push_back(port_name);
        } else if (current.type == TokenType::TOKEN_COMMA) {
            // IEEE Std 1800-2023 §13.5.3: Empty positional argument slot
            if (seen_named) {
                throw std::runtime_error("[CAS-DTINPCIL] Positional argument cannot follow named argument in subroutine call per IEEE 1800-2023 section 13.5.1");
            }
            result.args.push_back(nullptr);
            result.arg_names.push_back("");
        } else {
            // Positional argument expression
            if (seen_named) {
                throw std::runtime_error("[CAS-DTINPCIL] Positional argument cannot follow named argument in subroutine call per IEEE 1800-2023 section 13.5.1");
            }
            result.args.push_back(parse_expression());
            result.arg_names.push_back("");
        }

        if (match(TokenType::TOKEN_COMMA)) {
            if (current.type == TokenType::TOKEN_RPAREN) {
                // Trailing comma before ')' -> empty slot
                if (seen_named) {
                    throw std::runtime_error("[CAS-DTINPCIL] Positional argument cannot follow named argument in subroutine call per IEEE 1800-2023 section 13.5.1");
                }
                result.args.push_back(nullptr);
                result.arg_names.push_back("");
                break;
            }
            continue;
        }
        break;
    }
    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing subroutine argument list");
    return result;
}
