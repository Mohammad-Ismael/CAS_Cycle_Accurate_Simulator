#pragma once
#include "ast.hpp"
#include <string>

struct Token {
    TokenType type;
    std::string text;
    uint64_t num_val;
    uint32_t bit_width;
    size_t line;
};

class Lexer {
private:
    std::string src;
    size_t cursor = 0;
    size_t line = 1;

    char peek() const;
    char advance();
    void skip_whitespace();

public:
    explicit Lexer(std::string source);
    Token next_token();
};