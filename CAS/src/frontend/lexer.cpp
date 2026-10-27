#include "../../include/frontend/lexer.hpp"
#include <cctype>
#include <stdexcept>
#include <fstream>
#include <sstream>

Lexer::Lexer(const std::string& filename, const std::string& source, const std::string& base_dir, std::ostream* debug_stream)
    : current_dir(base_dir), debug_out(debug_stream) {
    file_stack.push_back({filename, source, 0, 1});
    if (debug_out) {
        *debug_out << "  [Source Accessed] #1: '" << filename << "' (" << source.size() << " bytes)\n";
    }
}

char Lexer::peek() const {
    if (file_stack.empty()) return '\0';
    const auto& cur = file_stack.back();
    if (cur.cursor >= cur.src.size()) return '\0';
    return cur.src[cur.cursor];
}

char Lexer::advance() {
    if (file_stack.empty()) return '\0';
    auto& cur = file_stack.back();
    if (cur.cursor >= cur.src.size()) return '\0';
    char c = cur.src[cur.cursor++];
    if (c == '\n') cur.line++;
    return c;
}

std::string Lexer::get_current_location() const {
    if (file_stack.empty()) return "EOF:0";
    return file_stack.back().filename + ":" + std::to_string(file_stack.back().line);
}

void Lexer::handle_include() {
    skip_whitespace_and_comments();
    char q = advance();
    if (q != '"' && q != '<') {
        throw std::runtime_error(get_current_location() + ": Expected '\"' after `include");
    }
    char close_q = (q == '"') ? '"' : '>';
    std::string inc_path = "";
    while (peek() != close_q && peek() != '\0' && peek() != '\n') {
        inc_path += advance();
    }
    if (peek() == close_q) advance();

    std::string parent_file = file_stack.back().filename;
    size_t parent_line = file_stack.back().line;

    std::string full_path = current_dir.empty() ? inc_path : (current_dir + "/" + inc_path);
    std::ifstream f(full_path);
    if (!f.is_open()) {
        f.open(inc_path);
        if (!f.is_open()) {
            throw std::runtime_error(get_current_location() + ": Cannot open `include file: " + inc_path);
        }
    }

    std::stringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();

    include_depth++;
    if (debug_out) {
        std::string indent(include_depth * 2, ' ');
        *debug_out << "  " << indent << "└── [Preprocessor `include] '" << inc_path 
                   << "' (included by " << parent_file << ":" << parent_line 
                   << ", " << content.size() << " bytes)\n";
    }

    file_stack.push_back({inc_path, content, 0, 1});
}

void Lexer::skip_whitespace_and_comments() {
    while (!file_stack.empty()) {
        auto& cur = file_stack.back();
        if (cur.cursor >= cur.src.size()) {
            if (debug_out && file_stack.size() > 1) {
                std::string indent(include_depth * 2, ' ');
                *debug_out << "  " << indent << "└── [EOF Reached] Returning to '" 
                           << file_stack[file_stack.size() - 2].filename << "'\n";
            }
            if (include_depth > 0) include_depth--;
            file_stack.pop_back();
            continue;
        }

        char c = peek();
        if (std::isspace(c)) {
            advance();
        } else if (c == '/' && cur.cursor + 1 < cur.src.size() && cur.src[cur.cursor + 1] == '/') {
            while (peek() != '\n' && peek() != '\0') advance();
        } else if (c == '/' && cur.cursor + 1 < cur.src.size() && cur.src[cur.cursor + 1] == '*') {
            advance(); advance();
            while (!(peek() == '*' && cur.cursor + 1 < cur.src.size() && cur.src[cur.cursor + 1] == '/')) {
                if (peek() == '\0') break;
                advance();
            }
            if (peek() != '\0') { advance(); advance(); }
        } else if (c == '`') {
            size_t saved_cursor = cur.cursor;
            advance(); // consume '`'
            std::string directive = "";
            while (std::isalpha(peek())) directive += advance();
            if (directive == "include") {
                handle_include();
            } else {
                cur.cursor = saved_cursor;
                break;
            }
        } else {
            break;
        }
    }
}

