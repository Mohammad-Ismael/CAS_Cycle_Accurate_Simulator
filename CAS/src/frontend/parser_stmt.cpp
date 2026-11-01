#include "../../include/frontend/parser.hpp"
#include <stdexcept>
#include <functional>
#include <cmath>

std::unique_ptr<StatementNode> Parser::parse_statement() {
    // Diagnostic [CAS-DTINPCIL]: defparam inside procedural scopes
    if (current.type == TokenType::TOKEN_DEFPARAM) {
        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: 'defparam' statement is illegal inside procedural scope per IEEE 1800-2023 section 23.10.1");
    }

    // Diagnostic [CAS-DTINPCIL]: alias inside procedural scopes
    if (current.type == TokenType::TOKEN_ALIAS) {
        throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: 'alias' construct is not permitted in procedural context per IEEE 1800-2023 section 10.6");
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

    // 0. Handle 'begin ... end' block statements
    if (match(TokenType::TOKEN_BEGIN)) {
        std::vector<std::unique_ptr<StatementNode>> stmts;
        while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
            stmts.push_back(parse_statement());
        }
        expect(TokenType::TOKEN_END, "Expected 'end' closing begin block");
        match(TokenType::TOKEN_SEMICOLON);
        return std::make_unique<BlockStatementNode>(std::move(stmts));
    }

    // Return Statement (§13.4.1)
    if (match(TokenType::TOKEN_RETURN)) {
        std::unique_ptr<ExpressionNode> ret_expr = nullptr;
        if (current.type != TokenType::TOKEN_SEMICOLON) {
            ret_expr = parse_expression();
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after return");
        return std::make_unique<ReturnStatementNode>(std::move(ret_expr));
    }

    // Virtual Interface Variable Declaration (§25.9)
    if (match(TokenType::TOKEN_VIRTUAL)) {
        std::string iface_name = "";
        std::string modport_name = "";
        if (current.type == TokenType::TOKEN_IDENTIFIER || is_interface_type(current.text)) {
            iface_name = current.text;
            advance();
            if (match(TokenType::TOKEN_DOT)) {
                modport_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected modport name after '.'");
            }
        }
        std::string vname = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected variable identifier");
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
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after variable declaration");
        auto var_stmt = std::make_unique<VarDeclStatementNode>(vname, 64, false, cg_type);
        var_stmt->is_covergroup_handle = true;
        var_stmt->covergroup_type_name = cg_type;
        return var_stmt;
    }

    // Class Handle Variable Declaration (§8.4) or Static Scope Resolution (§8.25)
    // Or lookahead for user-defined types (e.g. unknown class / interface type declared in another file)
    bool is_class_type = (current.type == TokenType::TOKEN_IDENTIFIER && is_class(current.text));
    bool is_udt_decl = false;
    if (current.type == TokenType::TOKEN_IDENTIFIER && !is_class_type && !is_typedef(current.text) &&
        !is_covergroup(current.text) && !is_interface_type(current.text) && !is_builtin_type_name(current.text)) {
        TokenType next_tok = peek_token().type;
        if (next_tok == TokenType::TOKEN_IDENTIFIER) {
            is_udt_decl = true;
        }
    }

    if (is_class_type || is_udt_decl) {
        std::string class_type = parse_class_type_and_specialize();
        if (match(TokenType::TOKEN_COLON_COLON)) {
            std::string member = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected member name after '::'");
            if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assignment");
                return std::make_unique<BlockingAssignNode>(class_type + "::" + member, std::move(rhs));
            }
            if (match(TokenType::TOKEN_LPAREN)) {
                auto parsed = parse_call_argument_list();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after static method call");
                return std::make_unique<TaskCallNode>(member, std::move(parsed.args), class_type, std::move(parsed.arg_names));
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after static member statement");
            return std::make_unique<TaskCallNode>(member, std::vector<std::unique_ptr<ExpressionNode>>(), class_type);
        }
        std::vector<std::unique_ptr<StatementNode>> vstmts;
        while (true) {
            std::string vname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected variable identifier");
            if (match(TokenType::TOKEN_LBRACKET)) {
                while (current.type != TokenType::TOKEN_RBRACKET && current.type != TokenType::TOKEN_EOF) advance();
                match(TokenType::TOKEN_RBRACKET);
            }
            std::unique_ptr<ExpressionNode> init_expr = nullptr;
            if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                init_expr = parse_expression();
            }
            auto var_stmt = std::make_unique<VarDeclStatementNode>(vname, 64, false, class_type);
            var_stmt->is_class_handle = true;
            var_stmt->class_type_name = class_type;
            var_stmt->init_expr = std::move(init_expr);
            vstmts.push_back(std::move(var_stmt));
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after variable declaration");
        if (vstmts.size() == 1) return std::move(vstmts[0]);
        return std::make_unique<BlockStatementNode>(std::move(vstmts));
    }

    // 1. Variable Declarations in Procedural Scopes (integer i; logic [7:0] temp; state_t s;)
    std::string proc_type_name = "";
    if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON_COLON) {
        std::string p_name = current.text;
        advance(); // consume package identifier
        advance(); // consume '::'
        std::string t_name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected type identifier after '::'");
        proc_type_name = p_name + "::" + t_name;
    } else if (current.type == TokenType::TOKEN_IDENTIFIER && is_typedef(current.text)) {
        proc_type_name = current.text;
        advance();
    }

    if (!proc_type_name.empty()) {
        std::string type_name = proc_type_name;
        auto enum_info = get_enum_typedef(type_name);
        auto struct_info = get_struct_typedef(type_name);
        uint32_t width = struct_info ? struct_info->total_width : (enum_info ? enum_info->base_width : 32);
        bool is_signed_var = struct_info ? struct_info->is_signed : (enum_info ? enum_info->is_signed : false);

        std::string vname = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected variable identifier");
        bool is_q = false;
        bool is_dyn = false;
        bool is_assoc = false;
        std::string assoc_key_type = "";
        uint32_t assoc_key_width = 32;
        if (match(TokenType::TOKEN_LBRACKET)) {
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
                parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
            }
        } else if (match(TokenType::TOKEN_REP_CONSEC)) {
            is_assoc = true;
            assoc_key_type = "*";
            assoc_key_width = 64;
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in associative array dimension");
        }
        std::unique_ptr<ExpressionNode> init_expr = nullptr;
        if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
            init_expr = parse_expression();
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after variable declaration");
        auto vstmt = std::make_unique<VarDeclStatementNode>(vname, width, is_signed_var, type_name, is_q, is_dyn, is_assoc, assoc_key_type, assoc_key_width);
        vstmt->init_expr = std::move(init_expr);
        return vstmt;
    }

    bool is_ident_int_type = (current.type == TokenType::TOKEN_IDENTIFIER &&
                              (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint"));
    bool is_ident_string_type = (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "string");
    bool is_ident_unsigned_type = (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned");
    bool is_chandle_stmt = (current.type == TokenType::TOKEN_CHANDLE ||
                            (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "chandle"));
    bool is_semaphore_stmt = (current.type == TokenType::TOKEN_SEMAPHORE ||
                             (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "semaphore"));
    bool is_mailbox_stmt = (current.type == TokenType::TOKEN_MAILBOX ||
                           (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "mailbox"));

    if (current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_LOGIC ||
        current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_WIRE ||
        current.type == TokenType::TOKEN_BIT || current.type == TokenType::TOKEN_REAL ||
        is_ident_int_type || is_ident_string_type || is_ident_unsigned_type || is_chandle_stmt || is_semaphore_stmt || is_mailbox_stmt) {

        bool explicit_signed = false;
        bool explicit_unsigned = false;
        if (current.type == TokenType::TOKEN_SIGNED) {
            explicit_signed = true;
            advance();
        } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned") {
            explicit_unsigned = true;
            advance();
        }

        bool is_real_var = (current.type == TokenType::TOKEN_REAL);
        bool is_int = (current.type == TokenType::TOKEN_INTEGER);
        std::string int_type_name = "";
        if (current.type == TokenType::TOKEN_IDENTIFIER &&
            (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint")) {
            int_type_name = current.text;
            is_int = true;
        }
        advance();
        if (is_mailbox_stmt && match(TokenType::TOKEN_HASH)) {
            expect(TokenType::TOKEN_LPAREN, "Expected '(' in mailbox parameter");
            while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) advance();
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing mailbox parameter");
        }
        if (current.type == TokenType::TOKEN_SIGNED) {
            explicit_signed = true;
            advance();
        } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned") {
            explicit_unsigned = true;
            advance();
        }

        bool is_signed_var = false;
        if (explicit_signed) is_signed_var = true;
        else if (explicit_unsigned) is_signed_var = false;
        else if (is_int) is_signed_var = true;

        uint32_t width = (is_chandle_stmt || is_semaphore_stmt || is_mailbox_stmt) ? 64 : (is_ident_string_type ? 64 : (is_int ? 32 : (is_real_var ? 64 : 1)));
        if (int_type_name == "byte") width = 8;
        else if (int_type_name == "shortint") width = 16;
        else if (int_type_name == "longint") width = 64;

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
        std::string tname = is_chandle_stmt ? "chandle" : (is_semaphore_stmt ? "semaphore" : (is_mailbox_stmt ? "mailbox" : (is_real_var ? "real" : (is_ident_string_type ? "string" : (!int_type_name.empty() ? int_type_name : "")))));
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
                    if (match(TokenType::TOKEN_COLON)) {
                        parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                    break;
                } else if (match(TokenType::TOKEN_RBRACKET)) {
                    is_dyn = true;
                    break;
                } else if (is_assoc_key_token(current)) {
                    is_assoc = true;
                    parse_assoc_key_type(assoc_key_type, assoc_key_width);
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in associative array dimension");
                    break;
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
            if (match(TokenType::TOKEN_REP_CONSEC)) {
                is_assoc = true;
                assoc_key_type = "*";
                assoc_key_width = 64;
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in associative array dimension");
            }
            std::unique_ptr<ExpressionNode> init_expr = nullptr;
            if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                init_expr = parse_expression();
            }
            auto vnode = std::make_unique<VarDeclStatementNode>(vname, width, is_signed_var, tname, is_q, is_dyn, is_assoc, assoc_key_type, assoc_key_width);
            std::vector<AstDimension> cloned_packed;
            for (const auto& pd : packed_dims) {
                AstDimension cad;
                cad.left_expr = pd.left_expr ? pd.left_expr->clone() : nullptr;
                cad.right_expr = pd.right_expr ? pd.right_expr->clone() : nullptr;
                cad.is_single = pd.is_single;
                cloned_packed.push_back(std::move(cad));
            }
            vnode->packed_dimensions = std::move(cloned_packed);
            vnode->unpacked_dimensions = std::move(unpacked_dims);
            vnode->is_chandle = is_chandle_stmt;
            vnode->is_semaphore = is_semaphore_stmt;
            vnode->is_mailbox = is_mailbox_stmt;
            vnode->init_expr = std::move(init_expr);
            vstmts.push_back(std::move(vnode));
            if (match(TokenType::TOKEN_COMMA)) continue;
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
        if (current.type == TokenType::TOKEN_NUMBER) {
            if (current.is_real) {
                d = static_cast<uint64_t>(std::round(current.real_val * Lexer::get_timescale_ratio()));
            } else {
                d = current.num_val * Lexer::get_timescale_ratio();
            }
            advance();
        } else if (match(TokenType::TOKEN_LPAREN)) {
            delay_expr = parse_expression();
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing delay expression");
        } else if (current.type == TokenType::TOKEN_IDENTIFIER) {
            std::string id_name = current.text;
            advance();
            delay_expr = std::make_unique<IdentifierNode>(id_name);
        } else {
            delay_expr = parse_expression();
        }

        if (match(TokenType::TOKEN_SEMICOLON)) {
            return std::make_unique<DelayStatementNode>(d, std::move(delay_expr));
        }

        auto target_stmt = parse_statement();
        std::vector<std::unique_ptr<StatementNode>> stmts;
        stmts.push_back(std::make_unique<DelayStatementNode>(d, std::move(delay_expr)));
        stmts.push_back(std::move(target_stmt));
        return std::make_unique<BlockStatementNode>(std::move(stmts));
    }

    // 3. Procedural Event Control: @(posedge clk), @(negedge clk), @(signal), @event
    if (match(TokenType::TOKEN_AT)) {
        EdgeType edge = EdgeType::LEVEL;
        std::string sig;
        if (match(TokenType::TOKEN_LPAREN)) {
            if (match(TokenType::TOKEN_POSEDGE)) edge = EdgeType::POSEDGE;
            else if (match(TokenType::TOKEN_NEGEDGE)) edge = EdgeType::NEGEDGE;

            sig = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier in event control");
            while (match(TokenType::TOKEN_DOT)) {
                sig += "." + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in event control");
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing event control");
        } else if (current.type == TokenType::TOKEN_IDENTIFIER) {
            sig = current.text;
            advance();
            while (match(TokenType::TOKEN_DOT)) {
                sig += "." + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in event control");
            }
        } else {
            throw std::runtime_error("Expected identifier or '(' after '@'");
        }

        if (match(TokenType::TOKEN_SEMICOLON)) {
            return std::make_unique<EventControlStatementNode>(edge, sig);
        }

        auto target_stmt = parse_statement();
        std::vector<std::unique_ptr<StatementNode>> stmts;
        stmts.push_back(std::make_unique<EventControlStatementNode>(edge, sig));
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
        if (match(TokenType::TOKEN_COLON)) {
            expect(TokenType::TOKEN_IDENTIFIER, "Expected fork block label");
        }

        std::vector<std::unique_ptr<StatementNode>> branches;
        while (current.type != TokenType::TOKEN_JOIN &&
               current.type != TokenType::TOKEN_JOIN_ANY &&
               current.type != TokenType::TOKEN_JOIN_NONE &&
               current.type != TokenType::TOKEN_EOF) {
            branches.push_back(parse_statement());
        }

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

        if (match(TokenType::TOKEN_COLON)) {
            expect(TokenType::TOKEN_IDENTIFIER, "Expected join block label");
        }
        match(TokenType::TOKEN_SEMICOLON);

        return std::make_unique<ForkStatementNode>(jt, std::move(branches));
    }

    // disable fork (IEEE 1800-2023 §9.6.2)
    if (match(TokenType::TOKEN_DISABLE)) {
        expect(TokenType::TOKEN_FORK, "Expected 'fork' after 'disable'");
        match(TokenType::TOKEN_SEMICOLON);
        return std::make_unique<DisableForkStatementNode>();
    }

    // wait fork (IEEE 1800-2023 §9.6.3) or wait (e.triggered) (IEEE 1800-2023 §15.5)
    if (match(TokenType::TOKEN_WAIT)) {
        if (match(TokenType::TOKEN_FORK)) {
            match(TokenType::TOKEN_SEMICOLON);
            return std::make_unique<WaitForkStatementNode>();
        }
        if (match(TokenType::TOKEN_LPAREN)) {
            std::string ev_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected event identifier in wait(...)");
            expect(TokenType::TOKEN_DOT, "Expected '.' in wait(event.triggered)");
            std::string prop = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected 'triggered' property after '.'");
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing wait(...)");
            match(TokenType::TOKEN_SEMICOLON);
            return std::make_unique<WaitEventTriggeredStatementNode>(ev_name);
        }
        throw std::runtime_error("Expected 'fork' or '(' after 'wait'");
    }

    // Event Trigger: -> e; or ->> e; (IEEE 1800-2023 §15.4)
    if (match(TokenType::TOKEN_TRIGGER)) {
        std::string ev_name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected event identifier after '->'");
        match(TokenType::TOKEN_SEMICOLON);
        return std::make_unique<EventTriggerStatementNode>(ev_name, /*is_nonblocking=*/false);
    }
    if (match(TokenType::TOKEN_NONBLOCKING_TRIGGER)) {
        std::string ev_name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected event identifier after '->>'");
        match(TokenType::TOKEN_SEMICOLON);
        return std::make_unique<EventTriggerStatementNode>(ev_name, /*is_nonblocking=*/true);
    }

    // Foreach Loop (§12.7.3)
    if (match(TokenType::TOKEN_FOREACH)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'foreach'");
        std::string arr_name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected array identifier in foreach");
        expect(TokenType::TOKEN_LBRACKET, "Expected '[' after array name in foreach");
        std::string loop_var = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected loop variable in foreach");
        expect(TokenType::TOKEN_RBRACKET, "Expected ']' after loop variable in foreach");
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing foreach header");
        auto body = parse_statement_or_block();
        return std::make_unique<ForeachStatementNode>(arr_name, loop_var, std::move(body));
    }

    // 5. For Loop
    if (match(TokenType::TOKEN_FOR)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'for'");
        auto init_stmt = parse_statement();
        auto cond_expr = parse_expression();
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after for condition");
        
        std::string target = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected loop variable in for step");
        std::unique_ptr<StatementNode> step_stmt = nullptr;
        if (match(TokenType::TOKEN_PLUS_PLUS)) {
            auto lhs_id = std::make_unique<IdentifierNode>(target);
            auto one = std::make_unique<NumberNode>(1ULL, 32);
            auto add = std::make_unique<BinaryOpNode>("+", std::move(lhs_id), std::move(one));
            step_stmt = std::make_unique<BlockingAssignNode>(target, std::move(add));
        } else if (match(TokenType::TOKEN_MINUS_MINUS)) {
            auto lhs_id = std::make_unique<IdentifierNode>(target);
            auto one = std::make_unique<NumberNode>(1ULL, 32);
            auto sub = std::make_unique<BinaryOpNode>("-", std::move(lhs_id), std::move(one));
            step_stmt = std::make_unique<BlockingAssignNode>(target, std::move(sub));
        } else {
            expect(TokenType::TOKEN_EQUAL, "Expected '=' in for step");
            auto step_rhs = parse_expression();
            step_stmt = std::make_unique<BlockingAssignNode>(target, std::move(step_rhs));
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

    // 8. System Tasks ($display / $monitor)
    if (current.type == TokenType::TOKEN_DISPLAY || current.type == TokenType::TOKEN_MONITOR) {
        bool is_mon = (current.type == TokenType::TOKEN_MONITOR);
        std::string origin_file = current.filename;
        size_t origin_line = current.line;

        advance();
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after $display / $monitor");

        bool has_lead_time = false;
        std::string fmt = "";

        if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "$time") {
            has_lead_time = true;
            advance();
            if (match(TokenType::TOKEN_COMMA)) {
                if (current.type == TokenType::TOKEN_STRING) {
                    fmt = current.text;
                    advance();
                }
            }
        } else if (current.type == TokenType::TOKEN_STRING) {
            fmt = current.text;
            advance();
        }

        std::vector<std::unique_ptr<ExpressionNode>> args;
        while (match(TokenType::TOKEN_COMMA)) {
            args.push_back(parse_expression());
        }

        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $display / $monitor argument list");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $display / $monitor");
        return std::make_unique<DisplayStatementNode>(fmt, std::move(args), is_mon, has_lead_time, origin_file, origin_line);
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
        if (match(TokenType::TOKEN_LPAREN)) expect(TokenType::TOKEN_RPAREN, "Expected ')'");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $finish/$stop");
        return std::make_unique<FinishStatementNode>(origin_file, origin_line);
    }

    if (match(TokenType::TOKEN_EXIT)) {
        std::string origin_file = current.filename;
        size_t origin_line = current.line;
        if (match(TokenType::TOKEN_LPAREN)) expect(TokenType::TOKEN_RPAREN, "Expected ')'");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $exit");
        return std::make_unique<ExitStatementNode>(origin_file, origin_line);
    }

    // 12. If Statement
    if (match(TokenType::TOKEN_IF)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'if'");
        auto cond = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' after 'if' condition");

        auto if_node = std::make_unique<IfStatementNode>(std::move(cond));
        if_node->then_branch = parse_statement_or_block();

        if (match(TokenType::TOKEN_ELSE)) {
            if_node->else_branch = parse_statement_or_block();
        }
        return if_node;
    }

    // 13. Case / Casez / Casex Statement
    if (current.type == TokenType::TOKEN_CASE ||
        current.type == TokenType::TOKEN_CASEZ ||
        current.type == TokenType::TOKEN_CASEX) {

        TokenType case_tok = current.type;
        advance();
        CaseType ct = CaseType::CASE;
        if (case_tok == TokenType::TOKEN_CASEZ) ct = CaseType::CASEZ;
        else if (case_tok == TokenType::TOKEN_CASEX) ct = CaseType::CASEX;

        expect(TokenType::TOKEN_LPAREN, "Expected '(' after case keyword");
        auto sel = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing case selector");

        auto case_node = std::make_unique<CaseStatementNode>(std::move(sel), ct);

        while (current.type != TokenType::TOKEN_ENDCASE && current.type != TokenType::TOKEN_EOF) {
            CaseItem item;
            if (match(TokenType::TOKEN_DEFAULT)) {
                item.is_default = true;
                match(TokenType::TOKEN_COLON);
            } else {
                while (true) {
                    auto expr = parse_expression();
                    if (!item.match_expr) {
                        item.match_expr = expr->clone();
                    }
                    item.match_exprs.push_back(std::move(expr));
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

    auto parse_intra_timing = [&](uint64_t& intra_delay, bool& has_intra_ev, EdgeType& intra_edge, std::string& intra_sig) {
        if (match(TokenType::TOKEN_HASH)) {
            intra_delay = current.num_val;
            expect(TokenType::TOKEN_NUMBER, "Expected numerical intra-assignment delay value");
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
                items.push_back(parse_expression());
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_RBRACE, "Expected '}' closing streaming concatenation items");
            expect(TokenType::TOKEN_RBRACE, "Expected '}' closing streaming concatenation");

            auto stream_node = std::make_unique<StreamingConcatNode>(dir, std::move(slice_sz), std::move(items));

            if (match(TokenType::TOKEN_LE)) {
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
        while (true) {
            targets.push_back(current.text);
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier in concatenation target list");
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_RBRACE, "Expected '}' closing concatenation target list");
        expect(TokenType::TOKEN_EQUAL, "Expected '=' in concatenation blocking assignment");
        
        uint64_t intra_delay = 0;
        bool has_intra_ev = false;
        EdgeType intra_edge = EdgeType::LEVEL;
        std::string intra_sig = "";
        parse_intra_timing(intra_delay, has_intra_ev, intra_edge, intra_sig);

        auto rhs = parse_expression();
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after concatenation assignment");
        auto node = std::make_unique<BlockingAssignNode>(targets, std::move(rhs), intra_delay);
        if (has_intra_ev) node->set_intra_event(intra_edge, std::move(intra_sig));
        return node;
    }

    if (current.type == TokenType::TOKEN_ASSERT) {
        return parse_assert_property();
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

    // 15. Identifiers (Assignments, Bit-Selects, Memory Writes, Task Calls)
    if (current.type == TokenType::TOKEN_IDENTIFIER || current.type == TokenType::TOKEN_THIS || current.type == TokenType::TOKEN_SUPER) {
        std::string ident = current.text;
        advance();
        while (match(TokenType::TOKEN_DOT)) {
            if (current.type == TokenType::TOKEN_NEW) {
                ident += ".new";
                advance();
            } else {
                ident += "." + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.'");
            }
        }

        if (current_parsing_module && current_parsing_module->genvars.count(ident)) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: genvar '" + ident + "' modified in procedural context per IEEE 1800-2023 section 27.4");
        }

        if (match(TokenType::TOKEN_LBRACKET)) {
            auto idx_expr = parse_expression();
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in index select");

            if (match(TokenType::TOKEN_EQUAL)) {
                uint64_t intra_delay = 0;
                bool has_intra_ev = false;
                EdgeType intra_edge = EdgeType::LEVEL;
                std::string intra_sig = "";
                parse_intra_timing(intra_delay, has_intra_ev, intra_edge, intra_sig);

                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assignment");
                auto node = std::make_unique<BlockingAssignNode>(ident, std::move(rhs), std::move(idx_expr), intra_delay);
                if (has_intra_ev) node->set_intra_event(intra_edge, std::move(intra_sig));
                return node;
            }
            std::string comp_op = "";
            if (match(TokenType::TOKEN_PLUS_EQUAL)) comp_op = "+";
            else if (match(TokenType::TOKEN_MINUS_EQUAL)) comp_op = "-";
            else if (match(TokenType::TOKEN_STAR_EQUAL)) comp_op = "*";
            else if (match(TokenType::TOKEN_AND_EQUAL)) comp_op = "&";
            else if (match(TokenType::TOKEN_OR_EQUAL)) comp_op = "|";
            else if (match(TokenType::TOKEN_XOR_EQUAL)) comp_op = "^";
            if (!comp_op.empty()) {
                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assignment");
                auto lhs_idx = std::make_unique<IndexSelectNode>(ident, idx_expr ? idx_expr->clone() : nullptr);
                auto op_expr = std::make_unique<BinaryOpNode>(comp_op, std::move(lhs_idx), std::move(rhs));
                return std::make_unique<BlockingAssignNode>(ident, std::move(op_expr), std::move(idx_expr));
            }
            if (match(TokenType::TOKEN_LE)) {
                uint64_t intra_delay = 0;
                bool has_intra_ev = false;
                EdgeType intra_edge = EdgeType::LEVEL;
                std::string intra_sig = "";
                parse_intra_timing(intra_delay, has_intra_ev, intra_edge, intra_sig);

                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after non-blocking assignment");
                auto node = std::make_unique<MemAssignNode>(ident, std::move(idx_expr), std::move(rhs), true, intra_delay);
                if (has_intra_ev) node->set_intra_event(intra_edge, std::move(intra_sig));
                return node;
            }
        }

        if (match(TokenType::TOKEN_PLUS_PLUS)) {
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after '++'");
            auto lhs_id = std::make_unique<IdentifierNode>(ident);
            auto one = std::make_unique<NumberNode>(1ULL, 32);
            auto add = std::make_unique<BinaryOpNode>("+", std::move(lhs_id), std::move(one));
            return std::make_unique<BlockingAssignNode>(ident, std::move(add));
        }

        if (match(TokenType::TOKEN_MINUS_MINUS)) {
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after '--'");
            auto lhs_id = std::make_unique<IdentifierNode>(ident);
            auto one = std::make_unique<NumberNode>(1ULL, 32);
            auto sub = std::make_unique<BinaryOpNode>("-", std::move(lhs_id), std::move(one));
            return std::make_unique<BlockingAssignNode>(ident, std::move(sub));
        }

        {
            std::string comp_op = "";
            if (match(TokenType::TOKEN_PLUS_EQUAL)) comp_op = "+";
            else if (match(TokenType::TOKEN_MINUS_EQUAL)) comp_op = "-";
            else if (match(TokenType::TOKEN_STAR_EQUAL)) comp_op = "*";
            else if (match(TokenType::TOKEN_AND_EQUAL)) comp_op = "&";
            else if (match(TokenType::TOKEN_OR_EQUAL)) comp_op = "|";
            else if (match(TokenType::TOKEN_XOR_EQUAL)) comp_op = "^";
            if (!comp_op.empty()) {
                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assignment");
                auto lhs_id = std::make_unique<IdentifierNode>(ident);
                auto op_expr = std::make_unique<BinaryOpNode>(comp_op, std::move(lhs_id), std::move(rhs));
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
            auto node = std::make_unique<BlockingAssignNode>(ident, std::move(rhs), nullptr, intra_delay);
            if (has_intra_ev) node->set_intra_event(intra_edge, std::move(intra_sig));
            return node;
        }

        if (match(TokenType::TOKEN_LE)) {
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
                expect(TokenType::TOKEN_LBRACE, "Expected '{' after 'with'");
                while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                    auto expr = parse_expression();
                    if (expr) {
                        inline_constraints.push_back(std::move(expr));
                    }
                    match(TokenType::TOKEN_SEMICOLON);
                }
                expect(TokenType::TOKEN_RBRACE, "Expected '}' closing inline constraint block");
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
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after statement");
        return std::make_unique<TaskCallNode>(ident, std::move(task_args), "", std::move(task_arg_names));
    }

    std::string loc = current.filename.empty() 
        ? ("line " + std::to_string(current.line)) 
        : (current.filename + ":" + std::to_string(current.line));
    throw std::runtime_error(loc + ": error: Unexpected statement beginning with '" + current.text + "'");
}

std::vector<std::unique_ptr<StatementNode>> Parser::parse_statement_or_block() {
    std::vector<std::unique_ptr<StatementNode>> stmts;
    if (match(TokenType::TOKEN_BEGIN)) {
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
        match(TokenType::TOKEN_SEMICOLON);
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

        std::string sig = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected signal name in sensitivity list");
        while (match(TokenType::TOKEN_DOT)) {
            sig += "." + current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in sensitivity list");
        }
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
