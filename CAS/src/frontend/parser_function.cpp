#include "../../include/frontend/parser.hpp"
#include <stdexcept>

std::unique_ptr<FunctionNode> Parser::parse_function(bool is_pure, bool is_extern) {
    auto func = std::make_unique<FunctionNode>();
    func->is_pure = is_pure;
    func->is_extern = is_extern;
    func->has_body = !is_pure && !is_extern;
    if (match(TokenType::TOKEN_AUTOMATIC)) {
        func->is_automatic = true;
    } else if (match(TokenType::TOKEN_STATIC)) {
        func->is_automatic = false;
        func->is_static = true;
    }

    bool name_already_parsed = false;
    if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON_COLON && is_known_class(current.text)) {
        std::string class_name = current.text;
        advance(); // consume class_name
        advance(); // consume '::'
        std::string method_name = "";
        if (current.type == TokenType::TOKEN_NEW) {
            method_name = "new";
            advance();
            func->return_width = 0;
        } else {
            method_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected method name after '::'");
            ClassNode* cnode = find_class(class_name);
            if (cnode && cnode->function_map.count(method_name)) {
                func->return_width = cnode->function_map[method_name]->return_width;
                func->return_signed = cnode->function_map[method_name]->return_signed;
            }
        }
        func->name = class_name + "::" + method_name;
        name_already_parsed = true;
    }

    if (!name_already_parsed) {
        if (match(TokenType::TOKEN_SIGNED)) {
            func->return_signed = true;
        } else if (match(TokenType::TOKEN_UNSIGNED)) {
            func->return_signed = false;
        } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned") {
            advance();
            func->return_signed = false;
        }
        if (current.type == TokenType::TOKEN_VOID) {
            func->return_width = 0;
            advance();
        } else if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE ||
            current.type == TokenType::TOKEN_REG   || current.type == TokenType::TOKEN_BIT) {
            func->return_width = 1;
            advance();
        } else if (current.type == TokenType::TOKEN_INTEGER) {
            func->return_width = 32;
            func->return_signed = true;
            advance();
        } else if (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "string" || current.text == "void" || current.text == "byte" || current.text == "shortint" || current.text == "int" || current.text == "longint")) {
            if (current.text == "string") { func->return_width = 64; func->return_type_name = "string"; }
            else if (current.text == "void") func->return_width = 0;
            else if (current.text == "byte") func->return_width = 8;
            else if (current.text == "shortint") func->return_width = 16;
            else if (current.text == "int") { func->return_width = 32; func->return_signed = true; }
            else if (current.text == "longint") { func->return_width = 64; func->return_signed = true; }
            advance();
        } else if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON_COLON) {
            std::string p_name = current.text;
            advance(); advance();
            std::string t_name = current.text;
            std::string full_t = p_name + "::" + t_name;
            auto einfo = get_enum_typedef(full_t);
            auto sinfo = get_struct_typedef(full_t);
            func->return_width = sinfo ? sinfo->total_width : (einfo ? einfo->base_width : 32);
            func->return_signed = sinfo ? sinfo->is_signed : (einfo ? einfo->is_signed : false);
            advance();
        } else if (current.type == TokenType::TOKEN_IDENTIFIER && is_typedef(current.text)) {
            auto einfo = get_enum_typedef(current.text);
            auto sinfo = get_struct_typedef(current.text);
            func->return_width = sinfo ? sinfo->total_width : (einfo ? einfo->base_width : 32);
            func->return_signed = sinfo ? sinfo->is_signed : (einfo ? einfo->is_signed : false);
            advance();
        } else if (current.type == TokenType::TOKEN_IDENTIFIER && is_class(current.text)) {
            func->return_width = 64;
            func->return_type_name = current.text;
            func->return_signed = false;
            advance();
        }
        if (match(TokenType::TOKEN_SIGNED)) {
            func->return_signed = true;
        } else if (match(TokenType::TOKEN_UNSIGNED)) {
            func->return_signed = false;
        } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned") {
            advance();
            func->return_signed = false;
        }

        if (match(TokenType::TOKEN_LBRACKET)) {
            auto msb = parse_expression();
            expect(TokenType::TOKEN_COLON, "Expected ':' in function return range");
            auto lsb = parse_expression();
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in function return range");
            if (auto nm = dynamic_cast<NumberNode*>(msb.get())) {
                if (auto nl = dynamic_cast<NumberNode*>(lsb.get())) {
                    func->return_width = (nm->aval >= nl->aval) ? (uint32_t)(nm->aval - nl->aval + 1) : (uint32_t)(nl->aval - nm->aval + 1);
                }
            }
            func->return_msb_expr = std::move(msb);
            func->return_lsb_expr = std::move(lsb);
        }

        if (current.type == TokenType::TOKEN_NEW) {
            func->name = "new";
            advance();
        } else {
            func->name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected function name");
            if (match(TokenType::TOKEN_COLON_COLON)) {
                std::string class_name = func->name;
                std::string method_name = "";
                if (current.type == TokenType::TOKEN_NEW) {
                    method_name = "new";
                    advance();
                } else {
                    method_name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected method name after '::'");
                }
                func->name = class_name + "::" + method_name;
            }
        }
    }

    if (func->name.find("::") != std::string::npos) {
        if (func->is_static) {
            throw std::runtime_error("[CAS-DTINPCIL] Out-of-body method implementation cannot specify qualifiers ('virtual', 'static', 'extern') per IEEE 1800-2023 section 8.24: '" + func->name + "'");
        }
    }

    if (match(TokenType::TOKEN_LPAREN)) {
        while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
            std::string dir = "input";
            if (match(TokenType::TOKEN_INPUT)) dir = "input";
            else if (match(TokenType::TOKEN_OUTPUT)) dir = "output";
            else if (match(TokenType::TOKEN_INOUT)) dir = "inout";
            else if (match(TokenType::TOKEN_REF)) dir = "ref";

            bool is_s = false;
            bool is_int = false;
            uint32_t def_w = 1;
            bool is_vif = false;
            std::string vif_iface = "";
            std::string vif_mp = "";
            std::string arg_type_name = "";
            if (match(TokenType::TOKEN_VIRTUAL)) {
                is_vif = true;
                if (current.type == TokenType::TOKEN_IDENTIFIER || is_interface_type(current.text)) {
                    vif_iface = current.text;
                    advance();
                    if (match(TokenType::TOKEN_DOT)) {
                        vif_mp = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected modport name after '.'");
                    }
                }
                def_w = 64;
                is_int = true;
            }
            if (match(TokenType::TOKEN_SIGNED)) is_s = true;
            else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned") { is_s = false; advance(); }
            if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE ||
                current.type == TokenType::TOKEN_REG   || current.type == TokenType::TOKEN_INTEGER ||
                current.type == TokenType::TOKEN_BIT) {
                if (current.type == TokenType::TOKEN_INTEGER) { is_s = true; is_int = true; def_w = 32; }
                advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint" || current.text == "string")) {
                if (current.text == "string") {
                    is_s = false;
                    is_int = true;
                    def_w = 64;
                    arg_type_name = "string";
                } else {
                    is_s = true;
                    is_int = true;
                    if (current.text == "byte") def_w = 8;
                    else if (current.text == "shortint") def_w = 16;
                    else if (current.text == "longint") def_w = 64;
                    else def_w = 32;
                }
                advance();
            }
            if (match(TokenType::TOKEN_SIGNED)) is_s = true;
            else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned") { is_s = false; advance(); } else if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON_COLON) {
                std::string p_name = current.text;
                advance(); advance();
                std::string t_name = current.text;
                std::string full_t = p_name + "::" + t_name;
                auto einfo = get_enum_typedef(full_t);
                auto sinfo = get_struct_typedef(full_t);
                def_w = sinfo ? sinfo->total_width : (einfo ? einfo->base_width : 32);
                is_s = sinfo ? sinfo->is_signed : (einfo ? einfo->is_signed : false);
                is_int = true;
                advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && is_typedef(current.text)) {
                auto einfo = get_enum_typedef(current.text);
                auto sinfo = get_struct_typedef(current.text);
                def_w = sinfo ? sinfo->total_width : (einfo ? einfo->base_width : 32);
                is_s = sinfo ? sinfo->is_signed : (einfo ? einfo->is_signed : false);
                is_int = true;
                advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && is_class(current.text)) {
                arg_type_name = current.text;
                def_w = 64;
                is_s = false;
                is_int = false;
                advance();
            }
            if (match(TokenType::TOKEN_SIGNED)) is_s = true;

            std::vector<AstDimension> packed_dims;
            std::unique_ptr<ExpressionNode> msb = nullptr;
            std::unique_ptr<ExpressionNode> lsb = nullptr;
            while (match(TokenType::TOKEN_LBRACKET)) {
                auto m = parse_expression();
                expect(TokenType::TOKEN_COLON, "Expected ':' in argument range");
                auto l = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in argument range");
                AstDimension ad;
                ad.left_expr = m->clone();
                ad.right_expr = l->clone();
                ad.is_single = false;
                packed_dims.push_back(std::move(ad));
                if (!msb) {
                    msb = std::move(m);
                    lsb = std::move(l);
                }
            }
            if (packed_dims.size() > 1) {
                uint32_t total_w = 1;
                bool all_const = true;
                for (const auto& pd : packed_dims) {
                    int64_t v1 = 0, v2 = 0;
                    if (auto nl = dynamic_cast<NumberNode*>(pd.left_expr.get())) v1 = (int64_t)nl->aval;
                    else all_const = false;
                    if (auto nr = dynamic_cast<NumberNode*>(pd.right_expr.get())) v2 = (int64_t)nr->aval;
                    else all_const = false;
                    if (all_const) {
                        uint32_t d_w = (v1 >= v2) ? (uint32_t)(v1 - v2 + 1) : (uint32_t)(v2 - v1 + 1);
                        total_w *= d_w;
                    }
                }
                if (all_const && total_w > 0) {
                    msb = std::make_unique<NumberNode>((uint64_t)(total_w - 1), 32);
                    lsb = std::make_unique<NumberNode>(0ULL, 32);
                }
            } else if (packed_dims.empty() && is_int) {
                msb = std::make_unique<NumberNode>(def_w > 0 ? (def_w - 1) : 0, (uint32_t)32);
                lsb = std::make_unique<NumberNode>(0ULL, (uint32_t)32);
            }

            std::string arg_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected argument name");
            PortNode pnode(arg_name, dir, std::move(msb), std::move(lsb), is_s);
            pnode.packed_dimensions = packed_dims;
            pnode.type_name = arg_type_name;
            pnode.is_virtual_interface = is_vif;
            pnode.interface_name = vif_iface;
            pnode.modport_name = vif_mp;
            if (match(TokenType::TOKEN_EQUAL)) {
                pnode.default_value = parse_expression();
            }
            func->inputs.push_back(std::move(pnode));
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing function argument list");
    }
    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after function header");

    if (is_pure || is_extern) {
        return func;
    }

    while (current.type != TokenType::TOKEN_ENDFUNCTION && current.type != TokenType::TOKEN_EOF) {
        if (current.type == TokenType::TOKEN_INPUT || current.type == TokenType::TOKEN_OUTPUT ||
            current.type == TokenType::TOKEN_INOUT || current.type == TokenType::TOKEN_REF) {
            std::string dir = "input";
            if (match(TokenType::TOKEN_INPUT)) dir = "input";
            else if (match(TokenType::TOKEN_OUTPUT)) dir = "output";
            else if (match(TokenType::TOKEN_INOUT)) dir = "inout";
            else if (match(TokenType::TOKEN_REF)) dir = "ref";

            bool is_s = false;
            if (match(TokenType::TOKEN_SIGNED)) is_s = true;
            else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned") { is_s = false; advance(); }
            if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE ||
                current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_BIT) {
                advance();
            }
            if (match(TokenType::TOKEN_SIGNED)) is_s = true;

            std::unique_ptr<ExpressionNode> msb = nullptr;
            std::unique_ptr<ExpressionNode> lsb = nullptr;
            if (match(TokenType::TOKEN_LBRACKET)) {
                msb = parse_expression();
                expect(TokenType::TOKEN_COLON, "Expected ':' in port range");
                lsb = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing port range");
            }
            while (true) {
                std::string arg_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected port identifier");
                auto p_msb = msb ? msb->clone() : nullptr;
                auto p_lsb = lsb ? lsb->clone() : nullptr;
                PortNode pnode(arg_name, dir, std::move(p_msb), std::move(p_lsb), is_s);
                func->inputs.push_back(std::move(pnode));
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after port declaration");
            continue;
        }
        func->body.push_back(parse_statement());
    }
    expect(TokenType::TOKEN_ENDFUNCTION, "Expected 'endfunction'");
    if (match(TokenType::TOKEN_COLON)) {
        if (current.type == TokenType::TOKEN_IDENTIFIER || current.type == TokenType::TOKEN_NEW) {
            advance();
            if (match(TokenType::TOKEN_COLON_COLON)) {
                if (current.type == TokenType::TOKEN_IDENTIFIER || current.type == TokenType::TOKEN_NEW) advance();
            }
        }
    }
    return func;
}
