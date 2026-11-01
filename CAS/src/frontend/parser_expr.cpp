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
            std::string sub_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after 'this.'");
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
                return std::make_unique<RandomizeCallNode>(std::make_unique<ThisNode>(), std::move(inline_constraints));
            }
            if (match(TokenType::TOKEN_LPAREN)) {
                auto parsed = parse_call_argument_list();
                return std::make_unique<MethodCallNode>("this", sub_name, std::move(parsed.args), nullptr, std::move(parsed.arg_names));
            } else {
                return std::make_unique<MemberAccessNode>("this", sub_name);
            }
        }
        return std::make_unique<ThisNode>();
    }

    if (current.type == TokenType::TOKEN_STRING) {
        std::string s = current.text;
        advance();
        return std::make_unique<StringNode>(s);
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
                return std::make_unique<StaticMethodCallNode>(name, member, std::move(parsed.args), std::move(parsed.arg_names));
            }
            if (match(TokenType::TOKEN_APOSTROPHE)) {
                expect(TokenType::TOKEN_LPAREN, "Expected '(' in cast expression");
                auto cast_expr = parse_expression();
                expect(TokenType::TOKEN_RPAREN, "Expected ')' closing cast expression");
                return std::make_unique<CastNode>(name + "::" + member, std::move(cast_expr));
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
            return std::make_unique<CastNode>(name, std::move(cast_expr));
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
                return std::make_unique<IndexSelectNode>(name + "." + sub_name, std::move(msb));
            } else {
                return std::make_unique<MemberAccessNode>(name, sub_name);
            }
        }

        // Function call: identifier followed immediately by '('
        if (match(TokenType::TOKEN_LPAREN)) {
            auto parsed = parse_call_argument_list();
            return std::make_unique<FunctionCallNode>(name, std::move(parsed.args), std::move(parsed.arg_names));
        }

        if (match(TokenType::TOKEN_LBRACKET)) {
            auto msb = parse_expression();
            if (match(TokenType::TOKEN_COLON)) {
                auto lsb = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing part-select range");
                return std::make_unique<IndexSelectNode>(name, std::move(msb), std::move(lsb));
            }
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing bit-select index");
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
        return expr;
    }

    if (match(TokenType::TOKEN_LBRACE)) {
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
            expect(TokenType::TOKEN_LBRACE, "Expected '{' after 'inside'");
            std::vector<InsideRange> ranges;
            while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                if (match(TokenType::TOKEN_LBRACKET)) {
                    auto lo = parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in inside range");
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
            lhs = std::make_unique<InsideNode>(std::move(lhs), std::move(ranges));
        } else {
            std::string op = current.text;
            advance();
            auto rhs = parse_shift();
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