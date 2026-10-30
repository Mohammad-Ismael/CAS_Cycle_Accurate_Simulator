#include "../../include/frontend/parser.hpp"
#include <stdexcept>

std::unique_ptr<FunctionNode> Parser::parse_function() {
    auto func = std::make_unique<FunctionNode>();
    if (match(TokenType::TOKEN_AUTOMATIC)) {
        func->is_automatic = true;
    } else if (match(TokenType::TOKEN_STATIC)) {
        func->is_automatic = false;
    }

    if (match(TokenType::TOKEN_SIGNED)) func->return_signed = true;
    if (current.type == TokenType::TOKEN_VOID) {
        func->return_width = 0;
        advance();
    } else if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE ||
        current.type == TokenType::TOKEN_REG   || current.type == TokenType::TOKEN_INTEGER ||
        current.type == TokenType::TOKEN_BIT) {
        if (current.type == TokenType::TOKEN_INTEGER) func->return_signed = true;
        advance();
    } else if (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "string" || current.text == "void" || current.text == "byte" || current.text == "shortint" || current.text == "int" || current.text == "longint")) {
        if (current.text == "string") func->return_width = 64;
        else if (current.text == "void") func->return_width = 0;
        else if (current.text == "byte") func->return_width = 8;
        else if (current.text == "shortint") func->return_width = 16;
        else if (current.text == "int") { func->return_width = 32; func->return_signed = true; }
        else if (current.text == "longint") { func->return_width = 64; func->return_signed = true; }
        advance();
    }
    if (match(TokenType::TOKEN_SIGNED)) func->return_signed = true;

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
    }

    if (current.type == TokenType::TOKEN_NEW) {
        func->name = "new";
        advance();
    } else {
        func->name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected function name");
    }

    if (match(TokenType::TOKEN_LPAREN)) {
        while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
            match(TokenType::TOKEN_INPUT);
            bool is_s = false;
            bool is_int = false;
            uint32_t def_w = 1;
            if (match(TokenType::TOKEN_SIGNED)) is_s = true;
            if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE ||
                current.type == TokenType::TOKEN_REG   || current.type == TokenType::TOKEN_INTEGER ||
                current.type == TokenType::TOKEN_BIT) {
                if (current.type == TokenType::TOKEN_INTEGER) { is_s = true; is_int = true; def_w = 32; }
                advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint")) {
                is_s = true;
                is_int = true;
                if (current.text == "byte") def_w = 8;
                else if (current.text == "shortint") def_w = 16;
                else if (current.text == "longint") def_w = 64;
                else def_w = 32;
                advance();
            }
            if (match(TokenType::TOKEN_SIGNED)) is_s = true;

            std::unique_ptr<ExpressionNode> msb = nullptr;
            std::unique_ptr<ExpressionNode> lsb = nullptr;
            if (match(TokenType::TOKEN_LBRACKET)) {
                msb = parse_expression();
                expect(TokenType::TOKEN_COLON, "Expected ':' in argument range");
                lsb = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in argument range");
            } else if (is_int) {
                msb = std::make_unique<NumberNode>(def_w > 0 ? (def_w - 1) : 0, (uint32_t)32);
                lsb = std::make_unique<NumberNode>(0ULL, (uint32_t)32);
            }

            std::string arg_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected argument name");
            PortNode pnode(arg_name, "input", std::move(msb), std::move(lsb), is_s);
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

    while (current.type != TokenType::TOKEN_ENDFUNCTION && current.type != TokenType::TOKEN_EOF) {
        func->body.push_back(parse_statement());
    }
    expect(TokenType::TOKEN_ENDFUNCTION, "Expected 'endfunction'");
    if (match(TokenType::TOKEN_COLON)) {
        if (current.type == TokenType::TOKEN_IDENTIFIER || current.type == TokenType::TOKEN_NEW) advance();
    }
    return func;
}
