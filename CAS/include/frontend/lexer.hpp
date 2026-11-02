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
    TOKEN_REF,
    TOKEN_LOGIC,
    TOKEN_WIRE,
    TOKEN_WAND,
    TOKEN_WOR,
    TOKEN_TRI,
    TOKEN_REG,
    TOKEN_INTEGER,
    TOKEN_SIGNED,
    TOKEN_UNSIGNED,
    TOKEN_PARAMETER,
    TOKEN_LOCALPARAM,
    TOKEN_DEFPARAM,
    TOKEN_ASSIGN,
    TOKEN_DEASSIGN,
    TOKEN_ALWAYS,
    TOKEN_ALWAYS_COMB,
    TOKEN_ALWAYS_FF,
    TOKEN_ALWAYS_LATCH,
    TOKEN_TASK,
    TOKEN_ENDTASK,
    TOKEN_FUNCTION,
    TOKEN_ENDFUNCTION,
    TOKEN_AUTOMATIC,
    TOKEN_STATIC,
    TOKEN_FOR,
    TOKEN_WHILE,
    TOKEN_REPEAT,
    TOKEN_FOREVER,
    TOKEN_FORK,
    TOKEN_JOIN,
    TOKEN_JOIN_ANY,
    TOKEN_JOIN_NONE,
    TOKEN_DISABLE,
    TOKEN_WAIT,
    TOKEN_EVENT,
    TOKEN_TRIGGER,
    TOKEN_NONBLOCKING_TRIGGER,
    TOKEN_TYPEDEF,
    TOKEN_ENUM,
    TOKEN_STRUCT,
    TOKEN_PACKED,
    TOKEN_GATE_AND,
    TOKEN_GATE_OR,
    TOKEN_GATE_XOR,
    TOKEN_GATE_NAND,
    TOKEN_GATE_NOR,
    TOKEN_GATE_XNOR,
    TOKEN_GATE_NOT,
    TOKEN_GATE_BUF,
    TOKEN_GATE_BUFIF0,
    TOKEN_GATE_BUFIF1,
    TOKEN_GATE_NOTIF0,
    TOKEN_GATE_NOTIF1,
    TOKEN_AT,
    TOKEN_POSEDGE,
    TOKEN_NEGEDGE,
    TOKEN_OR,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_CASE,
    TOKEN_CASEZ,
    TOKEN_CASEX,
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
    TOKEN_FATAL,
    TOKEN_ERROR,
    TOKEN_WARNING,
    TOKEN_INFO,
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
    TOKEN_ASHR,
    TOKEN_LOGICAL_AND,
    TOKEN_LOGICAL_OR,
    TOKEN_QUESTION,
    TOKEN_COLON,
    TOKEN_COLON_COLON,
    TOKEN_APOSTROPHE,
    TOKEN_DOLLAR,
    TOKEN_NEW,
    TOKEN_INTERFACE,
    TOKEN_ENDINTERFACE,
    TOKEN_MODPORT,
    TOKEN_CLOCKING,
    TOKEN_ENDCLOCKING,
    TOKEN_CYCLE_DELAY,
    TOKEN_1STEP,
    TOKEN_CLASS,
    TOKEN_ENDCLASS,
    TOKEN_NULL,
    TOKEN_THIS,
    TOKEN_BIT,
    TOKEN_RETURN,
    TOKEN_EXTENDS,
    TOKEN_IMPLEMENTS,
    TOKEN_SUPER,
    TOKEN_VIRTUAL,
    TOKEN_EXTERN,
    TOKEN_CAST,
    TOKEN_RAND,
    TOKEN_RANDC,
    TOKEN_CONSTRAINT,
    TOKEN_WITH,
    TOKEN_INSIDE,
    TOKEN_COVERGROUP,
    TOKEN_ENDGROUP,
    TOKEN_COVERPOINT,
    TOKEN_BINS,
    TOKEN_CROSS,
    TOKEN_REAL,
    TOKEN_PROPERTY,
    TOKEN_ENDPROPERTY,
    TOKEN_SEQUENCE,
    TOKEN_ENDSEQUENCE,
    TOKEN_ASSERT,
    TOKEN_IFF,
    TOKEN_OVERLAP_IMP,
    TOKEN_NON_OVERLAP_IMP,
    TOKEN_ROSE,
    TOKEN_FELL,
    TOKEN_STABLE,
    TOKEN_PAST,
    TOKEN_REP_CONSEC,
    TOKEN_REP_NON_CONSEC,
    TOKEN_REP_GOTO,
    TOKEN_IMPORT,
    TOKEN_EXPORT,
    TOKEN_PURE,
    TOKEN_CONTEXT,
    TOKEN_VOID,
    TOKEN_CHANDLE,
    TOKEN_SEMAPHORE,
    TOKEN_MAILBOX,
    TOKEN_PACKAGE,
    TOKEN_ENDPACKAGE,
    TOKEN_CONFIG,
    TOKEN_ENDCONFIG,
    TOKEN_DESIGN,
    TOKEN_LIBLIST,
    TOKEN_INSTANCE,
    TOKEN_CELL,
    TOKEN_USE,
    TOKEN_PROGRAM,
    TOKEN_ENDPROGRAM,
    TOKEN_EXIT,
    TOKEN_PLUS_PLUS,
    TOKEN_MINUS_MINUS,
    TOKEN_PLUS_EQUAL,
    TOKEN_MINUS_EQUAL,
    TOKEN_STAR_EQUAL,
    TOKEN_AND_EQUAL,
    TOKEN_OR_EQUAL,
    TOKEN_XOR_EQUAL,
    TOKEN_ALIAS,
    TOKEN_PRIMITIVE,
    TOKEN_ENDPRIMITIVE,
    TOKEN_TABLE,
    TOKEN_ENDTABLE,
    TOKEN_SPECIFY,
    TOKEN_ENDSPECIFY,
    TOKEN_FAT_ARROW,
    TOKEN_STAR_ARROW,
    TOKEN_IFNONE,
    TOKEN_SYS_SETUP,
    TOKEN_SYS_HOLD,
    TOKEN_SYS_WIDTH,
    TOKEN_SYS_RECOVERY,
    TOKEN_FOREACH,
    TOKEN_SYS_DIMENSIONS,
    TOKEN_SYS_UNPACKED_DIMENSIONS,
    TOKEN_SYS_LEFT,
    TOKEN_SYS_RIGHT,
    TOKEN_SYS_LOW,
    TOKEN_SYS_HIGH,
    TOKEN_SYS_SIZE,
    TOKEN_SYS_INCREMENT,
    TOKEN_SYS_TYPENAME,
    TOKEN_GENERATE,
    TOKEN_ENDGENERATE,
    TOKEN_GENVAR,
    TOKEN_UNIQUE,
    TOKEN_UNIQUE0,
    TOKEN_PRIORITY,
    TOKEN_LET,
    TOKEN_UNTYPED,
    TOKEN_TYPE
};

struct Token {
    TokenType type;
    std::string text;
    uint64_t num_val;
    uint32_t width;
    std::string filename;
    size_t line;
    uint64_t bval = 0;
    bool is_real = false;
    double real_val = 0.0;
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
    void handle_timescale();

public:
    static uint64_t global_timescale_ratio;
    static std::string global_time_unit_str;
    static std::string global_time_precision_str;
    static std::string global_time_precision_unit;
    static uint64_t get_timescale_ratio() { return global_timescale_ratio; }
    static const std::string& get_time_precision_unit() { return global_time_precision_unit; }
    static void reset_timescale() {
        global_timescale_ratio = 1;
        global_time_unit_str = "1ns";
        global_time_precision_str = "1ps";
        global_time_precision_unit = "ps";
    }

    explicit Lexer(const std::string& filename, const std::string& source, 
                   const std::string& base_dir = ".", std::ostream* debug_stream = nullptr);
    Token next_token();
    std::string get_current_location() const;
};
