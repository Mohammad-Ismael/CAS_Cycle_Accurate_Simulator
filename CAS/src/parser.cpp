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
        std::string ident_name = current.text;
        advance();

        // Bit-select, part-select, or array index
        if (match(TokenType::TOKEN_LBRACKET)) {
            auto first_expr = parse_expression();
            if (match(TokenType::TOKEN_COLON)) {
                auto second_expr = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' after part-select slice");
                return std::make_unique<IndexSelectNode>(ident_name, std::move(first_expr), std::move(second_expr));
            } else {
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' after index");
                return std::make_unique<IndexSelectNode>(ident_name, std::move(first_expr));
            }
        }
        return std::make_unique<IdentifierNode>(ident_name);
    }
    if (match(TokenType::TOKEN_LPAREN)) {
        auto expr = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing expression");
        return expr;
    }
    throw std::runtime_error("Unexpected token in expression evaluation on line " + std::to_string(current.line) + " (token: '" + current.text + "')");
}

std::unique_ptr<ExpressionNode> Parser::parse_unary() {
    if (match(TokenType::TOKEN_NOT)) {
        return std::make_unique<UnaryOpNode>('!', parse_unary());
    }
    if (match(TokenType::TOKEN_TILDE)) {
        return std::make_unique<UnaryOpNode>('~', parse_unary());
    }
    return parse_primary();
}

