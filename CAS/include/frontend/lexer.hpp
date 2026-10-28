#pragma once
#include <string>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <ostream>

enum class TokenType {
    TOKEN_EOF,
    TOKEN_MODULE,
    TOKEN_ENDMODULE,
    TOKEN_INPUT,
    TOKEN_OUTPUT,
    TOKEN_INOUT,
    TOKEN_LOGIC,
    TOKEN_WIRE,
    TOKEN_REG,
    TOKEN_INTEGER,
    TOKEN_PARAMETER,
    TOKEN_LOCALPARAM,
    TOKEN_ASSIGN,
    TOKEN_ALWAYS,
    TOKEN_ALWAYS_COMB,
    TOKEN_ALWAYS_FF,
    TOKEN_TASK,
    TOKEN_ENDTASK,
    TOKEN_GATE_AND,
    TOKEN_GATE_OR,
    TOKEN_GATE_XOR,
    TOKEN_GATE_NAND,
    TOKEN_GATE_NOR,
    TOKEN_GATE_XNOR,
    TOKEN_GATE_NOT,
    TOKEN_GATE_BUF,
    TOKEN_AT,
    TOKEN_POSEDGE,
    TOKEN_NEGEDGE,
    TOKEN_OR,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_CASE,
    TOKEN_ENDCASE,
    TOKEN_DEFAULT,
    TOKEN_NOT,
    TOKEN_TILDE,
    TOKEN_BEGIN,
    TOKEN_END,
    TOKEN_INITIAL,
    TOKEN_DISPLAY,
    TOKEN_MONITOR,
    TOKEN_FINISH,
    TOKEN_STOP,
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
    TOKEN_EQUAL,
    TOKEN_LE,
    TOKEN_GE,
    TOKEN_LT,
    TOKEN_GT,
    TOKEN_EQ,
    TOKEN_NEQ,
    TOKEN_CASE_EQ,
    TOKEN_CASE_NEQ,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_AND,
    TOKEN_OR_BIT,
    TOKEN_XOR,
    TOKEN_SHL,
    TOKEN_SHR,
    TOKEN_LOGICAL_AND,
    TOKEN_LOGICAL_OR,
    TOKEN_QUESTION,
    TOKEN_COLON
};

struct Token {
    TokenType type;
    std::string text;
    uint64_t num_val;
    uint32_t width;
    std::string filename;
    size_t line;
    uint64_t bval = 0; // Added for 4-state literal support
};

struct SourceBuffer {
    std::string filename;
    std::string src;
    size_t cursor = 0;
    size_t line = 1;
};

class Lexer {
private:
    std::vector<SourceBuffer> file_stack;
    std::string current_dir = ".";
    std::ostream* debug_out = nullptr;
    size_t include_depth = 0;

    char peek() const;
    char peek_next() const;
    char advance();
    void skip_whitespace_and_comments();
    void handle_include();

public:
    explicit Lexer(const std::string& filename, const std::string& source, 
                   const std::string& base_dir = ".", std::ostream* debug_stream = nullptr);
    Token next_token();
    std::string get_current_location() const;
};