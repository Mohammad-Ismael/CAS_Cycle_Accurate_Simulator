#include "../include/parser.hpp"
#include <stdexcept>
#include <iostream>

Parser::Parser(Lexer& lex) : lexer(lex) {
    advance();
}

void Parser::advance() {
    current = lexer.next_token();
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
        throw std::runtime_error(err + " on line " + std::to_string(current.line) + " (found: '" + current.text + "')");
    }
    advance();
}

std::unique_ptr<ExpressionNode> Parser::parse_primary() {
    if (current.type == TokenType::TOKEN_NUMBER) {
        auto node = std::make_unique<NumberNode>(current.num_val, current.bit_width);
        advance();
        return node;
    }
    if (current.type == TokenType::TOKEN_IDENTIFIER) {
        auto node = std::make_unique<IdentifierNode>(current.text);
        advance();
        return node;
    }
    throw std::runtime_error("Unexpected token in expression evaluation on line " + std::to_string(current.line) + " (token: '" + current.text + "')");
}

std::unique_ptr<ExpressionNode> Parser::parse_expression() {
    auto lhs = parse_primary();
    if (current.type == TokenType::TOKEN_PLUS || current.type == TokenType::TOKEN_MINUS) {
        char op = (current.type == TokenType::TOKEN_PLUS) ? '+' : '-';
        advance();
        auto rhs = parse_primary();
        return std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<StatementNode> Parser::parse_statement() {
    if (match(TokenType::TOKEN_DISPLAY)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after $display");
        std::string fmt = current.text;
        expect(TokenType::TOKEN_STRING, "Expected format string for $display");

        std::vector<std::string> args;
        while (match(TokenType::TOKEN_COMMA)) {
            args.push_back(current.text);
            expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier in $display argument list");
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' after $display parameters");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $display statement");
        return std::make_unique<DisplayStatementNode>(fmt, args);
    }

    if (match(TokenType::TOKEN_FINISH)) {
        if (match(TokenType::TOKEN_LPAREN)) expect(TokenType::TOKEN_RPAREN, "Expected ')'");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $finish");
        return std::make_unique<FinishStatementNode>();
    }

    // Assignment: target = expr; OR {c, s} = expr;
    std::vector<std::string> targets;
    if (match(TokenType::TOKEN_LBRACE)) {
        while (true) {
            targets.push_back(current.text);
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier inside concatenation");
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_RBRACE, "Expected '}' in concatenation assignment");
    } else {
        targets.push_back(current.text);
        expect(TokenType::TOKEN_IDENTIFIER, "Expected target identifier in assignment");
    }

    expect(TokenType::TOKEN_EQUAL, "Expected '=' in blocking assignment");
    auto expr = parse_expression();
    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' terminating statement");
    return std::make_unique<BlockingAssignNode>(targets, std::move(expr));
}

std::vector<std::unique_ptr<ModuleNode>> Parser::parse_all() {
    std::vector<std::unique_ptr<ModuleNode>> modules;

    while (current.type != TokenType::TOKEN_EOF) {
        expect(TokenType::TOKEN_MODULE, "Expected 'module' keyword");
        auto mod = std::make_unique<ModuleNode>();
        mod->name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected module name");

        // Ports List
        if (match(TokenType::TOKEN_LPAREN)) {
            while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                PortNode port;
                if (current.type == TokenType::TOKEN_INPUT) {
                    port.direction = "input";
                    advance();
                } else if (current.type == TokenType::TOKEN_OUTPUT) {
                    port.direction = "output";
                    advance();
                }
                match(TokenType::TOKEN_LOGIC);

                port.msb = 0; port.lsb = 0;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    port.msb = current.num_val;
                    advance();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in bus width");
                    port.lsb = current.num_val;
                    advance();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in bus width");
                }

                port.name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected port name");
                mod->ports.push_back(port);

                if (!match(TokenType::TOKEN_COMMA)) break;
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing port list");
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after module header");

        // Module Body
        while (current.type != TokenType::TOKEN_ENDMODULE && current.type != TokenType::TOKEN_EOF) {
            if (match(TokenType::TOKEN_LOGIC)) {
                PortNode sig;
                sig.direction = "internal";
                sig.msb = 0; sig.lsb = 0;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    sig.msb = current.num_val;
                    advance();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in bus width");
                    sig.lsb = current.num_val;
                    advance();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in bus width");
                }
                sig.name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected signal name");
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';'");
                mod->signals.push_back(sig);
            }
            else if (current.type == TokenType::TOKEN_ALWAYS_COMB || current.type == TokenType::TOKEN_INITIAL) {
                bool is_comb = (current.type == TokenType::TOKEN_ALWAYS_COMB);
                advance(); // Consume always_comb or initial
                
                auto proc = std::make_unique<ProcessNode>(is_comb);
                expect(TokenType::TOKEN_BEGIN, "Expected 'begin' block");
                while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
                    proc->body.push_back(parse_statement());
                }
                expect(TokenType::TOKEN_END, "Expected 'end'");
                mod->processes.push_back(std::move(proc));
            }
            else {
                advance(); // Gracefully skip unrecognized outer tokens
            }
        }
        expect(TokenType::TOKEN_ENDMODULE, "Expected 'endmodule'");
        modules.push_back(std::move(mod));
    }

    return modules;
}