#include "../../include/frontend/parser.hpp"
#include <stdexcept>

Parser::Parser(Lexer& lex) : lexer(lex) {
    advance();
}

void Parser::advance() {
    if (has_peek) {
        current = peeked;
        has_peek = false;
    } else {
        current = lexer.next_token();
    }
}

Token Parser::peek_token() {
    if (!has_peek) {
        peeked = lexer.next_token();
        has_peek = true;
    }
    return peeked;
}

bool Parser::match(TokenType t) {
    if (current.type == t) {
        advance();
        return true;
    }
    return false;
}

void Parser::expect(TokenType t, const std::string& err) {
    if (current.type != t) {
        std::string loc = current.filename.empty() 
            ? ("line " + std::to_string(current.line)) 
            : (current.filename + ":" + std::to_string(current.line));
        throw std::runtime_error(loc + ": error: " + err + " (found: '" + current.text + "')");
    }
    advance();
}