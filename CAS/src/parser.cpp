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
    if (match(TokenType::TOKEN_NOT)) {
        auto operand = parse_primary();
        return std::make_unique<UnaryOpNode>('!', std::move(operand));
    }
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
    if (match(TokenType::TOKEN_LPAREN)) {
        auto expr = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing expression");
        return expr;
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
    // 1. Delay: #10;
    if (match(TokenType::TOKEN_HASH)) {
        if (current.type != TokenType::TOKEN_NUMBER) {
            throw std::runtime_error("Expected numeric delay value after '#' on line " + std::to_string(current.line));
        }
        uint64_t delay_val = current.num_val;
        advance();
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after delay statement");
        return std::make_unique<DelayStatementNode>(delay_val);
    }

    // 2. If-Else Construct
    if (match(TokenType::TOKEN_IF)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'if'");
        auto cond = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' after if-condition");

        auto if_stmt = std::make_unique<IfStatementNode>(std::move(cond));

        if (match(TokenType::TOKEN_BEGIN)) {
            while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
                if_stmt->then_branch.push_back(parse_statement());
            }
            expect(TokenType::TOKEN_END, "Expected 'end' closing if-block");
        } else {
            if_stmt->then_branch.push_back(parse_statement());
        }

        if (match(TokenType::TOKEN_ELSE)) {
            if (match(TokenType::TOKEN_BEGIN)) {
                while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
                    if_stmt->else_branch.push_back(parse_statement());
                }
                expect(TokenType::TOKEN_END, "Expected 'end' closing else-block");
            } else {
                if_stmt->else_branch.push_back(parse_statement());
            }
        }
        return if_stmt;
    }

    // 3. $display
    if (match(TokenType::TOKEN_DISPLAY)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after $display");
        std::string fmt = current.text;
        expect(TokenType::TOKEN_STRING, "Expected format string for $display");

        std::vector<std::string> args;
        while (match(TokenType::TOKEN_COMMA)) {
            args.push_back(current.text);
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier in $display argument list");
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' after $display parameters");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $display statement");
        return std::make_unique<DisplayStatementNode>(fmt, args);
    }

    // 4. $finish
    if (match(TokenType::TOKEN_FINISH)) {
        if (match(TokenType::TOKEN_LPAREN)) expect(TokenType::TOKEN_RPAREN, "Expected ')'");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $finish");
        return std::make_unique<FinishStatementNode>();
    }

    // 5. Assignments: Blocking (=) vs Non-blocking (<=)
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

    if (match(TokenType::TOKEN_LE)) {
        // Non-blocking assignment <=
        auto expr = parse_expression();
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' terminating non-blocking assignment");
        return std::make_unique<NonBlockingAssignNode>(targets[0], std::move(expr));
    } else {
        // Blocking assignment =
        expect(TokenType::TOKEN_EQUAL, "Expected '=' or '<=' in assignment");
        auto expr = parse_expression();
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' terminating blocking assignment");
        return std::make_unique<BlockingAssignNode>(targets, std::move(expr));
    }
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
            // always_comb
            else if (match(TokenType::TOKEN_ALWAYS_COMB)) {
                auto proc = std::make_unique<ProcessNode>(ProcessType::COMBINATIONAL);
                expect(TokenType::TOKEN_BEGIN, "Expected 'begin' block");
                while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
                    proc->body.push_back(parse_statement());
                }
                expect(TokenType::TOKEN_END, "Expected 'end'");
                mod->processes.push_back(std::move(proc));
            }
            // always_ff @(...)
            else if (match(TokenType::TOKEN_ALWAYS_FF)) {
                auto proc = std::make_unique<ProcessNode>(ProcessType::SEQUENTIAL);
                expect(TokenType::TOKEN_AT, "Expected '@' in always_ff");
                expect(TokenType::TOKEN_LPAREN, "Expected '(' in sensitivity list");

                while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                    EdgeType edge = EdgeType::POSEDGE;
                    if (match(TokenType::TOKEN_POSEDGE)) {
                        edge = EdgeType::POSEDGE;
                    } else if (match(TokenType::TOKEN_NEGEDGE)) {
                        edge = EdgeType::NEGEDGE;
                    }
                    std::string sig = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected signal name in sensitivity list");
                    proc->sensitivity_list.push_back({edge, sig});

                    match(TokenType::TOKEN_OR); // optional 'or' or comma
                }
                expect(TokenType::TOKEN_RPAREN, "Expected ')' closing sensitivity list");
                expect(TokenType::TOKEN_BEGIN, "Expected 'begin' block");
                while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
                    proc->body.push_back(parse_statement());
                }
                expect(TokenType::TOKEN_END, "Expected 'end'");
                mod->processes.push_back(std::move(proc));
            }
            // initial
            else if (match(TokenType::TOKEN_INITIAL)) {
                auto proc = std::make_unique<ProcessNode>(ProcessType::INITIAL);
                expect(TokenType::TOKEN_BEGIN, "Expected 'begin' block");
                while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
                    proc->body.push_back(parse_statement());
                }
                expect(TokenType::TOKEN_END, "Expected 'end'");
                mod->processes.push_back(std::move(proc));
            }
            // Instantiations: ModuleType InstanceName (.port(net), ...);
            else if (current.type == TokenType::TOKEN_IDENTIFIER) {
                std::string mod_type = current.text;
                advance();
                std::string inst_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected instance name");
                expect(TokenType::TOKEN_LPAREN, "Expected '(' for instance port map");

                auto inst = std::make_unique<InstantiationNode>(mod_type, inst_name);

                while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                    expect(TokenType::TOKEN_DOT, "Expected '.' before port name in port connection");
                    std::string p_name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected port name");
                    expect(TokenType::TOKEN_LPAREN, "Expected '(' after port name");
                    std::string conn_net = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected net connected to port");
                    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing port connection");

                    inst->connections.push_back({p_name, conn_net});
                    if (!match(TokenType::TOKEN_COMMA)) break;
                }

                expect(TokenType::TOKEN_RPAREN, "Expected ')' closing instantiation");
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after instantiation");
                mod->instantiations.push_back(std::move(inst));
            }
            else {
                advance();
            }
        }
        expect(TokenType::TOKEN_ENDMODULE, "Expected 'endmodule'");
        modules.push_back(std::move(mod));
    }

    return modules;
}