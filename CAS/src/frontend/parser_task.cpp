#include "../../include/frontend/parser.hpp"
#include <stdexcept>

std::unique_ptr<TaskNode> Parser::parse_task() {
    auto task_node = std::make_unique<TaskNode>();

    if (match(TokenType::TOKEN_AUTOMATIC)) {
        task_node->is_automatic = true;
    } else if (match(TokenType::TOKEN_STATIC)) {
        task_node->is_automatic = false;
    }

    task_node->name = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected task name");

    if (match(TokenType::TOKEN_LPAREN)) {
        while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
            std::string dir = "input";
            if (match(TokenType::TOKEN_INPUT)) dir = "input";
            else if (match(TokenType::TOKEN_OUTPUT)) dir = "output";
            else if (match(TokenType::TOKEN_INOUT)) dir = "inout";

            bool is_s = false;
            bool is_int = false;
            if (match(TokenType::TOKEN_SIGNED)) is_s = true;
            if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE ||
                current.type == TokenType::TOKEN_REG   || current.type == TokenType::TOKEN_INTEGER ||
                current.type == TokenType::TOKEN_BIT) {
                if (current.type == TokenType::TOKEN_INTEGER) {
                    is_s = true;
                    is_int = true;
                }
                advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint")) {
                is_s = true;
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

    while (current.type != TokenType::TOKEN_ENDTASK && current.type != TokenType::TOKEN_EOF) {
        task_node->body.push_back(parse_statement());
    }
    expect(TokenType::TOKEN_ENDTASK, "Expected 'endtask'");
    if (match(TokenType::TOKEN_COLON)) {
        if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
    }
    return task_node;
}
