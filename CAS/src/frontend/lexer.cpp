#include "../../include/frontend/lexer.hpp"
#include "../../include/frontend/number_parser.hpp"
#include <cctype>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <cmath>

uint64_t Lexer::global_timescale_ratio = 1;

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

char Lexer::peek_next() const {
    if (file_stack.empty()) return '\0';
    const auto& cur = file_stack.back();
    if (cur.cursor + 1 >= cur.src.size()) return '\0';
    return cur.src[cur.cursor + 1];
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

static uint64_t parse_time_fs(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && (std::isdigit(s[i]) || s[i] == '.')) i++;
    double num = 1.0;
    if (i > 0) {
        try { num = std::stod(s.substr(0, i)); } catch (...) { num = 1.0; }
    }
    std::string unit = s.substr(i);
    while (!unit.empty() && std::isspace(unit.front())) unit.erase(unit.begin());
    while (!unit.empty() && std::isspace(unit.back())) unit.pop_back();
    double mult = 1e15; // default 1s
    if (unit == "s") mult = 1e15;
    else if (unit == "ms") mult = 1e12;
    else if (unit == "us") mult = 1e9;
    else if (unit == "ns") mult = 1e6;
    else if (unit == "ps") mult = 1e3;
    else if (unit == "fs") mult = 1.0;
    return static_cast<uint64_t>(std::round(num * mult));
}

