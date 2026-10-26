#pragma once
#include <string>
#include <cstdint>
#include <cstddef>

enum class TokenType {
    TOKEN_EOF,
    TOKEN_MODULE,
    TOKEN_ENDMODULE,
    TOKEN_INPUT,
    TOKEN_OUTPUT,
    TOKEN_LOGIC,
    TOKEN_ALWAYS_COMB,
    TOKEN_ALWAYS_FF,
    TOKEN_AT,          // @
    TOKEN_POSEDGE,
    TOKEN_NEGEDGE,
    TOKEN_OR,          // 'or' keyword in sensitivity lists
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_NOT,         // !
    TOKEN_TILDE,       // ~
    TOKEN_BEGIN,
    TOKEN_END,
    TOKEN_INITIAL,
    TOKEN_DISPLAY,
    TOKEN_FINISH,
    TOKEN_DUMPFILE,
    TOKEN_DUMPVARS,
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_SEMICOLON,
    TOKEN_COMMA,
    TOKEN_DOT,
    TOKEN_HASH,
    TOKEN_EQUAL,       // =
    TOKEN_LE,          // <=
    TOKEN_GE,          // >=
    TOKEN_LT,          // <
    TOKEN_GT,          // >
    TOKEN_EQ,          // ==
    TOKEN_NEQ,         // !=
    TOKEN_PLUS,        // +
    TOKEN_MINUS,       // -
    TOKEN_AND,         // &
    TOKEN_OR_BIT,      // |
    TOKEN_XOR,         // ^
    TOKEN_SHL,         // <<
    TOKEN_SHR,         // >>
    TOKEN_LOGICAL_AND, // &&
    TOKEN_LOGICAL_OR,  // ||
    TOKEN_QUESTION,    // ?
    TOKEN_COLON        // :
};

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