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
        while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
            std::string dir = "input";
            if (match(TokenType::TOKEN_INPUT)) dir = "input";
            else if (match(TokenType::TOKEN_OUTPUT)) dir = "output";
            else if (match(TokenType::TOKEN_INOUT)) dir = "inout";
            else if (match(TokenType::TOKEN_REF)) dir = "ref";

            bool is_s = false;
            bool is_int = false;
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
                is_int = true;
            }
            if (match(TokenType::TOKEN_SIGNED)) is_s = true;
            else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned") { is_s = false; advance(); }
            if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE ||
                current.type == TokenType::TOKEN_REG   || current.type == TokenType::TOKEN_INTEGER ||
                current.type == TokenType::TOKEN_BIT) {
                if (current.type == TokenType::TOKEN_INTEGER) {
                    is_s = true;
                    is_int = true;
                }
                advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint" || current.text == "string")) {
                if (current.text == "string") {
                    arg_type_name = "string";
                    is_int = true;
                    is_s = false;
                } else {
                    is_s = true;
                    is_int = true;
                }
                advance();
            }
            if (match(TokenType::TOKEN_SIGNED)) is_s = true;
            else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned") { is_s = false; advance(); } else if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON_COLON) {
                advance(); advance();
                is_int = true;
                advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && is_typedef(current.text)) {
                is_int = true;
                advance();
            }
            if (match(TokenType::TOKEN_SIGNED)) is_s = true;

            std::unique_ptr<ExpressionNode> msb = nullptr;
            std::unique_ptr<ExpressionNode> lsb = nullptr;
            if (match(TokenType::TOKEN_LBRACKET)) {
                msb = parse_expression();
                expect(TokenType::TOKEN_COLON, "Expected ':' in port range");
                lsb = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in port range");
            } else if (is_int) {
                msb = std::make_unique<NumberNode>(31ULL, (uint32_t)32);
                lsb = std::make_unique<NumberNode>(0ULL, (uint32_t)32);
            }

            std::string pname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected port name");
            PortNode pnode(pname, dir, std::move(msb), std::move(lsb), is_s);
            pnode.type_name = arg_type_name;
            pnode.is_virtual_interface = is_vif;
            pnode.interface_name = vif_iface;
            pnode.modport_name = vif_mp;
            if (match(TokenType::TOKEN_EQUAL)) {
                pnode.default_value = parse_expression();
            }
            task_node->ports.push_back(std::move(pnode));
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
        task_node->body.push_back(parse_statement());
    }
    expect(TokenType::TOKEN_ENDTASK, "Expected 'endtask'");
    if (match(TokenType::TOKEN_COLON)) {
        if (current.type == TokenType::TOKEN_IDENTIFIER) {
            advance();
            if (match(TokenType::TOKEN_COLON_COLON)) {
                if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
            }
        }
    }
    return task_node;
}
