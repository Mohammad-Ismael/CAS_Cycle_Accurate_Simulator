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
    if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE ||
        current.type == TokenType::TOKEN_REG   || current.type == TokenType::TOKEN_INTEGER) {
        if (current.type == TokenType::TOKEN_INTEGER) func->return_signed = true;
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

    func->name = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected function name");

    if (match(TokenType::TOKEN_LPAREN)) {
        while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
            expect(TokenType::TOKEN_INPUT, "Expected 'input' in function argument");
            bool is_s = false;
            if (match(TokenType::TOKEN_SIGNED)) is_s = true;
            if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE ||
                current.type == TokenType::TOKEN_REG   || current.type == TokenType::TOKEN_INTEGER) {
                if (current.type == TokenType::TOKEN_INTEGER) is_s = true;
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
            } else if (is_s) {
                msb = std::make_unique<NumberNode>(31ULL, (uint32_t)32);
                lsb = std::make_unique<NumberNode>(0ULL, (uint32_t)32);
            }

            std::string arg_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected argument name");
            func->inputs.emplace_back(arg_name, "input", std::move(msb), std::move(lsb), is_s);
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
    return func;
}