Token Lexer::next_token() {
    skip_whitespace_and_comments();
    if (file_stack.empty()) return {TokenType::TOKEN_EOF, "", 0, 0, "", 0};

    const std::string cur_file = file_stack.back().filename;
    const size_t cur_line = file_stack.back().line;
    char c = peek();

    // Delimiters
    if (c == '(') { advance(); return {TokenType::TOKEN_LPAREN, "(", 0, 0, cur_file, cur_line}; }
    if (c == ')') { advance(); return {TokenType::TOKEN_RPAREN, ")", 0, 0, cur_file, cur_line}; }
    if (c == '[') { advance(); return {TokenType::TOKEN_LBRACKET, "[", 0, 0, cur_file, cur_line}; }
    if (c == ']') { advance(); return {TokenType::TOKEN_RBRACKET, "]", 0, 0, cur_file, cur_line}; }
    if (c == '{') { advance(); return {TokenType::TOKEN_LBRACE, "{", 0, 0, cur_file, cur_line}; }
    if (c == '}') { advance(); return {TokenType::TOKEN_RBRACE, "}", 0, 0, cur_file, cur_line}; }
    if (c == ';') { advance(); return {TokenType::TOKEN_SEMICOLON, ";", 0, 0, cur_file, cur_line}; }
    if (c == ',') { advance(); return {TokenType::TOKEN_COMMA, ",", 0, 0, cur_file, cur_line}; }
    if (c == '.') { advance(); return {TokenType::TOKEN_DOT, ".", 0, 0, cur_file, cur_line}; }
    if (c == '#') { advance(); return {TokenType::TOKEN_HASH, "#", 0, 0, cur_file, cur_line}; }
    if (c == '@') { advance(); return {TokenType::TOKEN_AT, "@", 0, 0, cur_file, cur_line}; }
    if (c == '?') { advance(); return {TokenType::TOKEN_QUESTION, "?", 0, 0, cur_file, cur_line}; }
    if (c == ':') { advance(); return {TokenType::TOKEN_COLON, ":", 0, 0, cur_file, cur_line}; }
    if (c == '~') { advance(); return {TokenType::TOKEN_TILDE, "~", 0, 0, cur_file, cur_line}; }
    if (c == '^') { advance(); return {TokenType::TOKEN_XOR, "^", 0, 0, cur_file, cur_line}; }
    if (c == '+') { advance(); return {TokenType::TOKEN_PLUS, "+", 0, 0, cur_file, cur_line}; }
    if (c == '-') { advance(); return {TokenType::TOKEN_MINUS, "-", 0, 0, cur_file, cur_line}; }

    // Multi-character Operators
    if (c == '&') {
        advance();
        if (peek() == '&') { advance(); return {TokenType::TOKEN_LOGICAL_AND, "&&", 0, 0, cur_file, cur_line}; }
        return {TokenType::TOKEN_AND, "&", 0, 0, cur_file, cur_line};
    }
    if (c == '|') {
        advance();
        if (peek() == '|') { advance(); return {TokenType::TOKEN_LOGICAL_OR, "||", 0, 0, cur_file, cur_line}; }
        return {TokenType::TOKEN_OR_BIT, "|", 0, 0, cur_file, cur_line};
    }
    if (c == '!') {
        advance();
        if (peek() == '=') { advance(); return {TokenType::TOKEN_NEQ, "!=", 0, 0, cur_file, cur_line}; }
        return {TokenType::TOKEN_NOT, "!", 0, 0, cur_file, cur_line};
    }
    if (c == '=') {
        advance();
        if (peek() == '=') { advance(); return {TokenType::TOKEN_EQ, "==", 0, 0, cur_file, cur_line}; }
        return {TokenType::TOKEN_EQUAL, "=", 0, 0, cur_file, cur_line};
    }
    if (c == '<') {
        advance();
        if (peek() == '=') { advance(); return {TokenType::TOKEN_LE, "<=", 0, 0, cur_file, cur_line}; }
        if (peek() == '<') { advance(); return {TokenType::TOKEN_SHL, "<<", 0, 0, cur_file, cur_line}; }
        return {TokenType::TOKEN_LT, "<", 0, 0, cur_file, cur_line};
    }
    if (c == '>') {
        advance();
        if (peek() == '=') { advance(); return {TokenType::TOKEN_GE, ">=", 0, 0, cur_file, cur_line}; }
        if (peek() == '>') { advance(); return {TokenType::TOKEN_SHR, ">>", 0, 0, cur_file, cur_line}; }
        return {TokenType::TOKEN_GT, ">", 0, 0, cur_file, cur_line};
    }

    // Numeric Literals
    if (std::isdigit(c)) {
        std::string num_str;
        while (std::isdigit(peek())) num_str += advance();

        if (peek() == '\'') {
            uint32_t width = std::stoul(num_str);
            advance();
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
            return {TokenType::TOKEN_NUMBER, digits, val, width, cur_file, cur_line};
        }
        return {TokenType::TOKEN_NUMBER, num_str, std::stoull(num_str), 32, cur_file, cur_line};
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
        return {TokenType::TOKEN_STRING, s, 0, 0, cur_file, cur_line};
    }

    // Identifiers and Keywords
    if (std::isalpha(c) || c == '_' || c == '$') {
        std::string ident;
        while (std::isalnum(peek()) || peek() == '_' || peek() == '$') {
            ident += advance();
        }

        if (ident == "module") return {TokenType::TOKEN_MODULE, ident, 0, 0, cur_file, cur_line};
        if (ident == "endmodule") return {TokenType::TOKEN_ENDMODULE, ident, 0, 0, cur_file, cur_line};
        if (ident == "input") return {TokenType::TOKEN_INPUT, ident, 0, 0, cur_file, cur_line};
        if (ident == "output") return {TokenType::TOKEN_OUTPUT, ident, 0, 0, cur_file, cur_line};
        if (ident == "inout") return {TokenType::TOKEN_INOUT, ident, 0, 0, cur_file, cur_line};
        if (ident == "logic") return {TokenType::TOKEN_LOGIC, ident, 0, 0, cur_file, cur_line};
        if (ident == "wire") return {TokenType::TOKEN_WIRE, ident, 0, 0, cur_file, cur_line};
        if (ident == "reg") return {TokenType::TOKEN_REG, ident, 0, 0, cur_file, cur_line};
        if (ident == "parameter") return {TokenType::TOKEN_PARAMETER, ident, 0, 0, cur_file, cur_line};
        if (ident == "localparam") return {TokenType::TOKEN_LOCALPARAM, ident, 0, 0, cur_file, cur_line};
        if (ident == "assign") return {TokenType::TOKEN_ASSIGN, ident, 0, 0, cur_file, cur_line};
        if (ident == "always") return {TokenType::TOKEN_ALWAYS, ident, 0, 0, cur_file, cur_line};
        if (ident == "always_comb") return {TokenType::TOKEN_ALWAYS_COMB, ident, 0, 0, cur_file, cur_line};
        if (ident == "always_ff") return {TokenType::TOKEN_ALWAYS_FF, ident, 0, 0, cur_file, cur_line};
        if (ident == "task") return {TokenType::TOKEN_TASK, ident, 0, 0, cur_file, cur_line};
        if (ident == "endtask") return {TokenType::TOKEN_ENDTASK, ident, 0, 0, cur_file, cur_line};

        if (ident == "or") return {TokenType::TOKEN_OR, ident, 0, 0, cur_file, cur_line};
        if (ident == "and") return {TokenType::TOKEN_GATE_AND, ident, 0, 0, cur_file, cur_line};
        if (ident == "xor") return {TokenType::TOKEN_GATE_XOR, ident, 0, 0, cur_file, cur_line};
        if (ident == "nand") return {TokenType::TOKEN_GATE_NAND, ident, 0, 0, cur_file, cur_line};
        if (ident == "nor") return {TokenType::TOKEN_GATE_NOR, ident, 0, 0, cur_file, cur_line};
        if (ident == "xnor") return {TokenType::TOKEN_GATE_XNOR, ident, 0, 0, cur_file, cur_line};
        if (ident == "not") return {TokenType::TOKEN_GATE_NOT, ident, 0, 0, cur_file, cur_line};
        if (ident == "buf") return {TokenType::TOKEN_GATE_BUF, ident, 0, 0, cur_file, cur_line};

        if (ident == "posedge") return {TokenType::TOKEN_POSEDGE, ident, 0, 0, cur_file, cur_line};
        if (ident == "negedge") return {TokenType::TOKEN_NEGEDGE, ident, 0, 0, cur_file, cur_line};
        if (ident == "if") return {TokenType::TOKEN_IF, ident, 0, 0, cur_file, cur_line};
        if (ident == "else") return {TokenType::TOKEN_ELSE, ident, 0, 0, cur_file, cur_line};
        if (ident == "case") return {TokenType::TOKEN_CASE, ident, 0, 0, cur_file, cur_line};
        if (ident == "endcase") return {TokenType::TOKEN_ENDCASE, ident, 0, 0, cur_file, cur_line};
        if (ident == "default") return {TokenType::TOKEN_DEFAULT, ident, 0, 0, cur_file, cur_line};
        if (ident == "begin") return {TokenType::TOKEN_BEGIN, ident, 0, 0, cur_file, cur_line};
        if (ident == "end") return {TokenType::TOKEN_END, ident, 0, 0, cur_file, cur_line};
        if (ident == "initial") return {TokenType::TOKEN_INITIAL, ident, 0, 0, cur_file, cur_line};
        if (ident == "$display") return {TokenType::TOKEN_DISPLAY, ident, 0, 0, cur_file, cur_line};
        if (ident == "$monitor") return {TokenType::TOKEN_MONITOR, ident, 0, 0, cur_file, cur_line};
        if (ident == "$finish") return {TokenType::TOKEN_FINISH, ident, 0, 0, cur_file, cur_line};
        if (ident == "$stop") return {TokenType::TOKEN_STOP, ident, 0, 0, cur_file, cur_line};
        if (ident == "$dumpfile") return {TokenType::TOKEN_DUMPFILE, ident, 0, 0, cur_file, cur_line};
        if (ident == "$dumpvars") return {TokenType::TOKEN_DUMPVARS, ident, 0, 0, cur_file, cur_line};

        return {TokenType::TOKEN_IDENTIFIER, ident, 0, 0, cur_file, cur_line};
    }

    throw std::runtime_error(cur_file + ":" + std::to_string(cur_line) + ": error: Unknown lexical token '" + c + "'");
}