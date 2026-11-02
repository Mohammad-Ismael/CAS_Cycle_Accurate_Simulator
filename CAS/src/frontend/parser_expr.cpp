#include "../../include/frontend/parser.hpp"
#include <stdexcept>

std::unique_ptr<ExpressionNode> Parser::parse_primary() {
    if (match(TokenType::TOKEN_NEW)) {
        if (match(TokenType::TOKEN_LBRACKET)) {
            auto size_expr = parse_expression();
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' after size in 'new[]'");
            std::unique_ptr<ExpressionNode> copy_src = nullptr;
            if (match(TokenType::TOKEN_LPAREN)) {
                copy_src = parse_expression();
                expect(TokenType::TOKEN_RPAREN, "Expected ')' closing copy constructor source");
            }
            return std::make_unique<NewArrayNode>(std::move(size_expr), std::move(copy_src));
        }
        std::vector<std::unique_ptr<ExpressionNode>> args;
        std::vector<std::string> arg_names;
        if (match(TokenType::TOKEN_LPAREN)) {
            auto parsed = parse_call_argument_list();
            args = std::move(parsed.args);
            arg_names = std::move(parsed.arg_names);
        }
        return std::make_unique<ClassNewNode>("", std::move(args), std::move(arg_names));
    }

    if (match(TokenType::TOKEN_NULL)) {
        return std::make_unique<NullNode>();
    }

    if (match(TokenType::TOKEN_CAST)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after $cast");
        auto dest = parse_expression();
        expect(TokenType::TOKEN_COMMA, "Expected ',' in $cast(dest, src)");
        auto src = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $cast");
        return std::make_unique<SysCastCallNode>(std::move(dest), std::move(src));
    }

    if (current.type == TokenType::TOKEN_SYS_SIZE ||
        current.type == TokenType::TOKEN_SYS_DIMENSIONS ||
        current.type == TokenType::TOKEN_SYS_UNPACKED_DIMENSIONS ||
        current.type == TokenType::TOKEN_SYS_LEFT ||
        current.type == TokenType::TOKEN_SYS_RIGHT ||
        current.type == TokenType::TOKEN_SYS_LOW ||
        current.type == TokenType::TOKEN_SYS_HIGH ||
        current.type == TokenType::TOKEN_SYS_INCREMENT) {

        TokenType qtok = current.type;
        std::string fname = current.text;
        advance();
        ArrayQueryType qtype;
        if (qtok == TokenType::TOKEN_SYS_SIZE) qtype = ArrayQueryType::SIZE;
        else if (qtok == TokenType::TOKEN_SYS_DIMENSIONS) qtype = ArrayQueryType::DIMENSIONS;
        else if (qtok == TokenType::TOKEN_SYS_UNPACKED_DIMENSIONS) qtype = ArrayQueryType::UNPACKED_DIMENSIONS;
        else if (qtok == TokenType::TOKEN_SYS_LEFT) qtype = ArrayQueryType::LEFT;
        else if (qtok == TokenType::TOKEN_SYS_RIGHT) qtype = ArrayQueryType::RIGHT;
        else if (qtok == TokenType::TOKEN_SYS_LOW) qtype = ArrayQueryType::LOW;
        else if (qtok == TokenType::TOKEN_SYS_HIGH) qtype = ArrayQueryType::HIGH;
        else qtype = ArrayQueryType::INCREMENT;

        expect(TokenType::TOKEN_LPAREN, "Expected '(' after " + fname);
        auto arr_expr = parse_expression();
        std::unique_ptr<ExpressionNode> dim_expr = nullptr;
        if (match(TokenType::TOKEN_COMMA)) {
            dim_expr = parse_expression();
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing " + fname);
        return std::make_unique<ArrayQueryNode>(qtype, std::move(arr_expr), std::move(dim_expr));
    }

    if (current.type == TokenType::TOKEN_SYS_TYPENAME) {
        advance();
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after $typename");
        if (current.type == TokenType::TOKEN_RPAREN) {
            throw std::runtime_error("[CAS-DTINPCIL] $typename requires exactly one argument per IEEE 1800-2023 section 20.6");
        }

        std::string type_keyword = "";
        std::unique_ptr<ExpressionNode> arg_expr = nullptr;

        bool is_prim_keyword = (current.type == TokenType::TOKEN_LOGIC ||
                                current.type == TokenType::TOKEN_REG ||
                                current.type == TokenType::TOKEN_BIT ||
                                current.type == TokenType::TOKEN_INTEGER ||
                                current.type == TokenType::TOKEN_REAL ||
                                current.type == TokenType::TOKEN_CHANDLE ||
                                current.type == TokenType::TOKEN_VOID ||
                                (current.type == TokenType::TOKEN_IDENTIFIER &&
                                 (current.text == "int" || current.text == "byte" ||
                                  current.text == "shortint" || current.text == "longint" ||
                                  current.text == "time" || current.text == "string" ||
                                  current.text == "shortreal" || current.text == "realtime")));

        if (is_prim_keyword) {
            type_keyword = current.text;
            advance();
            if (match(TokenType::TOKEN_LBRACKET)) {
                auto msb = parse_expression();
                expect(TokenType::TOKEN_COLON, "Expected ':' in dimension");
                auto lsb = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' after dimension");
                int64_t m = 0, l = 0;
                if (auto nm = dynamic_cast<NumberNode*>(msb.get())) m = nm->aval;
                if (auto nl = dynamic_cast<NumberNode*>(lsb.get())) l = nl->aval;
                type_keyword += "[" + std::to_string(m) + ":" + std::to_string(l) + "]";
            }
        } else {
            arg_expr = parse_expression();
        }

        if (current.type == TokenType::TOKEN_COMMA) {
            throw std::runtime_error("[CAS-DTINPCIL] $typename requires exactly one argument per IEEE 1800-2023 section 20.6");
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $typename");

        return std::make_unique<TypeNameNode>(std::move(arg_expr), type_keyword);
    }

    if (current.type == TokenType::TOKEN_TYPE) {
        advance();
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'type'");
        if (current.type == TokenType::TOKEN_RPAREN) {
            throw std::runtime_error("[CAS-DTINPCIL] type() requires an argument per IEEE 1800-2023 section 6.23");
        }

        std::string type_keyword = "";
        std::unique_ptr<ExpressionNode> arg_expr = nullptr;

        bool is_prim_keyword = (current.type == TokenType::TOKEN_LOGIC ||
                                current.type == TokenType::TOKEN_REG ||
                                current.type == TokenType::TOKEN_BIT ||
                                current.type == TokenType::TOKEN_INTEGER ||
                                current.type == TokenType::TOKEN_REAL ||
                                current.type == TokenType::TOKEN_CHANDLE ||
                                current.type == TokenType::TOKEN_VOID ||
                                (current.type == TokenType::TOKEN_IDENTIFIER &&
                                 (current.text == "int" || current.text == "byte" ||
                                  current.text == "shortint" || current.text == "longint" ||
                                  current.text == "time" || current.text == "string" ||
                                  current.text == "shortreal" || current.text == "realtime")));

        if (is_prim_keyword) {
            type_keyword = current.text;
            advance();
            if (match(TokenType::TOKEN_LBRACKET)) {
                auto msb = parse_expression();
                expect(TokenType::TOKEN_COLON, "Expected ':' in dimension");
                auto lsb = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' after dimension");
                int64_t m = 0, l = 0;
                if (auto nm = dynamic_cast<NumberNode*>(msb.get())) m = nm->aval;
                if (auto nl = dynamic_cast<NumberNode*>(lsb.get())) l = nl->aval;
                type_keyword += "[" + std::to_string(m) + ":" + std::to_string(l) + "]";
            }
        } else {
            arg_expr = parse_expression();
        }

        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing type()");
        return std::make_unique<TypeOpNode>(std::move(arg_expr), type_keyword);
    }

    if (current.type == TokenType::TOKEN_SYS_COUNTONES ||
        current.type == TokenType::TOKEN_SYS_ONEHOT ||
        current.type == TokenType::TOKEN_SYS_ONEHOT0 ||
        current.type == TokenType::TOKEN_SYS_ISUNKNOWN) {
        TokenType tt = current.type;
        std::string fname = current.text;
        advance();
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after " + fname);
        if (current.type == TokenType::TOKEN_RPAREN) {
            throw std::runtime_error("[CAS-DTINPCIL] " + fname + " requires exactly one argument per IEEE 1800-2023 section 20.9");
        }
        auto operand = parse_expression();
        if (current.type == TokenType::TOKEN_COMMA) {
            throw std::runtime_error("[CAS-DTINPCIL] " + fname + " requires exactly one argument per IEEE 1800-2023 section 20.9");
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing " + fname);
        VectorIntrinsicKind kind;
        if (tt == TokenType::TOKEN_SYS_COUNTONES) kind = VectorIntrinsicKind::COUNTONES;
        else if (tt == TokenType::TOKEN_SYS_ONEHOT) kind = VectorIntrinsicKind::ONEHOT;
        else if (tt == TokenType::TOKEN_SYS_ONEHOT0) kind = VectorIntrinsicKind::ONEHOT0;
        else kind = VectorIntrinsicKind::ISUNKNOWN;
        return std::make_unique<VectorIntrinsicNode>(kind, std::move(operand));
    }

    if (current.type == TokenType::TOKEN_SYS_CLOG2) {
        advance();
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after $clog2");
        if (current.type == TokenType::TOKEN_RPAREN) {
            throw std::runtime_error("[CAS-DTINPCIL] $clog2 requires exactly one argument per IEEE 1800-2023 section 20.8.1");
        }
        auto operand = parse_expression();
        if (current.type == TokenType::TOKEN_COMMA) {
            throw std::runtime_error("[CAS-DTINPCIL] $clog2 requires exactly one argument per IEEE 1800-2023 section 20.8.1");
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $clog2");
        return std::make_unique<Clog2Node>(std::move(operand));
    }

    if (current.type == TokenType::TOKEN_ROSE || current.type == TokenType::TOKEN_FELL ||
        current.type == TokenType::TOKEN_STABLE || current.type == TokenType::TOKEN_PAST) {
        TokenType fn_tok = current.type;
        std::string fn_name = current.text;
        advance();
        SysSampledFuncType sft;
        if (fn_tok == TokenType::TOKEN_ROSE) sft = SysSampledFuncType::ROSE;
        else if (fn_tok == TokenType::TOKEN_FELL) sft = SysSampledFuncType::FELL;
        else if (fn_tok == TokenType::TOKEN_STABLE) sft = SysSampledFuncType::STABLE;
        else sft = SysSampledFuncType::PAST;

        expect(TokenType::TOKEN_LPAREN, "Expected '(' after " + fn_name);
        auto expr = parse_expression();
        std::unique_ptr<ExpressionNode> cycles_expr = nullptr;
        if (match(TokenType::TOKEN_COMMA)) {
            if (current.type != TokenType::TOKEN_COMMA && current.type != TokenType::TOKEN_RPAREN) {
                cycles_expr = parse_expression();
            }
            while (match(TokenType::TOKEN_COMMA)) {
                if (current.type != TokenType::TOKEN_RPAREN) {
                    if (match(TokenType::TOKEN_AT)) {
                        if (match(TokenType::TOKEN_LPAREN)) {
                            while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) advance();
                            match(TokenType::TOKEN_RPAREN);
                        } else {
                            if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
                        }
                    } else {
                        auto dummy = parse_expression();
                        (void)dummy;
                    }
                }
            }
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing " + fn_name);
        return std::make_unique<SysSampledFuncNode>(sft, fn_name, std::move(expr), std::move(cycles_expr));
    }

    if (match(TokenType::TOKEN_SUPER)) {
        if (match(TokenType::TOKEN_DOT)) {
            std::string sub_name;
            if (current.type == TokenType::TOKEN_NEW) {
                sub_name = "new";
                advance();
            } else {
                sub_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after 'super.'");
            }
            if (match(TokenType::TOKEN_LPAREN)) {
                auto parsed = parse_call_argument_list();
                return std::make_unique<MethodCallNode>("super", sub_name, std::move(parsed.args), nullptr, std::move(parsed.arg_names));
            } else {
                return std::make_unique<MemberAccessNode>("super", sub_name);
            }
        }
        return std::make_unique<IdentifierNode>("super");
    }

    if (match(TokenType::TOKEN_THIS)) {
        if (match(TokenType::TOKEN_DOT)) {
            std::string path = "this";
            std::string sub_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after 'this.'");
            while (match(TokenType::TOKEN_DOT)) {
                path += "." + sub_name;
                sub_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.'");
            }
            if (sub_name == "randomize") {
                if (match(TokenType::TOKEN_LPAREN)) {
                    if (current.type != TokenType::TOKEN_RPAREN) {
                        while (true) {
                            parse_expression();
                            if (match(TokenType::TOKEN_COMMA)) continue;
                            break;
                        }
                    }
                    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing randomize arguments");
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
                std::unique_ptr<ExpressionNode> rand_target = (path == "this") ?
                    std::unique_ptr<ExpressionNode>(std::make_unique<ThisNode>()) :
                    std::unique_ptr<ExpressionNode>(std::make_unique<MemberAccessNode>(path, sub_name));
                return std::make_unique<RandomizeCallNode>(std::move(rand_target), std::move(inline_constraints));
            }
            if (match(TokenType::TOKEN_LPAREN)) {
                auto parsed = parse_call_argument_list();
                return std::make_unique<MethodCallNode>(path, sub_name, std::move(parsed.args), nullptr, std::move(parsed.arg_names));
            } else {
                return std::make_unique<MemberAccessNode>(path, sub_name);
            }
        }
        return std::make_unique<ThisNode>();
    }

    if (current.type == TokenType::TOKEN_STRING) {
        std::string s = current.text;
        advance();
        return std::make_unique<StringNode>(s);
    }

    if (current.type == TokenType::TOKEN_SIGNED) {
        if (peek_token().type == TokenType::TOKEN_APOSTROPHE) {
            advance(); // consume 'signed'
            advance(); // consume '''
            expect(TokenType::TOKEN_LPAREN, "Expected '(' in signed cast expression");
            auto cast_expr = parse_expression();
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing signed cast expression");
            return std::make_unique<StaticCastExprNode>(CastKind::SIGNED, std::move(cast_expr));
        }
    }

    if (current.type == TokenType::TOKEN_UNSIGNED) {
        if (peek_token().type == TokenType::TOKEN_APOSTROPHE) {
            advance(); // consume 'unsigned'
            advance(); // consume '''
            expect(TokenType::TOKEN_LPAREN, "Expected '(' in unsigned cast expression");
            auto cast_expr = parse_expression();
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing unsigned cast expression");
            return std::make_unique<StaticCastExprNode>(CastKind::UNSIGNED, std::move(cast_expr));
        }
    }

    if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_BIT ||
        current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_INTEGER ||
        current.type == TokenType::TOKEN_REAL) {
        if (peek_token().type == TokenType::TOKEN_APOSTROPHE) {
            std::string tname = current.text;
            advance(); // consume type token
            advance(); // consume '''
            expect(TokenType::TOKEN_LPAREN, "Expected '(' in cast expression");
            auto cast_expr = parse_expression();
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing cast expression");
            return std::make_unique<StaticCastExprNode>(CastKind::TYPE, 0, tname, std::move(cast_expr));
        }
    }

    if (current.type == TokenType::TOKEN_NUMBER) {
        if (current.is_real) {
            double r = current.real_val;
            advance();
            return std::make_unique<NumberNode>(r);
        }
        uint64_t a = current.num_val;
        uint64_t b = current.bval;
        uint32_t w = current.width;
        advance();
        if (match(TokenType::TOKEN_APOSTROPHE)) {
            expect(TokenType::TOKEN_LPAREN, "Expected '(' in size cast expression");
            if (a == 0) {
                throw std::runtime_error("[CAS-DTINPCIL] Size cast bit width must be positive integer (got 0) per IEEE 1800-2023 section 6.24.1");
            }
            auto cast_expr = parse_expression();
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing size cast expression");
            return std::make_unique<StaticCastExprNode>(static_cast<uint32_t>(a), std::move(cast_expr));
        }
        return std::make_unique<NumberNode>(a, b, w);
    }

    if (current.type == TokenType::TOKEN_IDENTIFIER) {
        if (is_class(current.text)) {
            std::string class_type = parse_class_type_and_specialize();
            if (match(TokenType::TOKEN_COLON_COLON)) {
                std::string member = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected member name after '::'");
                if (match(TokenType::TOKEN_LPAREN)) {
                    auto parsed = parse_call_argument_list();
                    return std::make_unique<StaticMethodCallNode>(class_type, member, std::move(parsed.args), std::move(parsed.arg_names));
                }
                return std::make_unique<StaticMemberAccessNode>(class_type, member);
            }
        }

        std::string name = current.text;
        advance();

        if (match(TokenType::TOKEN_COLON_COLON)) {
            std::string member = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected member name after '::'");
            if (match(TokenType::TOKEN_LPAREN)) {
                auto parsed = parse_call_argument_list();
                const LetDeclNode* let_decl = find_visible_let(name + "::" + member);
                if (let_decl) {
                    return expand_let(let_decl, std::move(parsed.args), parsed.arg_names);
                }
                return std::make_unique<StaticMethodCallNode>(name, member, std::move(parsed.args), std::move(parsed.arg_names));
            }
            const LetDeclNode* let_decl = find_visible_let(name + "::" + member);
            if (let_decl && let_decl->ports.empty()) {
                return expand_let(let_decl, {}, {});
            }
            if (match(TokenType::TOKEN_APOSTROPHE)) {
                expect(TokenType::TOKEN_LPAREN, "Expected '(' in cast expression");
                auto cast_expr = parse_expression();
                expect(TokenType::TOKEN_RPAREN, "Expected ')' closing cast expression");
                std::string full_tname = name + "::" + member;
                auto pit = global_packages.find(name);
                bool valid = false;
                if (pit != global_packages.end()) {
                    if (pit->second->enum_typedefs.count(member) ||
                        pit->second->struct_typedefs.count(member) ||
                        pit->second->simple_typedefs.count(member) ||
                        pit->second->class_map.count(member)) {
                        valid = true;
                    }
                }
                if (!valid) {
                    throw std::runtime_error("[CAS-DTINPCIL] Undefined or unresolved type identifier in static cast per IEEE 1800-2023 section 6.24.1: '" + full_tname + "'");
                }
                return std::make_unique<StaticCastExprNode>(CastKind::TYPE, 0, full_tname, std::move(cast_expr));
            }
            return std::make_unique<StaticMemberAccessNode>(name, member);
        }

        if (name == "randomize") {
            if (match(TokenType::TOKEN_LPAREN)) {
                if (current.type != TokenType::TOKEN_RPAREN) {
                    while (true) {
                        parse_expression();
                        if (match(TokenType::TOKEN_COMMA)) continue;
                        break;
                    }
                }
                expect(TokenType::TOKEN_RPAREN, "Expected ')' closing randomize arguments");
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
            return std::make_unique<RandomizeCallNode>(std::make_unique<ThisNode>(), std::move(inline_constraints));
        }

        // Cast expression: identifier followed by ''' then '('
        if (match(TokenType::TOKEN_APOSTROPHE)) {
            expect(TokenType::TOKEN_LPAREN, "Expected '(' in cast expression");
            auto cast_expr = parse_expression();
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing cast expression");
            if (name == "signed") {
                return std::make_unique<StaticCastExprNode>(CastKind::SIGNED, std::move(cast_expr));
            }
            if (name == "unsigned") {
                return std::make_unique<StaticCastExprNode>(CastKind::UNSIGNED, std::move(cast_expr));
            }
            uint32_t csize = 0;
            if (is_constant_size_param(name, csize)) {
                if (csize == 0) {
                    throw std::runtime_error("[CAS-DTINPCIL] Size cast bit width must be positive integer (got 0) per IEEE 1800-2023 section 6.24.1");
                }
                return std::make_unique<StaticCastExprNode>(csize, std::move(cast_expr));
            }
            if (!is_type_identifier(name)) {
                throw std::runtime_error("[CAS-DTINPCIL] Undefined or unresolved type identifier in static cast per IEEE 1800-2023 section 6.24.1: '" + name + "'");
            }
            return std::make_unique<StaticCastExprNode>(CastKind::TYPE, 0, name, std::move(cast_expr));
        }

        // Method call or Member access: identifier followed by '.'
        if (match(TokenType::TOKEN_DOT)) {
            std::string sub_name;
            if (current.type == TokenType::TOKEN_IDENTIFIER ||
                current.type == TokenType::TOKEN_GATE_AND ||
                current.type == TokenType::TOKEN_GATE_OR ||
                current.type == TokenType::TOKEN_GATE_XOR ||
                current.type == TokenType::TOKEN_OR) {
                sub_name = current.text;
                advance();
            } else {
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.'");
            }
            if (sub_name == "randomize") {
                if (match(TokenType::TOKEN_LPAREN)) {
                    if (current.type != TokenType::TOKEN_RPAREN) {
                        while (true) {
                            parse_expression();
                            if (match(TokenType::TOKEN_COMMA)) continue;
                            break;
                        }
                    }
                    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing randomize arguments");
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
                return std::make_unique<RandomizeCallNode>(std::make_unique<IdentifierNode>(name), std::move(inline_constraints));
            }
            if (match(TokenType::TOKEN_LPAREN)) {
                auto parsed = parse_call_argument_list();
                std::unique_ptr<ExpressionNode> with_expr = nullptr;
                if (match(TokenType::TOKEN_WITH)) {
                    expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'with'");
                    with_expr = parse_expression();
                    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing with expression");
                }
                return std::make_unique<MethodCallNode>(name, sub_name, std::move(parsed.args), std::move(with_expr), std::move(parsed.arg_names));
            } else if (match(TokenType::TOKEN_WITH)) {
                expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'with'");
                auto with_expr = parse_expression();
                expect(TokenType::TOKEN_RPAREN, "Expected ')' closing with expression");
                return std::make_unique<MethodCallNode>(name, sub_name, std::vector<std::unique_ptr<ExpressionNode>>(), std::move(with_expr));
            } else if (match(TokenType::TOKEN_LBRACKET)) {
                auto msb = parse_expression();
                if (match(TokenType::TOKEN_COLON)) {
                    auto lsb = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing part-select range");
                    return std::make_unique<IndexSelectNode>(name + "." + sub_name, std::move(msb), std::move(lsb));
                }
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing bit-select index");
                if (match(TokenType::TOKEN_DOT)) {
                    std::string idx_str = "0";
                    if (auto num = dynamic_cast<NumberNode*>(msb.get())) idx_str = std::to_string(num->aval);
                    std::string path = name + "." + sub_name + "[" + idx_str + "]";
                    std::string mem = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.'");
                    while (match(TokenType::TOKEN_DOT)) {
                        path += "." + mem;
                        mem = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.'");
                    }
                    return std::make_unique<MemberAccessNode>(path, mem);
                }
                return std::make_unique<IndexSelectNode>(name + "." + sub_name, std::move(msb));
            } else {
                std::string path = name;
                std::string mem = sub_name;
                while (match(TokenType::TOKEN_DOT)) {
                    path += "." + mem;
                    mem = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.'");
                }
                if (match(TokenType::TOKEN_LPAREN)) {
                    auto parsed = parse_call_argument_list();
                    return std::make_unique<MethodCallNode>(path, mem, std::move(parsed.args), nullptr, std::move(parsed.arg_names));
                }
                return std::make_unique<MemberAccessNode>(path, mem);
            }
        }

        // Function call: identifier followed immediately by '('
        if (match(TokenType::TOKEN_LPAREN)) {
            auto parsed = parse_call_argument_list();
            const LetDeclNode* let_decl = find_visible_let(name);
            if (let_decl) {
                return expand_let(let_decl, std::move(parsed.args), parsed.arg_names);
            }
            return std::make_unique<FunctionCallNode>(name, std::move(parsed.args), std::move(parsed.arg_names));
        }

        const LetDeclNode* let_decl_noargs = find_visible_let(name);
        if (let_decl_noargs && let_decl_noargs->ports.empty()) {
            return expand_let(let_decl_noargs, {}, {});
        }

        if (match(TokenType::TOKEN_LBRACKET)) {
            auto msb = parse_expression();
            if (match(TokenType::TOKEN_COLON)) {
                auto lsb = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing part-select range");
                return std::make_unique<IndexSelectNode>(name, std::move(msb), std::move(lsb));
            }
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing bit-select index");
            if (match(TokenType::TOKEN_DOT)) {
                std::string idx_str = "0";
                if (auto num = dynamic_cast<NumberNode*>(msb.get())) idx_str = std::to_string(num->aval);
                std::string path = name + "[" + idx_str + "]";
                std::string mem = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.'");
                while (match(TokenType::TOKEN_DOT)) {
                    path += "." + mem;
                    mem = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.'");
                }
                if (match(TokenType::TOKEN_LPAREN)) {
                    auto parsed = parse_call_argument_list();
                    return std::make_unique<MethodCallNode>(path, mem, std::move(parsed.args), nullptr, std::move(parsed.arg_names));
                }
                return std::make_unique<MemberAccessNode>(path, mem);
            }
            auto idx_node = std::make_unique<IndexSelectNode>(name, std::move(msb));
            if (match(TokenType::TOKEN_LBRACKET)) {
                idx_node->has_bit_select = true;
                idx_node->bit_msb_expr = parse_expression();
                if (match(TokenType::TOKEN_COLON)) {
                    idx_node->bit_lsb_expr = parse_expression();
                }
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing bit-select range");
            }
            return idx_node;
        }

        return std::make_unique<IdentifierNode>(name);
    }

    if (match(TokenType::TOKEN_LPAREN)) {
        auto expr = parse_sequence_expression();
        if (match(TokenType::TOKEN_COMMA)) {
            std::vector<SequenceMatchAction> assigns;
            do {
                if (current.type == TokenType::TOKEN_DISPLAY || current.type == TokenType::TOKEN_FINISH || current.type == TokenType::TOKEN_STOP) {
                    std::string task_name = current.text;
                    advance();
                    std::vector<std::unique_ptr<ExpressionNode>> args;
                    if (match(TokenType::TOKEN_LPAREN)) {
                        if (current.type != TokenType::TOKEN_RPAREN) {
                            while (true) {
                                args.push_back(parse_expression());
                                if (match(TokenType::TOKEN_COMMA)) continue;
                                break;
                            }
                        }
                        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing system task arguments");
                    }
                    assigns.push_back(SequenceMatchAction(true, task_name, std::make_unique<FunctionCallNode>(task_name, std::move(args))));
                } else if (current.type == TokenType::TOKEN_IDENTIFIER) {
                    std::string name = current.text;
                    advance();
                    if (match(TokenType::TOKEN_COLON_COLON)) {
                        std::string member = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected member identifier after '::'");
                        name = name + "::" + member;
                    }
                    if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                        auto assign_expr = parse_expression();
                        assigns.push_back(SequenceMatchAction(false, name, std::move(assign_expr)));
                    } else if (match(TokenType::TOKEN_LPAREN)) {
                        std::vector<std::unique_ptr<ExpressionNode>> args;
                        if (current.type != TokenType::TOKEN_RPAREN) {
                            while (true) {
                                args.push_back(parse_expression());
                                if (match(TokenType::TOKEN_COMMA)) continue;
                                break;
                            }
                        }
                        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing subroutine arguments");
                        assigns.push_back(SequenceMatchAction(true, name, std::make_unique<FunctionCallNode>(name, std::move(args))));
                    } else {
                        // Subroutine call without parentheses (e.g. `foo`)
                        assigns.push_back(SequenceMatchAction(true, name, std::make_unique<FunctionCallNode>(name, std::vector<std::unique_ptr<ExpressionNode>>{})));
                    }
                } else {
                    throw std::runtime_error("Expected variable assignment or subroutine call in sequence match item");
                }
            } while (match(TokenType::TOKEN_COMMA));
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing parenthesized match expression");
            return std::make_unique<SequenceMatchItemNode>(std::move(expr), std::move(assigns));
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing parenthesized expression");
        if (match(TokenType::TOKEN_APOSTROPHE)) {
            expect(TokenType::TOKEN_LPAREN, "Expected '(' in size cast expression");
            int64_t sz = 0;
            if (auto num = dynamic_cast<NumberNode*>(expr.get())) {
                if (num->width <= 32) {
                    sz = static_cast<int64_t>(static_cast<int32_t>(num->aval));
                } else {
                    sz = static_cast<int64_t>(num->aval);
                }
            } else if (auto un = dynamic_cast<UnaryOpNode*>(expr.get())) {
                if (un->op == "-") {
                    if (auto un_num = dynamic_cast<NumberNode*>(un->operand.get())) {
                        sz = -static_cast<int64_t>(un_num->aval);
                    } else {
                        sz = -1;
                    }
                }
            } else {
                try {
                    sz = static_cast<int64_t>(eval_const_expr(expr.get()));
                } catch (...) {
                    sz = 0;
                }
            }
            if (sz <= 0) {
                throw std::runtime_error("[CAS-DTINPCIL] Size cast bit width must be positive integer (got " + std::to_string(sz) + ") per IEEE 1800-2023 section 6.24.1");
            }
            auto cast_expr = parse_expression();
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing size cast expression");
            return std::make_unique<StaticCastExprNode>(static_cast<uint32_t>(sz), std::move(cast_expr));
        }
        return expr;
    }

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
            return std::make_unique<StreamingConcatNode>(dir, std::move(slice_sz), std::move(items));
        }

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
            if (current.is_real) {
                double r = -current.real_val;
                advance();
                return std::make_unique<NumberNode>(r);
            }
            uint64_t a = current.num_val;
            uint64_t b = current.bval;
            uint32_t w = current.width;
            advance();
            if (match(TokenType::TOKEN_APOSTROPHE)) {
                expect(TokenType::TOKEN_LPAREN, "Expected '(' in size cast expression");
                throw std::runtime_error("[CAS-DTINPCIL] Size cast bit width must be positive integer (got -" + std::to_string(a) + ") per IEEE 1800-2023 section 6.24.1");
            }
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
           current.type == TokenType::TOKEN_GT || current.type == TokenType::TOKEN_GE ||
           current.type == TokenType::TOKEN_INSIDE) {
        if (current.type == TokenType::TOKEN_INSIDE) {
            advance();
            if (!match(TokenType::TOKEN_LBRACE)) {
                throw std::runtime_error("[CAS-DTINPCIL] Expected '{' opening inside open range list per IEEE 1800-2023 section 11.4.13");
            }
            std::vector<InsideRange> ranges;
            while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                if (match(TokenType::TOKEN_LBRACKET)) {
                    auto lo = parse_expression();
                    if (!match(TokenType::TOKEN_COLON)) {
                        throw std::runtime_error("[CAS-DTINPCIL] Inside operator bounded range item lacks valid colon syntax per IEEE 1800-2023 section 11.4.13");
                    }
                    auto hi = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in inside range");
                    ranges.emplace_back(std::move(lo), std::move(hi));
                } else {
                    auto single = parse_expression();
                    ranges.emplace_back(std::move(single), nullptr);
                }
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_RBRACE, "Expected '}' closing inside range list");
            if (ranges.empty()) {
                throw std::runtime_error("[CAS-DTINPCIL] Inside operator open range list cannot be empty per IEEE 1800-2023 section 11.4.13");
            }
            lhs = std::make_unique<InsideNode>(std::move(lhs), std::move(ranges));
        } else {
            std::string op = current.text;
            advance();
            auto rhs = parse_shift();
            if (dynamic_cast<TypeOpNode*>(lhs.get()) || dynamic_cast<TypeOpNode*>(rhs.get())) {
                throw std::runtime_error("[CAS-DTINPCIL] Relational operator '" + op + "' is invalid between type() references per IEEE 1800-2023 section 6.23");
            }
            lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
        }
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

        bool l_is_type = (dynamic_cast<TypeOpNode*>(lhs.get()) != nullptr);
        bool r_is_type = (dynamic_cast<TypeOpNode*>(rhs.get()) != nullptr);
        if (l_is_type || r_is_type) {
            if (!l_is_type || !r_is_type) {
                throw std::runtime_error("[CAS-DTINPCIL] Both operands of type comparison must be type() references per IEEE 1800-2023 section 6.23");
            }
            auto l_top = dynamic_cast<TypeOpNode*>(lhs.get());
            auto r_top = dynamic_cast<TypeOpNode*>(rhs.get());
            std::string l_t = resolve_type_from_type_op(l_top);
            std::string r_t = resolve_type_from_type_op(r_top);
            if (!l_t.empty() && !r_t.empty()) {
                bool match = (l_t == r_t);
                bool res = (op == "==" || op == "===") ? match : !match;
                lhs = std::make_unique<NumberNode>(res ? 1 : 0, 1);
                continue;
            }
        }
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

std::unique_ptr<ExpressionNode> Parser::parse_sequence_element() {
    auto expr = parse_expression();
    if (current.type == TokenType::TOKEN_REP_CONSEC ||
        current.type == TokenType::TOKEN_REP_NON_CONSEC ||
        current.type == TokenType::TOKEN_REP_GOTO) {
        TokenType rep_tok = current.type;
        advance();
        RepetitionType rtype = RepetitionType::CONSECUTIVE;
        if (rep_tok == TokenType::TOKEN_REP_NON_CONSEC) rtype = RepetitionType::NON_CONSECUTIVE;
        else if (rep_tok == TokenType::TOKEN_REP_GOTO) rtype = RepetitionType::GOTO;

        uint32_t min_count = 1;
        uint32_t max_count = 1;
        if (current.type == TokenType::TOKEN_RBRACKET) {
            min_count = 0;
            max_count = UINT32_MAX;
            advance();
        } else if (match(TokenType::TOKEN_PLUS)) {
            min_count = 1;
            max_count = UINT32_MAX;
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' after '+' repetition");
        } else {
            auto cnt_expr = parse_expression();
            if (auto num = dynamic_cast<NumberNode*>(cnt_expr.get())) {
                min_count = static_cast<uint32_t>(num->aval);
                max_count = min_count;
            }
            if (match(TokenType::TOKEN_COLON)) {
                if (match(TokenType::TOKEN_DOLLAR)) {
                    max_count = UINT32_MAX;
                } else {
                    auto max_expr = parse_expression();
                    if (auto num2 = dynamic_cast<NumberNode*>(max_expr.get())) {
                        max_count = static_cast<uint32_t>(num2->aval);
                    }
                }
            }
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing repetition operator");
        }
        return std::make_unique<SequenceRepetitionNode>(std::move(expr), rtype, min_count, max_count);
    }
    return expr;
}

std::unique_ptr<ExpressionNode> Parser::parse_sequence_expression() {
    if (current.type == TokenType::TOKEN_CYCLE_DELAY ||
        (current.type == TokenType::TOKEN_HASH && peek_token().type == TokenType::TOKEN_HASH)) {
        std::vector<SequenceConcatItem> items;
        while (current.type == TokenType::TOKEN_CYCLE_DELAY ||
               (current.type == TokenType::TOKEN_HASH && peek_token().type == TokenType::TOKEN_HASH)) {
            uint32_t delay = 1;
            if (current.type == TokenType::TOKEN_CYCLE_DELAY) {
                advance();
                if (current.type == TokenType::TOKEN_NUMBER) {
                    delay = static_cast<uint32_t>(current.num_val);
                    advance();
                }
            } else {
                advance(); advance();
                if (current.type == TokenType::TOKEN_NUMBER) {
                    delay = static_cast<uint32_t>(current.num_val);
                    advance();
                }
            }
            auto next_elem = parse_sequence_element();
            items.push_back({delay, std::move(next_elem)});
        }
        return std::make_unique<SequenceConcatNode>(std::move(items));
    }

    auto first = parse_sequence_element();

    if (current.type != TokenType::TOKEN_CYCLE_DELAY &&
        !(current.type == TokenType::TOKEN_HASH && peek_token().type == TokenType::TOKEN_HASH)) {
        return first;
    }

    std::vector<SequenceConcatItem> items;
    items.push_back({0, std::move(first)});

    while (current.type == TokenType::TOKEN_CYCLE_DELAY ||
           (current.type == TokenType::TOKEN_HASH && peek_token().type == TokenType::TOKEN_HASH)) {
        uint32_t delay = 1;
        if (current.type == TokenType::TOKEN_CYCLE_DELAY) {
            advance();
            if (current.type == TokenType::TOKEN_NUMBER) {
                delay = static_cast<uint32_t>(current.num_val);
                advance();
            }
        } else {
            advance(); advance();
            if (current.type == TokenType::TOKEN_NUMBER) {
                delay = static_cast<uint32_t>(current.num_val);
                advance();
            }
        }
        auto next_elem = parse_sequence_element();
        items.push_back({delay, std::move(next_elem)});
    }
    return std::make_unique<SequenceConcatNode>(std::move(items));
}

const LetDeclNode* Parser::find_visible_let(const std::string& name) const {
    auto colon = name.find("::");
    if (colon != std::string::npos) {
        std::string pkg_name = name.substr(0, colon);
        std::string item_name = name.substr(colon + 2);
        auto pit = global_packages.find(pkg_name);
        if (pit != global_packages.end()) {
            auto lit = pit->second->let_decl_map.find(item_name);
            if (lit != pit->second->let_decl_map.end()) return lit->second.get();
        }
        auto git = global_let_decls.find(name);
        if (git != global_let_decls.end()) return git->second.get();
        return nullptr;
    }

    if (current_parsing_module) {
        auto it = current_parsing_module->let_decl_map.find(name);
        if (it != current_parsing_module->let_decl_map.end()) return it->second.get();

        auto eit = current_parsing_module->explicit_imports.find(name);
        if (eit != current_parsing_module->explicit_imports.end()) {
            auto pit = global_packages.find(eit->second.first);
            if (pit != global_packages.end()) {
                auto lit = pit->second->let_decl_map.find(eit->second.second);
                if (lit != pit->second->let_decl_map.end()) return lit->second.get();
            }
        }

        for (const auto& pkg_name : current_parsing_module->wildcard_imports) {
            auto pit = global_packages.find(pkg_name);
            if (pit != global_packages.end()) {
                auto lit = pit->second->let_decl_map.find(name);
                if (lit != pit->second->let_decl_map.end()) return lit->second.get();
            }
        }
    }

    if (current_parsing_package) {
        auto it = current_parsing_package->let_decl_map.find(name);
        if (it != current_parsing_package->let_decl_map.end()) return it->second.get();

        for (const auto& pkg_name : current_parsing_package->wildcard_imports) {
            auto pit = global_packages.find(pkg_name);
            if (pit != global_packages.end()) {
                auto lit = pit->second->let_decl_map.find(name);
                if (lit != pit->second->let_decl_map.end()) return lit->second.get();
            }
        }
    }

    for (const auto& pkg_name : global_wildcard_imports) {
        auto pit = global_packages.find(pkg_name);
        if (pit != global_packages.end()) {
            auto lit = pit->second->let_decl_map.find(name);
            if (lit != pit->second->let_decl_map.end()) return lit->second.get();
        }
    }

    auto git = global_let_decls.find(name);
    if (git != global_let_decls.end()) return git->second.get();

    return nullptr;
}

std::unique_ptr<ExpressionNode> Parser::expand_let(
    const LetDeclNode* let_decl,
    std::vector<std::unique_ptr<ExpressionNode>> actual_args,
    const std::vector<std::string>& arg_names)
{
    if (!let_decl) return nullptr;

    if (active_let_expansions.count(let_decl->name)) {
        throw std::runtime_error("[CAS-DTINPCIL] Recursive let declaration or invocation is illegal per IEEE 1800-2023 section 11.12: '" + let_decl->name + "'");
    }
    active_let_expansions.insert(let_decl->name);
    struct ExpansionGuard {
        std::unordered_set<std::string>& s;
        std::string k;
        ~ExpansionGuard() { s.erase(k); }
    } guard{active_let_expansions, let_decl->name};

    std::unordered_map<std::string, std::unique_ptr<ExpressionNode>> bound_args;
    bool has_named = false;
    for (const auto& nm : arg_names) {
        if (!nm.empty()) { has_named = true; break; }
    }

    if (!has_named) {
        if (actual_args.size() > let_decl->ports.size()) {
            throw std::runtime_error("[CAS-DTINPCIL] Too many arguments passed to let '" + let_decl->name + "' per IEEE 1800-2023 section 11.12");
        }
        for (size_t i = 0; i < let_decl->ports.size(); ++i) {
            const auto& port = let_decl->ports[i];
            if (i < actual_args.size() && actual_args[i] != nullptr) {
                bound_args[port.name] = std::move(actual_args[i]);
            } else if (port.default_value != nullptr) {
                bound_args[port.name] = port.default_value->clone();
            } else {
                throw std::runtime_error("[CAS-DTINPCIL] Missing required argument '" + port.name + "' in call to let '" + let_decl->name + "' per IEEE 1800-2023 section 11.12");
            }
        }
    } else {
        std::unordered_map<std::string, std::unique_ptr<ExpressionNode>> passed_named;
        for (size_t i = 0; i < actual_args.size(); ++i) {
            std::string nm = (i < arg_names.size()) ? arg_names[i] : "";
            if (!nm.empty() && nm[0] == '.') nm = nm.substr(1);
            if (nm.empty() && i < let_decl->ports.size()) {
                nm = let_decl->ports[i].name;
            }
            passed_named[nm] = std::move(actual_args[i]);
        }
        for (const auto& port : let_decl->ports) {
            auto pit = passed_named.find(port.name);
            if (pit != passed_named.end() && pit->second != nullptr) {
                bound_args[port.name] = std::move(pit->second);
            } else if (port.default_value != nullptr) {
                bound_args[port.name] = port.default_value->clone();
            } else {
                throw std::runtime_error("[CAS-DTINPCIL] Missing required argument '" + port.name + "' in call to let '" + let_decl->name + "' per IEEE 1800-2023 section 11.12");
            }
        }
    }

    // Apply type casting for typed formals
    for (const auto& port : let_decl->ports) {
        if (port.is_typed) {
            auto& arg_expr = bound_args[port.name];
            arg_expr = std::make_unique<CastNode>(port.data_type, std::move(arg_expr), port.width, port.is_signed);
        }
    }

    // Build raw pointer map for substitute
    std::unordered_map<std::string, const ExpressionNode*> subst_map;
    for (const auto& kv : bound_args) {
        subst_map[kv.first] = kv.second.get();
    }

    return substitute_let_in_expr(let_decl->body_expr.get(), subst_map);
}

std::unique_ptr<ExpressionNode> Parser::substitute_let_in_expr(
    const ExpressionNode* expr,
    const std::unordered_map<std::string, const ExpressionNode*>& subst)
{
    if (!expr) return nullptr;

    if (auto id = dynamic_cast<const IdentifierNode*>(expr)) {
        auto it = subst.find(id->name);
        if (it != subst.end() && it->second != nullptr) {
            return it->second->clone();
        }
        return id->clone();
    }

    if (auto bo = dynamic_cast<const BinaryOpNode*>(expr)) {
        return std::make_unique<BinaryOpNode>(
            bo->op,
            substitute_let_in_expr(bo->lhs.get(), subst),
            substitute_let_in_expr(bo->rhs.get(), subst)
        );
    }

    if (auto uo = dynamic_cast<const UnaryOpNode*>(expr)) {
        return std::make_unique<UnaryOpNode>(
            uo->op,
            substitute_let_in_expr(uo->operand.get(), subst)
        );
    }

    if (auto to = dynamic_cast<const TernaryOpNode*>(expr)) {
        return std::make_unique<TernaryOpNode>(
            substitute_let_in_expr(to->condition.get(), subst),
            substitute_let_in_expr(to->true_expr.get(), subst),
            substitute_let_in_expr(to->false_expr.get(), subst)
        );
    }

    if (auto co = dynamic_cast<const ConcatNode*>(expr)) {
        std::vector<std::unique_ptr<ExpressionNode>> elems;
        for (const auto& el : co->elements) {
            elems.push_back(substitute_let_in_expr(el.get(), subst));
        }
        return std::make_unique<ConcatNode>(std::move(elems));
    }

    if (auto sco = dynamic_cast<const StreamingConcatNode*>(expr)) {
        std::vector<std::unique_ptr<ExpressionNode>> items;
        for (const auto& item : sco->stream_items) {
            items.push_back(substitute_let_in_expr(item.get(), subst));
        }
        return std::make_unique<StreamingConcatNode>(
            sco->direction,
            substitute_let_in_expr(sco->slice_size.get(), subst),
            std::move(items)
        );
    }

    if (auto ro = dynamic_cast<const ReplicateNode*>(expr)) {
        return std::make_unique<ReplicateNode>(
            substitute_let_in_expr(ro->count_expr.get(), subst),
            substitute_let_in_expr(ro->target_expr.get(), subst)
        );
    }

    if (auto iso = dynamic_cast<const IndexSelectNode*>(expr)) {
        std::string base_name = iso->name;
        auto it = subst.find(base_name);
        if (it != subst.end() && it->second != nullptr) {
            if (auto aid = dynamic_cast<const IdentifierNode*>(it->second)) {
                base_name = aid->name;
            } else if (auto acid = dynamic_cast<const CastNode*>(it->second)) {
                if (auto cid = dynamic_cast<const IdentifierNode*>(acid->expr.get())) {
                    base_name = cid->name;
                }
            } else if (auto ascid = dynamic_cast<const StaticCastExprNode*>(it->second)) {
                if (auto cid = dynamic_cast<const IdentifierNode*>(ascid->expr.get())) {
                    base_name = cid->name;
                }
            }
        }
        std::unique_ptr<IndexSelectNode> res;
        if (!iso->is_part_select) {
            res = std::make_unique<IndexSelectNode>(
                base_name,
                substitute_let_in_expr(iso->msb_expr.get(), subst)
            );
        } else {
            res = std::make_unique<IndexSelectNode>(
                base_name,
                substitute_let_in_expr(iso->msb_expr.get(), subst),
                substitute_let_in_expr(iso->lsb_expr.get(), subst)
            );
        }
        res->has_bit_select = iso->has_bit_select;
        res->bit_msb_expr = substitute_let_in_expr(iso->bit_msb_expr.get(), subst);
        res->bit_lsb_expr = substitute_let_in_expr(iso->bit_lsb_expr.get(), subst);
        return res;
    }

    if (auto co = dynamic_cast<const CastNode*>(expr)) {
        return std::make_unique<CastNode>(
            co->target_type,
            substitute_let_in_expr(co->expr.get(), subst),
            co->width,
            co->is_signed
        );
    }

    if (auto sco = dynamic_cast<const StaticCastExprNode*>(expr)) {
        return std::make_unique<StaticCastExprNode>(
            sco->kind,
            sco->target_width,
            sco->target_type_name,
            substitute_let_in_expr(sco->expr.get(), subst)
        );
    }

    if (auto fco = dynamic_cast<const FunctionCallNode*>(expr)) {
        std::vector<std::unique_ptr<ExpressionNode>> c_args;
        for (const auto& a : fco->args) {
            c_args.push_back(substitute_let_in_expr(a.get(), subst));
        }
        const LetDeclNode* inner_let = find_visible_let(fco->func_name);
        if (inner_let) {
            return expand_let(inner_let, std::move(c_args), fco->arg_names);
        }
        return std::make_unique<FunctionCallNode>(fco->func_name, std::move(c_args), fco->arg_names);
    }

    if (auto mco = dynamic_cast<const MethodCallNode*>(expr)) {
        std::string vname = mco->var_name;
        auto it = subst.find(vname);
        if (it != subst.end() && it->second != nullptr) {
            if (auto aid = dynamic_cast<const IdentifierNode*>(it->second)) {
                vname = aid->name;
            }
        }
        std::vector<std::unique_ptr<ExpressionNode>> c_args;
        for (const auto& a : mco->args) {
            c_args.push_back(substitute_let_in_expr(a.get(), subst));
        }
        return std::make_unique<MethodCallNode>(
            vname,
            mco->method_name,
            std::move(c_args),
            substitute_let_in_expr(mco->with_expr.get(), subst),
            mco->arg_names
        );
    }

    if (auto mao = dynamic_cast<const MemberAccessNode*>(expr)) {
        std::string vname = mao->var_name;
        auto it = subst.find(vname);
        if (it != subst.end() && it->second != nullptr) {
            if (auto aid = dynamic_cast<const IdentifierNode*>(it->second)) {
                vname = aid->name;
            }
        }
        return std::make_unique<MemberAccessNode>(vname, mao->member_name);
    }

    return expr->clone();
}
