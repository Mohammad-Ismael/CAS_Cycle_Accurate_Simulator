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
    TOKEN_EQUAL,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_COLON
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

struct BinaryOpNode : public ExpressionNode {
    char op;
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

struct DisplayStatementNode : public StatementNode {
    std::string format_string;
    std::vector<std::string> args;
    DisplayStatementNode(std::string fmt, std::vector<std::string> a)
        : format_string(std::move(fmt)), args(std::move(a)) {}
};

struct FinishStatementNode : public StatementNode {};

struct ProcessNode : public ASTNode {
    bool is_combinational;
    std::vector<std::unique_ptr<StatementNode>> body;
    ProcessNode(bool comb) : is_combinational(comb) {}
};

struct PortNode {
    std::string name;
    std::string direction; // "input" or "output"
    uint32_t msb;
    uint32_t lsb;
};

struct PortConnection {
    std::string port_name;     // The port inside the child module (e.g. .a)
    std::string connected_net; // The wire in the parent module (e.g. (a))
};

struct InstantiationNode : public ASTNode {
    std::string module_type;   // e.g., adder
    std::string instance_name; // e.g., u_dut
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