std::unique_ptr<ExpressionNode> Parser::parse_shift() {
    auto lhs = parse_unary();
    while (current.type == TokenType::TOKEN_SHL || current.type == TokenType::TOKEN_SHR) {
        std::string op = (current.type == TokenType::TOKEN_SHL) ? "<<" : ">>";
        advance();
        auto rhs = parse_unary();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_additive() {
    auto lhs = parse_shift();
    while (current.type == TokenType::TOKEN_PLUS || current.type == TokenType::TOKEN_MINUS) {
        std::string op = (current.type == TokenType::TOKEN_PLUS) ? "+" : "-";
        advance();
        auto rhs = parse_shift();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_relational() {
    auto lhs = parse_additive();
    while (current.type == TokenType::TOKEN_LT || current.type == TokenType::TOKEN_LE ||
           current.type == TokenType::TOKEN_GT || current.type == TokenType::TOKEN_GE) {
        std::string op;
        if (current.type == TokenType::TOKEN_LT) op = "<";
        else if (current.type == TokenType::TOKEN_LE) op = "<=";
        else if (current.type == TokenType::TOKEN_GT) op = ">";
        else if (current.type == TokenType::TOKEN_GE) op = ">=";
        advance();
        auto rhs = parse_additive();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_equality() {
    auto lhs = parse_relational();
    while (current.type == TokenType::TOKEN_EQ || current.type == TokenType::TOKEN_NEQ) {
        std::string op = (current.type == TokenType::TOKEN_EQ) ? "==" : "!=";
        advance();
        auto rhs = parse_relational();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_bitwise_and() {
    auto lhs = parse_equality();
    while (match(TokenType::TOKEN_AND)) {
        auto rhs = parse_equality();
        lhs = std::make_unique<BinaryOpNode>("&", std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_bitwise_xor() {
    auto lhs = parse_bitwise_and();
    while (match(TokenType::TOKEN_XOR)) {
        auto rhs = parse_bitwise_and();
        lhs = std::make_unique<BinaryOpNode>("^", std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_bitwise_or() {
    auto lhs = parse_bitwise_xor();
    while (match(TokenType::TOKEN_OR_BIT)) {
        auto rhs = parse_bitwise_xor();
        lhs = std::make_unique<BinaryOpNode>("|", std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_logical_and() {
    auto lhs = parse_bitwise_or();
    while (match(TokenType::TOKEN_LOGICAL_AND)) {
        auto rhs = parse_bitwise_or();
        lhs = std::make_unique<BinaryOpNode>("&&", std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_logical_or() {
    auto lhs = parse_logical_and();
    while (match(TokenType::TOKEN_LOGICAL_OR)) {
        auto rhs = parse_logical_and();
        lhs = std::make_unique<BinaryOpNode>("||", std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_ternary() {
    auto cond = parse_logical_or();
    if (match(TokenType::TOKEN_QUESTION)) {
        auto true_expr = parse_expression();
        expect(TokenType::TOKEN_COLON, "Expected ':' in ternary conditional operator");
        auto false_expr = parse_ternary();
        return std::make_unique<TernaryOpNode>(std::move(cond), std::move(true_expr), std::move(false_expr));
    }
    return cond;
}

std::unique_ptr<ExpressionNode> Parser::parse_expression() {
    return parse_ternary();
}

std::unique_ptr<StatementNode> Parser::parse_statement() {
    // 1. Delay
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

    // 3. Case Statement
    if (match(TokenType::TOKEN_CASE)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'case'");
        auto selector = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' after case selector");

        auto case_stmt = std::make_unique<CaseStatementNode>(std::move(selector));

        while (current.type != TokenType::TOKEN_ENDCASE && current.type != TokenType::TOKEN_EOF) {
            CaseItem item;
            if (match(TokenType::TOKEN_DEFAULT)) {
                item.is_default = true;
                item.match_expr = nullptr;
                match(TokenType::TOKEN_COLON);
            } else {
                item.is_default = false;
                item.match_expr = parse_expression();
                expect(TokenType::TOKEN_COLON, "Expected ':' after case item value");
            }

            if (match(TokenType::TOKEN_BEGIN)) {
                while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
                    item.statements.push_back(parse_statement());
                }
                expect(TokenType::TOKEN_END, "Expected 'end' closing case branch block");
            } else {
                item.statements.push_back(parse_statement());
            }

            case_stmt->items.push_back(std::move(item));
        }

        expect(TokenType::TOKEN_ENDCASE, "Expected 'endcase'");
        return case_stmt;
    }

    // 4. $display
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

    // 5. $finish
    if (match(TokenType::TOKEN_FINISH)) {
        if (match(TokenType::TOKEN_LPAREN)) expect(TokenType::TOKEN_RPAREN, "Expected ')'");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $finish");
        return std::make_unique<FinishStatementNode>();
    }

    // 6. $dumpfile
    if (match(TokenType::TOKEN_DUMPFILE)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after $dumpfile");
        std::string fname = current.text;
        expect(TokenType::TOKEN_STRING, "Expected filename string for $dumpfile");
        expect(TokenType::TOKEN_RPAREN, "Expected ')'");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';'");
        return std::make_unique<DumpFileStatementNode>(fname);
    }

    // 7. $dumpvars
    if (match(TokenType::TOKEN_DUMPVARS)) {
        if (match(TokenType::TOKEN_LPAREN)) {
            while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                advance();
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')'");
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';'");
        return std::make_unique<DumpVarsStatementNode>();
    }

    // 8. Array Element Assignment: mem[addr] = val; or mem[addr] <= val;
    if (current.type == TokenType::TOKEN_IDENTIFIER) {
        std::string lhs_name = current.text;
        advance();

        if (match(TokenType::TOKEN_LBRACKET)) {
            auto addr_expr = parse_expression();
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing array index on LHS");

            bool is_non_blocking = false;
            if (match(TokenType::TOKEN_LE)) {
                is_non_blocking = true;
            } else {
                expect(TokenType::TOKEN_EQUAL, "Expected '=' or '<=' in array assignment");
            }
            auto rhs_expr = parse_expression();
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' terminating array assignment");
            return std::make_unique<MemAssignNode>(lhs_name, std::move(addr_expr), std::move(rhs_expr), is_non_blocking);
        }

        // Standard Scalar Assignment
        std::vector<std::string> targets = { lhs_name };
        if (match(TokenType::TOKEN_LE)) {
            auto expr = parse_expression();
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' terminating non-blocking assignment");
            return std::make_unique<NonBlockingAssignNode>(targets[0], std::move(expr));
        } else {
            expect(TokenType::TOKEN_EQUAL, "Expected '=' or '<=' in assignment");
            auto expr = parse_expression();
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' terminating blocking assignment");
            return std::make_unique<BlockingAssignNode>(targets, std::move(expr));
        }
    }

    // 9. Concatenation Assignment: {a, b} = expr;
    if (match(TokenType::TOKEN_LBRACE)) {
        std::vector<std::string> targets;
        while (true) {
            targets.push_back(current.text);
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier inside concatenation");
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_RBRACE, "Expected '}' in concatenation assignment");
        expect(TokenType::TOKEN_EQUAL, "Expected '=' in concatenation assignment");
        auto expr = parse_expression();
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' terminating blocking concatenation assignment");
        return std::make_unique<BlockingAssignNode>(targets, std::move(expr));
    }

    throw std::runtime_error("Unexpected statement beginning with '" + current.text + "' on line " + std::to_string(current.line));
}

std::vector<std::unique_ptr<ModuleNode>> Parser::parse_all() {
    std::vector<std::unique_ptr<ModuleNode>> modules;

    while (current.type != TokenType::TOKEN_EOF) {
        expect(TokenType::TOKEN_MODULE, "Expected 'module' keyword");
        auto mod = std::make_unique<ModuleNode>();
        mod->name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected module name");

        // Ports List: Handles both ANSI (input logic a) and Non-ANSI (clk, reset, d, q)
        if (match(TokenType::TOKEN_LPAREN)) {
            while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                PortNode port;
                port.direction = "unspecified"; // Default until declared in body if non-ANSI
                port.msb = 0; port.lsb = 0;

                if (current.type == TokenType::TOKEN_INPUT) {
                    port.direction = "input";
                    advance();
                } else if (current.type == TokenType::TOKEN_OUTPUT) {
                    port.direction = "output";
                    advance();
                } else if (current.type == TokenType::TOKEN_INOUT) {
                    port.direction = "inout";
                    advance();
                }

                // Optional data type: logic, wire, reg
                if (current.type == TokenType::TOKEN_LOGIC ||
                    current.type == TokenType::TOKEN_WIRE  ||
                    current.type == TokenType::TOKEN_REG) {
                    advance();
                }

                if (match(TokenType::TOKEN_LBRACKET)) {
                    port.msb = current.num_val;
                    advance();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in bus width");
                    port.lsb = current.num_val;
                    advance();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in bus width");
                }

                port.name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected port identifier");
                mod->ports.push_back(port);

                if (!match(TokenType::TOKEN_COMMA)) break;
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing port list");
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after module header");

        // Module Body
        while (current.type != TokenType::TOKEN_ENDMODULE && current.type != TokenType::TOKEN_EOF) {
            // 1. Non-ANSI / body port declarations: input, output, inout
            if (current.type == TokenType::TOKEN_INPUT ||
                current.type == TokenType::TOKEN_OUTPUT ||
                current.type == TokenType::TOKEN_INOUT) {

                std::string dir = (current.type == TokenType::TOKEN_INPUT) ? "input" :
                                  (current.type == TokenType::TOKEN_OUTPUT) ? "output" : "inout";
                advance();

                // Optional wire, reg, logic specifier (e.g. output reg [3:0] q;)
                if (current.type == TokenType::TOKEN_WIRE ||
                    current.type == TokenType::TOKEN_REG ||
                    current.type == TokenType::TOKEN_LOGIC) {
                    advance();
                }

                uint32_t msb = 0, lsb = 0;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    msb = current.num_val; advance();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in bus width");
                    lsb = current.num_val; advance();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in bus width");
                }

                while (true) {
                    std::string sig_name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected port name in body declaration");

                    // Check if already in mod->ports from non-ANSI header
                    bool found = false;
                    for (auto& p : mod->ports) {
                        if (p.name == sig_name) {
                            p.direction = dir;
                            p.msb = msb;
                            p.lsb = lsb;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        PortNode p;
                        p.name = sig_name;
                        p.direction = dir;
                        p.msb = msb;
                        p.lsb = lsb;
                        mod->ports.push_back(p);
                    }

                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after port declaration");
            }
            // 2. Internal signals & memory arrays: logic, wire, reg
            else if (current.type == TokenType::TOKEN_LOGIC ||
                     current.type == TokenType::TOKEN_WIRE  ||
                     current.type == TokenType::TOKEN_REG) {
                advance();

                uint32_t msb = 0, lsb = 0;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    msb = current.num_val; advance();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in bus width");
                    lsb = current.num_val; advance();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in bus width");
                }

                while (true) {
                    PortNode sig;
                    sig.direction = "internal";
                    sig.msb = msb;
                    sig.lsb = lsb;
                    sig.name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected signal name");

                    // Check if variable is already a port (e.g. output q; reg q;)
                    bool already_port = false;
                    for (auto& p : mod->ports) {
                        if (p.name == sig.name) {
                            already_port = true;
                            if (msb > 0) { p.msb = msb; p.lsb = lsb; }
                            break;
                        }
                    }

                    // Check for unpacked memory dimensions: [0:depth-1]
                    if (match(TokenType::TOKEN_LBRACKET)) {
                        uint32_t bound1 = current.num_val; advance();
                        if (match(TokenType::TOKEN_COLON)) {
                            uint32_t bound2 = current.num_val; advance();
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing array dimension");
                            sig.is_array = true;
                            sig.array_depth = (bound2 >= bound1) ? (bound2 - bound1 + 1) : (bound1 - bound2 + 1);
                        } else {
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing array dimension");
                            sig.is_array = true;
                            sig.array_depth = bound1;
                        }
                    }

                    if (!already_port) {
                        mod->signals.push_back(sig);
                    }

                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after signal declaration");
            }
            // 3. Continuous assign: assign target = expr;
            else if (match(TokenType::TOKEN_ASSIGN)) {
                std::string target = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected target identifier in continuous assign");
                expect(TokenType::TOKEN_EQUAL, "Expected '=' in continuous assign");
                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after continuous assign");
                mod->continuous_assigns.push_back(std::make_unique<ContinuousAssignNode>(target, std::move(rhs)));
            }
            // 4. always_comb
            else if (match(TokenType::TOKEN_ALWAYS_COMB)) {
                auto proc = std::make_unique<ProcessNode>(ProcessType::COMBINATIONAL);
                expect(TokenType::TOKEN_BEGIN, "Expected 'begin' block");
                while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
                    proc->body.push_back(parse_statement());
                }
                expect(TokenType::TOKEN_END, "Expected 'end'");
                mod->processes.push_back(std::move(proc));
            }
            // 5. always_ff @(...) or generic Verilog always @(...)
            else if (match(TokenType::TOKEN_ALWAYS_FF) || match(TokenType::TOKEN_ALWAYS)) {
                auto proc = std::make_unique<ProcessNode>(ProcessType::SEQUENTIAL);
                expect(TokenType::TOKEN_AT, "Expected '@' in always block sensitivity list");
                expect(TokenType::TOKEN_LPAREN, "Expected '(' in sensitivity list");

                bool has_edge = false;
                while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                    if (match(TokenType::TOKEN_OR_BIT) || match(TokenType::TOKEN_COMMA)) {
                        continue;
                    }
                    EdgeType edge = EdgeType::LEVEL;
                    if (match(TokenType::TOKEN_POSEDGE)) {
                        edge = EdgeType::POSEDGE;
                        has_edge = true;
                    } else if (match(TokenType::TOKEN_NEGEDGE)) {
                        edge = EdgeType::NEGEDGE;
                        has_edge = true;
                    }
                    std::string sig = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected signal name in sensitivity list");
                    proc->sensitivity_list.push_back({edge, sig});

                    match(TokenType::TOKEN_OR);
                }
                expect(TokenType::TOKEN_RPAREN, "Expected ')' closing sensitivity list");

                // Infer process type: edge-triggered -> SEQUENTIAL, level/wildcard -> COMBINATIONAL
                if (!has_edge) {
                    proc->type = ProcessType::COMBINATIONAL;
                }

                if (match(TokenType::TOKEN_BEGIN)) {
                    while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
                        proc->body.push_back(parse_statement());
                    }
                    expect(TokenType::TOKEN_END, "Expected 'end'");
                } else {
                    proc->body.push_back(parse_statement());
                }
                mod->processes.push_back(std::move(proc));
            }
            // 6. initial
            else if (match(TokenType::TOKEN_INITIAL)) {
                auto proc = std::make_unique<ProcessNode>(ProcessType::INITIAL);
                if (match(TokenType::TOKEN_BEGIN)) {
                    while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
                        proc->body.push_back(parse_statement());
                    }
                    expect(TokenType::TOKEN_END, "Expected 'end'");
                } else {
                    proc->body.push_back(parse_statement());
                }
                mod->processes.push_back(std::move(proc));
            }
            // 7. Submodule Instantiations
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