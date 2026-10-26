#pragma once
#include <string>
#include <vector>
#include <memory>

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
    TOKEN_OR,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_NOT,         // !
    TOKEN_BEGIN,
    TOKEN_END,
    TOKEN_INITIAL,
    TOKEN_DISPLAY,
    TOKEN_FINISH,
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
    TOKEN_LE,          // <= (Non-blocking assign)
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_COLON
};

enum class EdgeType {
    POSEDGE,
    NEGEDGE,
    LEVEL
};

struct SensitivityTarget {
    EdgeType edge;
    std::string signal_name;
};

struct ASTNode {
    virtual ~ASTNode() = default;
};

struct ExpressionNode : public ASTNode {
    virtual ~ExpressionNode() = default;
};

struct IdentifierNode : public ExpressionNode {
    std::string name;
    IdentifierNode(std::string n) : name(std::move(n)) {}
};

struct NumberNode : public ExpressionNode {
    uint64_t value;
    uint32_t width;
    NumberNode(uint64_t val, uint32_t w) : value(val), width(w) {}
};

struct UnaryOpNode : public ExpressionNode {
    char op; // '!'
    std::unique_ptr<ExpressionNode> operand;
    UnaryOpNode(char o, std::unique_ptr<ExpressionNode> opnd)
        : op(o), operand(std::move(opnd)) {}
};

struct BinaryOpNode : public ExpressionNode {
    char op; // '+', '-'
    std::unique_ptr<ExpressionNode> lhs;
    std::unique_ptr<ExpressionNode> rhs;
    BinaryOpNode(char o, std::unique_ptr<ExpressionNode> l, std::unique_ptr<ExpressionNode> r)
        : op(o), lhs(std::move(l)), rhs(std::move(r)) {}
};

struct StatementNode : public ASTNode {
    virtual ~StatementNode() = default;
};

struct BlockingAssignNode : public StatementNode {
    std::vector<std::string> lhs_targets;
    std::unique_ptr<ExpressionNode> rhs;
    BlockingAssignNode(std::vector<std::string> targets, std::unique_ptr<ExpressionNode> r)
        : lhs_targets(std::move(targets)), rhs(std::move(r)) {}
};

struct NonBlockingAssignNode : public StatementNode {
    std::string target;
    std::unique_ptr<ExpressionNode> rhs;
    NonBlockingAssignNode(std::string tgt, std::unique_ptr<ExpressionNode> r)
        : target(std::move(tgt)), rhs(std::move(r)) {}
};

struct IfStatementNode : public StatementNode {
    std::unique_ptr<ExpressionNode> condition;
    std::vector<std::unique_ptr<StatementNode>> then_branch;
    std::vector<std::unique_ptr<StatementNode>> else_branch;
    IfStatementNode(std::unique_ptr<ExpressionNode> cond) : condition(std::move(cond)) {}
};

struct DelayStatementNode : public StatementNode {
    uint64_t delay;
    explicit DelayStatementNode(uint64_t d) : delay(d) {}
};

struct DisplayStatementNode : public StatementNode {
    std::string format_string;
    std::vector<std::string> args;
    DisplayStatementNode(std::string fmt, std::vector<std::string> a)
        : format_string(std::move(fmt)), args(std::move(a)) {}
};

struct FinishStatementNode : public StatementNode {};

enum class ProcessType {
    COMBINATIONAL, // always_comb
    SEQUENTIAL,    // always_ff
    INITIAL        // initial
};

struct ProcessNode : public ASTNode {
    ProcessType type;
    std::vector<SensitivityTarget> sensitivity_list;
    std::vector<std::unique_ptr<StatementNode>> body;
    ProcessNode(ProcessType t) : type(t) {}
};

struct PortNode {
    std::string name;
    std::string direction;
    uint32_t msb;
    uint32_t lsb;
};

struct PortConnection {
    std::string port_name;
    std::string connected_net;
};

struct InstantiationNode : public ASTNode {
    std::string module_type;
    std::string instance_name;
    std::vector<PortConnection> connections;
    InstantiationNode(std::string type, std::string inst)
        : module_type(std::move(type)), instance_name(std::move(inst)) {}
};

struct ModuleNode : public ASTNode {
    std::string name;
    std::vector<PortNode> ports;
    std::vector<PortNode> signals;
    std::vector<std::unique_ptr<ProcessNode>> processes;
    std::vector<std::unique_ptr<InstantiationNode>> instantiations;
};