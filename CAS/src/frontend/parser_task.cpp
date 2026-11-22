#include "../../include/frontend/parser.hpp"
#include <stdexcept>

std::unique_ptr<TaskNode> Parser::parse_task(bool is_pure, bool is_extern) {
    auto task_node = std::make_unique<TaskNode>();
    task_node->is_pure = is_pure;
    task_node->is_extern = is_extern;
    task_node->has_body = !is_pure && !is_extern;

    if (match(TokenType::TOKEN_AUTOMATIC)) {
        task_node->is_automatic = true;
    } else if (match(TokenType::TOKEN_STATIC)) {
        task_node->is_automatic = false;
        task_node->is_static = true;
    } else if (current_parsing_module && current_parsing_module->is_automatic) {
        task_node->is_automatic = true;
    }

    task_node->name = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected task name");
    if (match(TokenType::TOKEN_COLON_COLON)) {
        std::string class_name = task_node->name;
        std::string method_name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected method name after '::'");
        task_node->name = class_name + "::" + method_name;
    }

    if (task_node->name.find("::") != std::string::npos) {
        if (task_node->is_static) {
            throw std::runtime_error("[CAS-DTINPCIL] Out-of-body task implementation cannot specify qualifiers ('virtual', 'static', 'extern') per IEEE 1800-2023 section 8.24: '" + task_node->name + "'");
        }
    }

    if (match(TokenType::TOKEN_LPAREN)) {
        std::string prev_dir = "input";
        bool prev_is_s = false;
        bool prev_is_int = false;
        uint32_t prev_def_w = 1;
        bool prev_is_vif = false;
        std::string prev_vif_iface = "";
        std::string prev_vif_mp = "";
        std::string prev_arg_type_name = "";
        std::vector<AstDimension> prev_packed_dims;
        bool has_prev_arg = false;

        while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
            bool has_explicit_dir = false;
            std::string dir = has_prev_arg ? prev_dir : "input";
            if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "const") {
                advance();
                if (match(TokenType::TOKEN_REF)) {
                    dir = "ref";
                    has_explicit_dir = true;
                }
            } else if (match(TokenType::TOKEN_INPUT)) { dir = "input"; has_explicit_dir = true; }
            else if (match(TokenType::TOKEN_OUTPUT)) { dir = "output"; has_explicit_dir = true; }
            else if (match(TokenType::TOKEN_INOUT)) { dir = "inout"; has_explicit_dir = true; }
            else if (match(TokenType::TOKEN_REF)) { dir = "ref"; has_explicit_dir = true; }

            bool has_explicit_type = false;
            bool is_s = false;
            bool is_int = false;
            uint32_t def_w = 1;
            bool is_vif = false;
            std::string vif_iface = "";
            std::string vif_mp = "";
            std::string arg_type_name = "";
            if (match(TokenType::TOKEN_VIRTUAL)) {
                has_explicit_type = true;
                is_vif = true;
                if (current.type == TokenType::TOKEN_INTERFACE) advance();
                if (current.type == TokenType::TOKEN_IDENTIFIER || is_interface_type(current.text)) {
                    vif_iface = current.text;
                    advance();
                    if (match(TokenType::TOKEN_HASH)) {
                        expect(TokenType::TOKEN_LPAREN, "Expected '(' after '#' in virtual interface");
                        int pdepth = 1;
                        while (pdepth > 0 && current.type != TokenType::TOKEN_EOF) {
                            if (current.type == TokenType::TOKEN_LPAREN) pdepth++;
                            else if (current.type == TokenType::TOKEN_RPAREN) pdepth--;
                            advance();
                        }
                    }
                    if (match(TokenType::TOKEN_DOT)) {
                        vif_mp = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected modport name after '.'");
                    }
                }
                def_w = 64;
                is_int = true;
            }
            if (match(TokenType::TOKEN_SIGNED)) {
                is_s = true; has_explicit_type = true; def_w = 1; is_int = false;
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "signed") {
                is_s = true; has_explicit_type = true; def_w = 1; is_int = false;
                advance();
            } else if (match(TokenType::TOKEN_UNSIGNED)) {
                is_s = false; has_explicit_type = true; def_w = 1; is_int = false;
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned") {
                is_s = false; has_explicit_type = true; def_w = 1; is_int = false;
                advance();
            }
            if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE ||
                current.type == TokenType::TOKEN_REG   || current.type == TokenType::TOKEN_INTEGER ||
                current.type == TokenType::TOKEN_BIT   || current.type == TokenType::TOKEN_REAL ||
                current.type == TokenType::TOKEN_SHORTREAL) {
                has_explicit_type = true;
                if (current.type == TokenType::TOKEN_INTEGER) { is_s = true; is_int = true; def_w = 32; }
                else if (current.type == TokenType::TOKEN_REAL) { is_s = false; is_int = false; def_w = 64; arg_type_name = "real"; }
                else if (current.type == TokenType::TOKEN_SHORTREAL) { is_s = false; is_int = false; def_w = 32; arg_type_name = "shortreal"; }
                else if (current.type == TokenType::TOKEN_BIT) { arg_type_name = "bit"; }
                else if (current.type == TokenType::TOKEN_LOGIC) { arg_type_name = "logic"; }
                advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint" || current.text == "string" || current.text == "real" || current.text == "shortreal")) {
                has_explicit_type = true;
                if (current.text == "string") {
                    arg_type_name = "string";
                    is_int = true;
                    is_s = false;
                    def_w = 64;
                } else if (current.text == "real") {
                    is_s = false;
                    is_int = false;
                    def_w = 64;
                    arg_type_name = "real";
                } else if (current.text == "shortreal") {
                    is_s = false;
                    is_int = false;
                    def_w = 32;
                    arg_type_name = "shortreal";
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
            if (match(TokenType::TOKEN_SIGNED)) { is_s = true; has_explicit_type = true; }
            else if (match(TokenType::TOKEN_UNSIGNED)) { is_s = false; has_explicit_type = true; }
            else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned") {
                is_s = false; has_explicit_type = true; advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON_COLON) {
                has_explicit_type = true;
                std::string p_name = current.text;
                advance(); advance();
                std::string t_name = current.text;
                std::string full_t = p_name + "::" + t_name;
                arg_type_name = full_t;
                auto einfo = get_enum_typedef(full_t);
                auto sinfo = get_struct_typedef(full_t);
                def_w = sinfo ? sinfo->total_width : (einfo ? einfo->base_width : 32);
                is_s = sinfo ? sinfo->is_signed : (einfo ? einfo->is_signed : false);
                is_int = true;
                advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && (is_typedef(current.text) || is_simple_typedef(current.text) || is_module_type_parameter(current.text))) {
                has_explicit_type = true;
                arg_type_name = current.text;
                SimpleTypedef smp;
                if (get_simple_typedef(current.text, smp)) {
                    def_w = smp.width;
                    is_s = smp.is_signed;
                } else {
                    def_w = 32;
                }
                is_int = true;
                advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && (is_class(current.text) || is_known_class(current.text))) {
                has_explicit_type = true;
                arg_type_name = current.text;
                is_int = false;
                def_w = 64;
                advance();
                if (match(TokenType::TOKEN_HASH)) {
                    if (match(TokenType::TOKEN_LPAREN)) {
                        int depth = 1;
                        while (depth > 0 && current.type != TokenType::TOKEN_EOF) {
                            if (current.type == TokenType::TOKEN_LPAREN) depth++;
                            else if (current.type == TokenType::TOKEN_RPAREN) depth--;
                            advance();
                        }
                    }
                }
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && (peek_token().type == TokenType::TOKEN_IDENTIFIER || (peek_token().type == TokenType::TOKEN_LBRACKET && is_type_identifier(current.text)))) {
                has_explicit_type = true;
                arg_type_name = current.text;
                def_w = 32;
                advance();
            }
            if (match(TokenType::TOKEN_SIGNED)) { is_s = true; has_explicit_type = true; }

            std::vector<AstDimension> packed_dims;
            std::unique_ptr<ExpressionNode> msb = nullptr;
            std::unique_ptr<ExpressionNode> lsb = nullptr;
            while (match(TokenType::TOKEN_LBRACKET)) {
                has_explicit_type = true;
                auto m = parse_expression();
                expect(TokenType::TOKEN_COLON, "Expected ':' in port range");
                auto l = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in port range");
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
            if (!has_explicit_type) {
                if (has_explicit_dir) {
                    is_s = false;
                    is_int = false;
                    def_w = 1;
                    is_vif = false;
                    vif_iface = "";
                    vif_mp = "";
                    arg_type_name = "";
                    packed_dims.clear();
                } else if (has_prev_arg) {
                    is_s = prev_is_s;
                    is_int = prev_is_int;
                    def_w = prev_def_w;
                    is_vif = prev_is_vif;
                    vif_iface = prev_vif_iface;
                    vif_mp = prev_vif_mp;
                    arg_type_name = prev_arg_type_name;
                    for (const auto& pd : prev_packed_dims) {
                        AstDimension cad;
                        cad.left_expr = pd.left_expr ? pd.left_expr->clone() : nullptr;
                        cad.right_expr = pd.right_expr ? pd.right_expr->clone() : nullptr;
                        cad.is_single = pd.is_single;
                        packed_dims.push_back(std::move(cad));
                    }
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
            } else if (!packed_dims.empty() && !msb) {
                msb = packed_dims[0].left_expr ? packed_dims[0].left_expr->clone() : nullptr;
                lsb = packed_dims[0].right_expr ? packed_dims[0].right_expr->clone() : nullptr;
            }

            std::string pname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected port name");
            PortNode pnode(pname, dir, std::move(msb), std::move(lsb), is_s);
            pnode.packed_dimensions = packed_dims;
            pnode.type_name = arg_type_name;
            pnode.is_virtual_interface = is_vif;
            pnode.interface_name = vif_iface;
            pnode.modport_name = vif_mp;

            while (match(TokenType::TOKEN_LBRACKET)) {
                if (match(TokenType::TOKEN_DOLLAR)) {
                    pnode.is_queue = true;
                    if (match(TokenType::TOKEN_COLON)) {
                        pnode.queue_bound_expr = parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                    break;
                } else if (match(TokenType::TOKEN_RBRACKET)) {
                    pnode.is_dynamic_array = true;
                    break;
                } else if (match(TokenType::TOKEN_STAR)) {
                    pnode.is_associative_array = true;
                    pnode.assoc_key_type = "*";
                    pnode.assoc_key_width = 64;
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in associative array dimension");
                    break;
                } else if (is_assoc_key_token(current)) {
                    pnode.is_associative_array = true;
                    parse_assoc_key_type(pnode.assoc_key_type, pnode.assoc_key_width);
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
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in port unpacked dimension");
                    AstDimension ad;
                    ad.left_expr = std::move(l);
                    ad.right_expr = std::move(r);
                    ad.is_single = is_single;
                    pnode.unpacked_dimensions.push_back(std::move(ad));
                }
            }
            if (match(TokenType::TOKEN_REP_CONSEC)) {
                pnode.is_associative_array = true;
                pnode.assoc_key_type = "*";
                pnode.assoc_key_width = 64;
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in associative array dimension");
            }
            if (!pnode.unpacked_dimensions.empty()) {
                pnode.bound1_expr = pnode.unpacked_dimensions[0].left_expr->clone();
                if (pnode.unpacked_dimensions[0].right_expr) {
                    pnode.bound2_expr = pnode.unpacked_dimensions[0].right_expr->clone();
                }
            }

            if (match(TokenType::TOKEN_EQUAL)) {
                if (dir == "output") {
                    throw std::runtime_error("Default values are only allowed for input, inout, or ref arguments, illegal on output port per IEEE 1800-2023 section 13.5.3");
                }
                pnode.default_value = parse_expression();
            }
            task_node->ports.push_back(std::move(pnode));
            prev_dir = dir;
            prev_is_s = is_s;
            prev_is_int = is_int;
            prev_def_w = def_w;
            prev_is_vif = is_vif;
            prev_vif_iface = vif_iface;
            prev_vif_mp = vif_mp;
            prev_arg_type_name = arg_type_name;
            prev_packed_dims.clear();
            for (const auto& pd : packed_dims) {
                AstDimension cad;
                cad.left_expr = pd.left_expr ? pd.left_expr->clone() : nullptr;
                cad.right_expr = pd.right_expr ? pd.right_expr->clone() : nullptr;
                cad.is_single = pd.is_single;
                prev_packed_dims.push_back(std::move(cad));
            }
            has_prev_arg = true;
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing task port list");
    }
    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after task header");

    if (is_pure || is_extern) {
        return task_node;
    }

    while (current.type != TokenType::TOKEN_ENDTASK && current.type != TokenType::TOKEN_EOF) {
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
                current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_BIT ||
                current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_REAL ||
                (current.type == TokenType::TOKEN_IDENTIFIER &&
                 (current.text == "int" || current.text == "byte" || current.text == "shortint" ||
                  current.text == "longint" || current.text == "time" || current.text == "realtime" ||
                  current.text == "string" || current.text == "shortreal"))) {
                if (current.type == TokenType::TOKEN_INTEGER ||
                    (current.type == TokenType::TOKEN_IDENTIFIER &&
                     (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint"))) {
                    is_s = true;
                }
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
                while (match(TokenType::TOKEN_LBRACKET)) {
                    auto l = parse_expression();
                    std::unique_ptr<ExpressionNode> r = nullptr;
                    bool is_single = true;
                    if (match(TokenType::TOKEN_COLON)) {
                        r = parse_expression();
                        is_single = false;
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in port unpacked dimension");
                    AstDimension ad;
                    ad.left_expr = std::move(l);
                    ad.right_expr = std::move(r);
                    ad.is_single = is_single;
                    pnode.unpacked_dimensions.push_back(std::move(ad));
                }
                if (!pnode.unpacked_dimensions.empty()) {
                    pnode.bound1_expr = pnode.unpacked_dimensions[0].left_expr->clone();
                    if (pnode.unpacked_dimensions[0].right_expr) {
                        pnode.bound2_expr = pnode.unpacked_dimensions[0].right_expr->clone();
                    }
                }
                task_node->ports.push_back(std::move(pnode));
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after port declaration");
            continue;
        }
        task_node->body.push_back(parse_statement());
    }
    expect(TokenType::TOKEN_ENDTASK, "Expected 'endtask'");
    if (match(TokenType::TOKEN_COLON)) {
        if (current.type == TokenType::TOKEN_IDENTIFIER) {
            std::string end_id = current.text;
            advance();
            if (match(TokenType::TOKEN_COLON_COLON)) {
                if (current.type == TokenType::TOKEN_IDENTIFIER) {
                    end_id = current.text;
                    advance();
                }
            }
            if (!task_node->name.empty() && end_id != task_node->name) {
                throw std::runtime_error("End label '" + end_id + "' does not match task name '" + task_node->name + "'");
            }
        }
    }
    return task_node;
}
