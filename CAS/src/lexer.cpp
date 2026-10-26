#include "../include/lexer.hpp"
#include <cctype>
#include <stdexcept>

Lexer::Lexer(std::string source) : src(std::move(source)) {}

char Lexer::peek() const {
    if (cursor >= src.size()) return '\0';
    return src[cursor];
}

char Lexer::advance() {
    if (cursor >= src.size()) return '\0';
    char c = src[cursor++];
    if (c == '\n') line++;
    return c;
}

void Lexer::skip_whitespace() {
    while (true) {
        char c = peek();
        if (std::isspace(c)) {
            advance();
        } else if (c == '/' && cursor + 1 < src.size() && src[cursor + 1] == '/') {
            while (peek() != '\n' && peek() != '\0') advance();
        } else if (c == '/' && cursor + 1 < src.size() && src[cursor + 1] == '*') {
            advance(); advance();
            while (!(peek() == '*' && cursor + 1 < src.size() && src[cursor + 1] == '/')) {
                if (peek() == '\0') break;
                advance();
            }
            if (peek() != '\0') { advance(); advance(); }
        } else {
            break;
        }
    }
}

Token Lexer::next_token() {
    skip_whitespace();
    if (cursor >= src.size()) return {TokenType::TOKEN_EOF, "", 0, 0, line};

    char c = peek();

    // Delimiters
    if (c == '(') { advance(); return {TokenType::TOKEN_LPAREN, "(", 0, 0, line}; }
    if (c == ')') { advance(); return {TokenType::TOKEN_RPAREN, ")", 0, 0, line}; }
    if (c == '[') { advance(); return {TokenType::TOKEN_LBRACKET, "[", 0, 0, line}; }
    if (c == ']') { advance(); return {TokenType::TOKEN_RBRACKET, "]", 0, 0, line}; }
    if (c == '{') { advance(); return {TokenType::TOKEN_LBRACE, "{", 0, 0, line}; }
    if (c == '}') { advance(); return {TokenType::TOKEN_RBRACE, "}", 0, 0, line}; }
    if (c == ';') { advance(); return {TokenType::TOKEN_SEMICOLON, ";", 0, 0, line}; }
    if (c == ',') { advance(); return {TokenType::TOKEN_COMMA, ",", 0, 0, line}; }
    if (c == '.') { advance(); return {TokenType::TOKEN_DOT, ".", 0, 0, line}; }
    if (c == '#') { advance(); return {TokenType::TOKEN_HASH, "#", 0, 0, line}; }
    if (c == '@') { advance(); return {TokenType::TOKEN_AT, "@", 0, 0, line}; }
    if (c == '?') { advance(); return {TokenType::TOKEN_QUESTION, "?", 0, 0, line}; }
    if (c == ':') { advance(); return {TokenType::TOKEN_COLON, ":", 0, 0, line}; }
    if (c == '~') { advance(); return {TokenType::TOKEN_TILDE, "~", 0, 0, line}; }
    if (c == '^') { advance(); return {TokenType::TOKEN_XOR, "^", 0, 0, line}; }
    if (c == '+') { advance(); return {TokenType::TOKEN_PLUS, "+", 0, 0, line}; }
    if (c == '-') { advance(); return {TokenType::TOKEN_MINUS, "-", 0, 0, line}; }

    // Multi-character Operators
    if (c == '&') {
        advance();
        if (peek() == '&') { advance(); return {TokenType::TOKEN_LOGICAL_AND, "&&", 0, 0, line}; }
        return {TokenType::TOKEN_AND, "&", 0, 0, line};
    }
    if (c == '|') {
        advance();
        if (peek() == '|') { advance(); return {TokenType::TOKEN_LOGICAL_OR, "||", 0, 0, line}; }
        return {TokenType::TOKEN_OR_BIT, "|", 0, 0, line};
    }
    if (c == '!') {
        advance();
        if (peek() == '=') { advance(); return {TokenType::TOKEN_NEQ, "!=", 0, 0, line}; }
        return {TokenType::TOKEN_NOT, "!", 0, 0, line};
    }
    if (c == '=') {
        advance();
        if (peek() == '=') { advance(); return {TokenType::TOKEN_EQ, "==", 0, 0, line}; }
        return {TokenType::TOKEN_EQUAL, "=", 0, 0, line};
    }
    if (c == '<') {
        advance();
        if (peek() == '=') { advance(); return {TokenType::TOKEN_LE, "<=", 0, 0, line}; }
        if (peek() == '<') { advance(); return {TokenType::TOKEN_SHL, "<<", 0, 0, line}; }
        return {TokenType::TOKEN_LT, "<", 0, 0, line};
    }
    if (c == '>') {
        advance();
        if (peek() == '=') { advance(); return {TokenType::TOKEN_GE, ">=", 0, 0, line}; }
        if (peek() == '>') { advance(); return {TokenType::TOKEN_SHR, ">>", 0, 0, line}; }
        return {TokenType::TOKEN_GT, ">", 0, 0, line};
    }

    // Numeric Literals
    if (std::isdigit(c)) {
        std::string num_str;
        while (std::isdigit(peek())) num_str += advance();

        if (peek() == '\'') {
            uint32_t width = std::stoul(num_str);
            advance(); // consume '\''
            char base = advance();
            std::string digits;
            while (std::isxdigit(peek()) || peek() == '_' || peek() == 'x' || peek() == 'X' || peek() == 'z' || peek() == 'Z') {
                if (peek() != '_') digits += peek();
                advance();
            }

            uint64_t val = 0;
            bool is_x_or_z = false;
            for (char ch : digits) {
                if (ch == 'x' || ch == 'X' || ch == 'z' || ch == 'Z') {
                    is_x_or_z = true;
                    break;
                }
            }
            if (!is_x_or_z) {
                try {
                    if (base == 'd' || base == 'D') val = std::stoull(digits);
                    else if (base == 'h' || base == 'H') val = std::stoull(digits, nullptr, 16);
                    else if (base == 'b' || base == 'B') val = std::stoull(digits, nullptr, 2);
                } catch (...) {
                    val = 0;
                }
            }
            return {TokenType::TOKEN_NUMBER, digits, val, width, line};
        }
        return {TokenType::TOKEN_NUMBER, num_str, std::stoull(num_str), 32, line};
    }

    // String Literals
    if (c == '"') {
        advance();
        std::string s;
        while (peek() != '"' && peek() != '\0') {
            if (peek() == '\\') {
                advance();
                char esc = advance();
                if (esc == 'n') s += '\n';
                else if (esc == 't') s += '\t';
                else s += esc;
            } else {
                s += advance();
            }
        }
        if (peek() == '"') advance();
        return {TokenType::TOKEN_STRING, s, 0, 0, line};
    }

    // Identifiers and Keywords
    if (std::isalpha(c) || c == '_' || c == '$') {
        std::string ident;
        while (std::isalnum(peek()) || peek() == '_' || peek() == '$') {
            ident += advance();
        }

        if (ident == "module") return {TokenType::TOKEN_MODULE, ident, 0, 0, line};
        if (ident == "endmodule") return {TokenType::TOKEN_ENDMODULE, ident, 0, 0, line};
        if (ident == "input") return {TokenType::TOKEN_INPUT, ident, 0, 0, line};
        if (ident == "output") return {TokenType::TOKEN_OUTPUT, ident, 0, 0, line};
        if (ident == "inout") return {TokenType::TOKEN_INOUT, ident, 0, 0, line};
        if (ident == "logic") return {TokenType::TOKEN_LOGIC, ident, 0, 0, line};
        if (ident == "wire") return {TokenType::TOKEN_WIRE, ident, 0, 0, line};
        if (ident == "reg") return {TokenType::TOKEN_REG, ident, 0, 0, line};
        if (ident == "parameter") return {TokenType::TOKEN_PARAMETER, ident, 0, 0, line};
        if (ident == "localparam") return {TokenType::TOKEN_LOCALPARAM, ident, 0, 0, line};
        if (ident == "assign") return {TokenType::TOKEN_ASSIGN, ident, 0, 0, line};
        if (ident == "always") return {TokenType::TOKEN_ALWAYS, ident, 0, 0, line};
        if (ident == "always_comb") return {TokenType::TOKEN_ALWAYS_COMB, ident, 0, 0, line};
        if (ident == "always_ff") return {TokenType::TOKEN_ALWAYS_FF, ident, 0, 0, line};
        if (ident == "task") return {TokenType::TOKEN_TASK, ident, 0, 0, line};
        if (ident == "endtask") return {TokenType::TOKEN_ENDTASK, ident, 0, 0, line};

        if (ident == "or") return {TokenType::TOKEN_OR, ident, 0, 0, line};
        if (ident == "and") return {TokenType::TOKEN_GATE_AND, ident, 0, 0, line};
        if (ident == "xor") return {TokenType::TOKEN_GATE_XOR, ident, 0, 0, line};
        if (ident == "nand") return {TokenType::TOKEN_GATE_NAND, ident, 0, 0, line};
        if (ident == "nor") return {TokenType::TOKEN_GATE_NOR, ident, 0, 0, line};
        if (ident == "xnor") return {TokenType::TOKEN_GATE_XNOR, ident, 0, 0, line};
        if (ident == "not") return {TokenType::TOKEN_GATE_NOT, ident, 0, 0, line};
        if (ident == "buf") return {TokenType::TOKEN_GATE_BUF, ident, 0, 0, line};

        if (ident == "posedge") return {TokenType::TOKEN_POSEDGE, ident, 0, 0, line};
        if (ident == "negedge") return {TokenType::TOKEN_NEGEDGE, ident, 0, 0, line};
        if (ident == "if") return {TokenType::TOKEN_IF, ident, 0, 0, line};
        if (ident == "else") return {TokenType::TOKEN_ELSE, ident, 0, 0, line};
        if (ident == "case") return {TokenType::TOKEN_CASE, ident, 0, 0, line};
        if (ident == "endcase") return {TokenType::TOKEN_ENDCASE, ident, 0, 0, line};
        if (ident == "default") return {TokenType::TOKEN_DEFAULT, ident, 0, 0, line};
        if (ident == "begin") return {TokenType::TOKEN_BEGIN, ident, 0, 0, line};
        if (ident == "end") return {TokenType::TOKEN_END, ident, 0, 0, line};
        if (ident == "initial") return {TokenType::TOKEN_INITIAL, ident, 0, 0, line};
        if (ident == "$display") return {TokenType::TOKEN_DISPLAY, ident, 0, 0, line};
        if (ident == "$monitor") return {TokenType::TOKEN_MONITOR, ident, 0, 0, line};
        if (ident == "$finish") return {TokenType::TOKEN_FINISH, ident, 0, 0, line};
        if (ident == "$stop") return {TokenType::TOKEN_STOP, ident, 0, 0, line};
        if (ident == "$dumpfile") return {TokenType::TOKEN_DUMPFILE, ident, 0, 0, line};
        if (ident == "$dumpvars") return {TokenType::TOKEN_DUMPVARS, ident, 0, 0, line};

        return {TokenType::TOKEN_IDENTIFIER, ident, 0, 0, line};
    }

    throw std::runtime_error(std::string("Unknown lexical token encountered: '") + c + "' on line " + std::to_string(line));
}