void Lexer::handle_timescale() {
    auto& cur = file_stack.back();
    while (cur.cursor < cur.src.size() && (cur.src[cur.cursor] == ' ' || cur.src[cur.cursor] == '\t')) {
        cur.cursor++;
    }
    std::string unit_str = "";
    while (cur.cursor < cur.src.size() && cur.src[cur.cursor] != '/' && cur.src[cur.cursor] != '\n' && cur.src[cur.cursor] != '\0') {
        if (!std::isspace(cur.src[cur.cursor])) unit_str += cur.src[cur.cursor];
        cur.cursor++;
    }
    if (cur.cursor < cur.src.size() && cur.src[cur.cursor] == '/') {
        cur.cursor++;
    }
    while (cur.cursor < cur.src.size() && (cur.src[cur.cursor] == ' ' || cur.src[cur.cursor] == '\t')) {
        cur.cursor++;
    }
    std::string prec_str = "";
    while (cur.cursor < cur.src.size() && cur.src[cur.cursor] != '\n' && cur.src[cur.cursor] != '\0') {
        if (!std::isspace(cur.src[cur.cursor])) prec_str += cur.src[cur.cursor];
        cur.cursor++;
    }
    if (!unit_str.empty() && !prec_str.empty()) {
        uint64_t unit_fs = parse_time_fs(unit_str);
        uint64_t prec_fs = parse_time_fs(prec_str);
        if (prec_fs > 0 && unit_fs >= prec_fs) {
            global_timescale_ratio = unit_fs / prec_fs;
        } else {
            global_timescale_ratio = 1;
        }
    }
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
            advance();
            std::string directive = "";
            while (std::isalpha(peek())) directive += advance();
            if (directive == "include") {
                handle_include();
            } else if (directive == "timescale") {
                handle_timescale();
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
    if (file_stack.empty()) return {TokenType::TOKEN_EOF, "", 0, 0, "", 0, 0};

    const std::string cur_file = file_stack.back().filename;
    const size_t cur_line = file_stack.back().line;
    char c = peek();

    // Delimiters
    if (c == '(') { advance(); return {TokenType::TOKEN_LPAREN, "(", 0, 0, cur_file, cur_line, 0}; }
    if (c == ')') { advance(); return {TokenType::TOKEN_RPAREN, ")", 0, 0, cur_file, cur_line, 0}; }
    if (c == '[') {
        advance();
        if (peek() == '*') {
            advance();
            return {TokenType::TOKEN_REP_CONSEC, "[*", 0, 0, cur_file, cur_line, 0};
        }
        if (peek() == '=') {
            advance();
            return {TokenType::TOKEN_REP_NON_CONSEC, "[=", 0, 0, cur_file, cur_line, 0};
        }
        if (peek() == '-' && peek_next() == '>') {
            advance(); advance();
            return {TokenType::TOKEN_REP_GOTO, "[->", 0, 0, cur_file, cur_line, 0};
        }
        return {TokenType::TOKEN_LBRACKET, "[", 0, 0, cur_file, cur_line, 0};
    }
    if (c == ']') { advance(); return {TokenType::TOKEN_RBRACKET, "]", 0, 0, cur_file, cur_line, 0}; }
    if (c == '{') { advance(); return {TokenType::TOKEN_LBRACE, "{", 0, 0, cur_file, cur_line, 0}; }
    if (c == '}') { advance(); return {TokenType::TOKEN_RBRACE, "}", 0, 0, cur_file, cur_line, 0}; }
    if (c == ';') { advance(); return {TokenType::TOKEN_SEMICOLON, ";", 0, 0, cur_file, cur_line, 0}; }
    if (c == ',') { advance(); return {TokenType::TOKEN_COMMA, ",", 0, 0, cur_file, cur_line, 0}; }
    if (c == '.') { advance(); return {TokenType::TOKEN_DOT, ".", 0, 0, cur_file, cur_line, 0}; }
    if (c == '#') {
        advance();
        if (peek() == '#') {
            advance();
            return {TokenType::TOKEN_CYCLE_DELAY, "##", 0, 0, cur_file, cur_line, 0};
        }
        return {TokenType::TOKEN_HASH, "#", 0, 0, cur_file, cur_line, 0};
    }
    if (c == '@') { advance(); return {TokenType::TOKEN_AT, "@", 0, 0, cur_file, cur_line, 0}; }
    if (c == '?') { advance(); return {TokenType::TOKEN_QUESTION, "?", 0, 0, cur_file, cur_line, 0}; }
    if (c == ':') {
        advance();
        if (peek() == ':') {
            advance();
            return {TokenType::TOKEN_COLON_COLON, "::", 0, 0, cur_file, cur_line, 0};
        }
        return {TokenType::TOKEN_COLON, ":", 0, 0, cur_file, cur_line, 0};
    }
    if (c == '~') { advance(); return {TokenType::TOKEN_TILDE, "~", 0, 0, cur_file, cur_line, 0}; }
    if (c == '^') {
        advance();
        if (peek() == '=') {
            advance();
            return {TokenType::TOKEN_XOR_EQUAL, "^=", 0, 0, cur_file, cur_line, 0};
        }
        return {TokenType::TOKEN_XOR, "^", 0, 0, cur_file, cur_line, 0};
    }
    if (c == '+') {
        advance();
        if (peek() == '+') {
            advance();
            return {TokenType::TOKEN_PLUS_PLUS, "++", 0, 0, cur_file, cur_line, 0};
        }
        if (peek() == '=') {
            advance();
            return {TokenType::TOKEN_PLUS_EQUAL, "+=", 0, 0, cur_file, cur_line, 0};
        }
        return {TokenType::TOKEN_PLUS, "+", 0, 0, cur_file, cur_line, 0};
    }
    if (c == '-') {
        advance();
        if (peek() == '-') {
            advance();
            return {TokenType::TOKEN_MINUS_MINUS, "--", 0, 0, cur_file, cur_line, 0};
        }
        if (peek() == '=') {
            advance();
            return {TokenType::TOKEN_MINUS_EQUAL, "-=", 0, 0, cur_file, cur_line, 0};
        }
        if (peek() == '>') {
            advance();
            if (peek() == '>') {
                advance();
                return {TokenType::TOKEN_NONBLOCKING_TRIGGER, "->>", 0, 0, cur_file, cur_line, 0};
            }
            return {TokenType::TOKEN_TRIGGER, "->", 0, 0, cur_file, cur_line, 0};
        }
        return {TokenType::TOKEN_MINUS, "-", 0, 0, cur_file, cur_line, 0};
    }
    if (c == '*') {
        advance();
        if (peek() == '>') { advance(); return {TokenType::TOKEN_STAR_ARROW, "*>", 0, 0, cur_file, cur_line, 0}; }
        if (peek() == '=') { advance(); return {TokenType::TOKEN_STAR_EQUAL, "*=", 0, 0, cur_file, cur_line, 0}; }
        return {TokenType::TOKEN_STAR, "*", 0, 0, cur_file, cur_line, 0};
    }

    // Multi-character Operators
    if (c == '&') {
        advance();
        if (peek() == '&') { advance(); return {TokenType::TOKEN_LOGICAL_AND, "&&", 0, 0, cur_file, cur_line, 0}; }
        if (peek() == '=') { advance(); return {TokenType::TOKEN_AND_EQUAL, "&=", 0, 0, cur_file, cur_line, 0}; }
        return {TokenType::TOKEN_AND, "&", 0, 0, cur_file, cur_line, 0};
    }
    if (c == '|') {
        advance();
        if (peek() == '-' && peek_next() == '>') {
            advance(); advance();
            return {TokenType::TOKEN_OVERLAP_IMP, "|->", 0, 0, cur_file, cur_line, 0};
        }
        if (peek() == '=' && peek_next() == '>') {
            advance(); advance();
            return {TokenType::TOKEN_NON_OVERLAP_IMP, "|=>", 0, 0, cur_file, cur_line, 0};
        }
        if (peek() == '=') { advance(); return {TokenType::TOKEN_OR_EQUAL, "|=", 0, 0, cur_file, cur_line, 0}; }
        if (peek() == '|') { advance(); return {TokenType::TOKEN_LOGICAL_OR, "||", 0, 0, cur_file, cur_line, 0}; }
        return {TokenType::TOKEN_OR_BIT, "|", 0, 0, cur_file, cur_line, 0};
    }
    if (c == '!') {
        advance();
        if (peek() == '=') {
            advance();
            if (peek() == '=') {
                advance();
                return {TokenType::TOKEN_CASE_NEQ, "!==", 0, 0, cur_file, cur_line, 0};
            }
            return {TokenType::TOKEN_NEQ, "!=", 0, 0, cur_file, cur_line, 0};
        }
        return {TokenType::TOKEN_NOT, "!", 0, 0, cur_file, cur_line, 0};
    }
    if (c == '=') {
        advance();
        if (peek() == '=') {
            advance();
            if (peek() == '=') {
                advance();
                return {TokenType::TOKEN_CASE_EQ, "===", 0, 0, cur_file, cur_line, 0};
            }
            return {TokenType::TOKEN_EQ, "==", 0, 0, cur_file, cur_line, 0};
        }
        if (peek() == '>') {
            advance();
            return {TokenType::TOKEN_FAT_ARROW, "=>", 0, 0, cur_file, cur_line, 0};
        }
        return {TokenType::TOKEN_EQUAL, "=", 0, 0, cur_file, cur_line, 0};
    }
    if (c == '<') {
        advance();
        if (peek() == '=') { advance(); return {TokenType::TOKEN_LE, "<=", 0, 0, cur_file, cur_line, 0}; }
        if (peek() == '<') { advance(); return {TokenType::TOKEN_SHL, "<<", 0, 0, cur_file, cur_line, 0}; }
        return {TokenType::TOKEN_LT, "<", 0, 0, cur_file, cur_line, 0};
    }
    if (c == '>') {
        advance();
        if (peek() == '=') { advance(); return {TokenType::TOKEN_GE, ">=", 0, 0, cur_file, cur_line, 0}; }
        if (peek() == '>') {
            advance();
            if (peek() == '>') {
                advance();
                return {TokenType::TOKEN_ASHR, ">>>", 0, 0, cur_file, cur_line, 0};
            }
            return {TokenType::TOKEN_SHR, ">>", 0, 0, cur_file, cur_line, 0};
        }
        return {TokenType::TOKEN_GT, ">", 0, 0, cur_file, cur_line, 0};
    }

    // Unbased literals: '0, '1, 'x, 'z
    if (c == '\'') {
        if (peek_next() == '0' || peek_next() == '1' || 
            peek_next() == 'x' || peek_next() == 'X' || 
            peek_next() == 'z' || peek_next() == 'Z') {
            advance();
            char lit = advance();
            auto pnum = NumberParser::parse_unbased_literal(lit);
            return {TokenType::TOKEN_NUMBER, pnum.text, pnum.aval, pnum.width, cur_file, cur_line, pnum.bval};
        }
        advance();
        return {TokenType::TOKEN_APOSTROPHE, "'", 0, 0, cur_file, cur_line, 0};
    }

    // Numeric Literals
    if (std::isdigit(c)) {
        std::string num_str;
        while (std::isdigit(peek())) num_str += advance();

        if (peek() == '.' && std::isdigit(peek_next())) {
            num_str += advance();
            while (std::isdigit(peek())) num_str += advance();
            double dval = std::stod(num_str);
            uint64_t aval = static_cast<uint64_t>(dval);
            Token tok{TokenType::TOKEN_NUMBER, num_str, aval, 64, cur_file, cur_line, 0, true, dval};
            return tok;
        }

        if (std::isalpha(peek()) || peek() == '_') {
            std::string alnum_part = "";
            while (std::isalnum(peek()) || peek() == '_') alnum_part += advance();
            if (num_str == "1" && alnum_part == "step") {
                return {TokenType::TOKEN_1STEP, "1step", 0, 0, cur_file, cur_line, 0};
            }
            num_str += alnum_part;
        }

        if (peek() == '\'') {
            uint32_t width = std::stoul(num_str);
            advance();
            bool is_signed = false;
            if (peek() == 's' || peek() == 'S') {
                is_signed = true;
                advance();
            }
            char base = advance();
            std::string digits;
            while (std::isxdigit(peek()) || peek() == '_' || peek() == 'x' || peek() == 'X' || peek() == 'z' || peek() == 'Z' || peek() == '?') {
                if (peek() != '_') digits += peek();
                advance();
            }

            auto pnum = NumberParser::parse_sized_literal(width, is_signed, base, digits);
            return {TokenType::TOKEN_NUMBER, pnum.text, pnum.aval, pnum.width, cur_file, cur_line, pnum.bval};
        }

        auto pnum = NumberParser::parse_simple_decimal(num_str);
        return {TokenType::TOKEN_NUMBER, pnum.text, pnum.aval, pnum.width, cur_file, cur_line, pnum.bval};
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
        return {TokenType::TOKEN_STRING, s, 0, 0, cur_file, cur_line, 0};
    }

    // Identifiers and Keywords
    if (std::isalpha(c) || c == '_' || c == '$') {
        std::string ident;
        while (std::isalnum(peek()) || peek() == '_' || peek() == '$') {
            ident += advance();
        }

        if (ident == "module") return {TokenType::TOKEN_MODULE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "endmodule") return {TokenType::TOKEN_ENDMODULE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "input") return {TokenType::TOKEN_INPUT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "output") return {TokenType::TOKEN_OUTPUT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "inout") return {TokenType::TOKEN_INOUT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "ref") return {TokenType::TOKEN_REF, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "logic") return {TokenType::TOKEN_LOGIC, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "wire") return {TokenType::TOKEN_WIRE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "wand") return {TokenType::TOKEN_WAND, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "wor") return {TokenType::TOKEN_WOR, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "tri") return {TokenType::TOKEN_TRI, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "reg") return {TokenType::TOKEN_REG, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "integer") return {TokenType::TOKEN_INTEGER, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "signed") return {TokenType::TOKEN_SIGNED, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "parameter") return {TokenType::TOKEN_PARAMETER, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "localparam") return {TokenType::TOKEN_LOCALPARAM, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "defparam") return {TokenType::TOKEN_DEFPARAM, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "assign") return {TokenType::TOKEN_ASSIGN, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "deassign") return {TokenType::TOKEN_DEASSIGN, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "always") return {TokenType::TOKEN_ALWAYS, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "always_comb") return {TokenType::TOKEN_ALWAYS_COMB, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "always_ff") return {TokenType::TOKEN_ALWAYS_FF, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "always_latch") return {TokenType::TOKEN_ALWAYS_LATCH, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "task") return {TokenType::TOKEN_TASK, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "endtask") return {TokenType::TOKEN_ENDTASK, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "function") return {TokenType::TOKEN_FUNCTION, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "endfunction") return {TokenType::TOKEN_ENDFUNCTION, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "automatic") return {TokenType::TOKEN_AUTOMATIC, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "static") return {TokenType::TOKEN_STATIC, ident, 0, 0, cur_file, cur_line, 0};

        if (ident == "for") return {TokenType::TOKEN_FOR, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "foreach") return {TokenType::TOKEN_FOREACH, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "while") return {TokenType::TOKEN_WHILE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "repeat") return {TokenType::TOKEN_REPEAT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "forever") return {TokenType::TOKEN_FOREVER, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "fork") return {TokenType::TOKEN_FORK, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "join") return {TokenType::TOKEN_JOIN, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "join_any") return {TokenType::TOKEN_JOIN_ANY, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "join_none") return {TokenType::TOKEN_JOIN_NONE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "disable") return {TokenType::TOKEN_DISABLE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "wait") return {TokenType::TOKEN_WAIT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "event") return {TokenType::TOKEN_EVENT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "typedef") return {TokenType::TOKEN_TYPEDEF, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "enum") return {TokenType::TOKEN_ENUM, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "struct") return {TokenType::TOKEN_STRUCT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "packed") return {TokenType::TOKEN_PACKED, ident, 0, 0, cur_file, cur_line, 0};

        if (ident == "or") return {TokenType::TOKEN_GATE_OR, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "and") return {TokenType::TOKEN_GATE_AND, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "xor") return {TokenType::TOKEN_GATE_XOR, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "nand") return {TokenType::TOKEN_GATE_NAND, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "nor") return {TokenType::TOKEN_GATE_NOR, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "xnor") return {TokenType::TOKEN_GATE_XNOR, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "not") return {TokenType::TOKEN_GATE_NOT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "buf") return {TokenType::TOKEN_GATE_BUF, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "bufif0") return {TokenType::TOKEN_GATE_BUFIF0, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "bufif1") return {TokenType::TOKEN_GATE_BUFIF1, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "notif0") return {TokenType::TOKEN_GATE_NOTIF0, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "notif1") return {TokenType::TOKEN_GATE_NOTIF1, ident, 0, 0, cur_file, cur_line, 0};

        if (ident == "posedge") return {TokenType::TOKEN_POSEDGE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "negedge") return {TokenType::TOKEN_NEGEDGE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "if") return {TokenType::TOKEN_IF, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "else") return {TokenType::TOKEN_ELSE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "case") return {TokenType::TOKEN_CASE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "casez") return {TokenType::TOKEN_CASEZ, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "casex") return {TokenType::TOKEN_CASEX, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "endcase") return {TokenType::TOKEN_ENDCASE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "default") return {TokenType::TOKEN_DEFAULT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "begin") return {TokenType::TOKEN_BEGIN, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "end") return {TokenType::TOKEN_END, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "initial") return {TokenType::TOKEN_INITIAL, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$display") return {TokenType::TOKEN_DISPLAY, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$monitor") return {TokenType::TOKEN_MONITOR, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$finish") return {TokenType::TOKEN_FINISH, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$stop") return {TokenType::TOKEN_STOP, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$exit") return {TokenType::TOKEN_EXIT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$dumpfile") return {TokenType::TOKEN_DUMPFILE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$dumpvars") return {TokenType::TOKEN_DUMPVARS, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$") return {TokenType::TOKEN_DOLLAR, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "new") return {TokenType::TOKEN_NEW, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "interface") return {TokenType::TOKEN_INTERFACE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "endinterface") return {TokenType::TOKEN_ENDINTERFACE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "modport") return {TokenType::TOKEN_MODPORT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "clocking") return {TokenType::TOKEN_CLOCKING, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "endclocking") return {TokenType::TOKEN_ENDCLOCKING, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "1step") return {TokenType::TOKEN_1STEP, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "class") return {TokenType::TOKEN_CLASS, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "endclass") return {TokenType::TOKEN_ENDCLASS, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "null") return {TokenType::TOKEN_NULL, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "this") return {TokenType::TOKEN_THIS, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "bit") return {TokenType::TOKEN_BIT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "return") return {TokenType::TOKEN_RETURN, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "extends") return {TokenType::TOKEN_EXTENDS, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "implements") return {TokenType::TOKEN_IMPLEMENTS, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "super") return {TokenType::TOKEN_SUPER, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "virtual") return {TokenType::TOKEN_VIRTUAL, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$cast") return {TokenType::TOKEN_CAST, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "rand") return {TokenType::TOKEN_RAND, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "randc") return {TokenType::TOKEN_RANDC, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "constraint") return {TokenType::TOKEN_CONSTRAINT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "with") return {TokenType::TOKEN_WITH, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "inside") return {TokenType::TOKEN_INSIDE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "covergroup") return {TokenType::TOKEN_COVERGROUP, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "endgroup") return {TokenType::TOKEN_ENDGROUP, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "coverpoint") return {TokenType::TOKEN_COVERPOINT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "bins") return {TokenType::TOKEN_BINS, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "cross") return {TokenType::TOKEN_CROSS, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "real") return {TokenType::TOKEN_REAL, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "property") return {TokenType::TOKEN_PROPERTY, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "endproperty") return {TokenType::TOKEN_ENDPROPERTY, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "sequence") return {TokenType::TOKEN_SEQUENCE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "endsequence") return {TokenType::TOKEN_ENDSEQUENCE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "assert") return {TokenType::TOKEN_ASSERT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "iff") return {TokenType::TOKEN_IFF, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$rose") return {TokenType::TOKEN_ROSE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$fell") return {TokenType::TOKEN_FELL, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$stable") return {TokenType::TOKEN_STABLE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$past") return {TokenType::TOKEN_PAST, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "import") return {TokenType::TOKEN_IMPORT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "export") return {TokenType::TOKEN_EXPORT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "pure") return {TokenType::TOKEN_PURE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "context") return {TokenType::TOKEN_CONTEXT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "void") return {TokenType::TOKEN_VOID, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "chandle") return {TokenType::TOKEN_CHANDLE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "semaphore") return {TokenType::TOKEN_SEMAPHORE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "mailbox") return {TokenType::TOKEN_MAILBOX, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "package") return {TokenType::TOKEN_PACKAGE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "endpackage") return {TokenType::TOKEN_ENDPACKAGE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "config") return {TokenType::TOKEN_CONFIG, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "endconfig") return {TokenType::TOKEN_ENDCONFIG, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "design") return {TokenType::TOKEN_DESIGN, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "liblist") return {TokenType::TOKEN_LIBLIST, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "instance") return {TokenType::TOKEN_INSTANCE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "cell") return {TokenType::TOKEN_CELL, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "use") return {TokenType::TOKEN_USE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "program") return {TokenType::TOKEN_PROGRAM, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "endprogram") return {TokenType::TOKEN_ENDPROGRAM, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "alias") return {TokenType::TOKEN_ALIAS, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "primitive") return {TokenType::TOKEN_PRIMITIVE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "endprimitive") return {TokenType::TOKEN_ENDPRIMITIVE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "table") return {TokenType::TOKEN_TABLE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "endtable") return {TokenType::TOKEN_ENDTABLE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "specify") return {TokenType::TOKEN_SPECIFY, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "endspecify") return {TokenType::TOKEN_ENDSPECIFY, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "ifnone") return {TokenType::TOKEN_IFNONE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$setup") return {TokenType::TOKEN_SYS_SETUP, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$hold") return {TokenType::TOKEN_SYS_HOLD, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$width") return {TokenType::TOKEN_SYS_WIDTH, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$recovery") return {TokenType::TOKEN_SYS_RECOVERY, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$dimensions") return {TokenType::TOKEN_SYS_DIMENSIONS, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$unpacked_dimensions") return {TokenType::TOKEN_SYS_UNPACKED_DIMENSIONS, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$left") return {TokenType::TOKEN_SYS_LEFT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$right") return {TokenType::TOKEN_SYS_RIGHT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$low") return {TokenType::TOKEN_SYS_LOW, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$high") return {TokenType::TOKEN_SYS_HIGH, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$size") return {TokenType::TOKEN_SYS_SIZE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "$increment") return {TokenType::TOKEN_SYS_INCREMENT, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "generate") return {TokenType::TOKEN_GENERATE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "endgenerate") return {TokenType::TOKEN_ENDGENERATE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "genvar") return {TokenType::TOKEN_GENVAR, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "unique") return {TokenType::TOKEN_UNIQUE, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "unique0") return {TokenType::TOKEN_UNIQUE0, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "priority") return {TokenType::TOKEN_PRIORITY, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "let") return {TokenType::TOKEN_LET, ident, 0, 0, cur_file, cur_line, 0};
        if (ident == "untyped") return {TokenType::TOKEN_UNTYPED, ident, 0, 0, cur_file, cur_line, 0};

        return {TokenType::TOKEN_IDENTIFIER, ident, 0, 0, cur_file, cur_line, 0};
    }

    throw std::runtime_error(cur_file + ":" + std::to_string(cur_line) + ": error: Unknown lexical token '" + c + "'");
}