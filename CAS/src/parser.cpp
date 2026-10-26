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
        std::string loc = current.filename.empty() ? ("line " + std::to_string(current.line)) : (current.filename + ":" + std::to_string(current.line));
        throw std::runtime_error(loc + ": error: " + err + " (found: '" + current.text + "')");
    }
    advance();
}

std::unique_ptr<ExpressionNode> Parser::parse_primary() {
    if (current.type == TokenType::TOKEN_NUMBER) {
        bool is_unk = false;
        for (char ch : current.text) {
            if (ch == 'x' || ch == 'X' || ch == 'z' || ch == 'Z') {
                is_unk = true;
                break;
            }
        }
        auto node = std::make_unique<NumberNode>(current.num_val, current.bit_width, is_unk);
        advance();
        return node;
    }
    if (current.type == TokenType::TOKEN_IDENTIFIER) {
        std::string ident_name = current.text;
        advance();

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
    throw std::runtime_error((current.filename.empty() ? ("line " + std::to_string(current.line)) : (current.filename + ":" + std::to_string(current.line))) + ": error: Unexpected token '" + current.text + "' in expression");
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
    if (match(TokenType::TOKEN_HASH)) {
        if (current.type != TokenType::TOKEN_NUMBER) {
            throw std::runtime_error((current.filename.empty() ? ("line " + std::to_string(current.line)) : (current.filename + ":" + std::to_string(current.line))) + ": error: Unexpected statement beginning with '" + current.text + "'");
        }
        uint64_t delay_val = current.num_val;
        advance();
        match(TokenType::TOKEN_SEMICOLON);
        return std::make_unique<DelayStatementNode>(delay_val);
    }

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

    if (current.type == TokenType::TOKEN_DISPLAY || current.type == TokenType::TOKEN_MONITOR) {
        bool is_mon = (current.type == TokenType::TOKEN_MONITOR);
        advance();
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after display/monitor");

        bool has_lead_time = false;
        std::string fmt = "";
        std::vector<std::string> args;

        if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "$time") {
            has_lead_time = true;
            advance();
            expect(TokenType::TOKEN_COMMA, "Expected ',' after $time");
        }

        if (current.type == TokenType::TOKEN_STRING) {
            fmt = current.text;
            advance();
        }

        while (match(TokenType::TOKEN_COMMA)) {
            args.push_back(current.text);
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier in argument list");
        }

        expect(TokenType::TOKEN_RPAREN, "Expected ')'");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';'");
        return std::make_unique<DisplayStatementNode>(fmt, args, is_mon, has_lead_time);
    }

    if (match(TokenType::TOKEN_FINISH) || match(TokenType::TOKEN_STOP)) {
        if (match(TokenType::TOKEN_LPAREN)) {
            while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) advance();
            expect(TokenType::TOKEN_RPAREN, "Expected ')'");
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';'");
        return std::make_unique<FinishStatementNode>();
    }

    if (match(TokenType::TOKEN_DUMPFILE)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after $dumpfile");
        std::string fname = current.text;
        expect(TokenType::TOKEN_STRING, "Expected filename string for $dumpfile");
        expect(TokenType::TOKEN_RPAREN, "Expected ')'");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';'");
        return std::make_unique<DumpFileStatementNode>(fname);
    }

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

    if (current.type == TokenType::TOKEN_IDENTIFIER) {
        std::string lhs_name = current.text;
        advance();

        if (match(TokenType::TOKEN_SEMICOLON)) {
            return std::make_unique<TaskCallNode>(lhs_name);
        }

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

        // Ports List
        if (match(TokenType::TOKEN_LPAREN)) {
            std::string current_dir = "unspecified";

            while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                if (current.type == TokenType::TOKEN_INPUT) {
                    current_dir = "input";
                    advance();
                } else if (current.type == TokenType::TOKEN_OUTPUT) {
                    current_dir = "output";
                    advance();
                } else if (current.type == TokenType::TOKEN_INOUT) {
                    current_dir = "inout";
                    advance();
                }

                if (current.type == TokenType::TOKEN_LOGIC ||
                    current.type == TokenType::TOKEN_WIRE  ||
                    current.type == TokenType::TOKEN_REG) {
                    advance();
                }

                std::unique_ptr<ExpressionNode> msb = nullptr;
                std::unique_ptr<ExpressionNode> lsb = nullptr;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    msb = parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in bus width");
                    lsb = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in bus width");
                }

                PortNode port;
                port.direction = current_dir;
                port.msb_expr = std::move(msb);
                port.lsb_expr = std::move(lsb);
                port.name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected port identifier");
                mod->ports.push_back(std::move(port));

                if (!match(TokenType::TOKEN_COMMA)) break;
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing port list");
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after module header");

        // Module Body
        while (current.type != TokenType::TOKEN_ENDMODULE && current.type != TokenType::TOKEN_EOF) {
            // 0. Parameters
            if (current.type == TokenType::TOKEN_PARAMETER || current.type == TokenType::TOKEN_LOCALPARAM) {
                advance();
                while (true) {
                    std::string p_name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter name");
                    expect(TokenType::TOKEN_EQUAL, "Expected '=' in parameter definition");
                    auto def_expr = parse_expression();
                    mod->parameters.push_back({p_name, std::move(def_expr)});
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after parameter");
            }
            // 1. Port declarations in body
            else if (current.type == TokenType::TOKEN_INPUT ||
                     current.type == TokenType::TOKEN_OUTPUT ||
                     current.type == TokenType::TOKEN_INOUT) {

                std::string dir = (current.type == TokenType::TOKEN_INPUT) ? "input" :
                                  (current.type == TokenType::TOKEN_OUTPUT) ? "output" : "inout";
                advance();

                if (current.type == TokenType::TOKEN_WIRE ||
                    current.type == TokenType::TOKEN_REG ||
                    current.type == TokenType::TOKEN_LOGIC) {
                    advance();
                }

                std::unique_ptr<ExpressionNode> msb = nullptr;
                std::unique_ptr<ExpressionNode> lsb = nullptr;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    msb = parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in bus width");
                    lsb = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in bus width");
                }

                while (true) {
                    std::string sig_name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected port name in body declaration");

                    bool found = false;
                    for (auto& p : mod->ports) {
                        if (p.name == sig_name) {
                            p.direction = dir;
                            if (msb && lsb) {
                                p.msb_expr = msb->clone();
                                p.lsb_expr = lsb->clone();
                            }
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        PortNode p;
                        p.name = sig_name;
                        p.direction = dir;
                        if (msb && lsb) {
                            p.msb_expr = msb->clone();
                            p.lsb_expr = lsb->clone();
                        }
                        mod->ports.push_back(std::move(p));
                    }

                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after port declaration");
            }
            // 2. Signals
            else if (current.type == TokenType::TOKEN_LOGIC ||
                     current.type == TokenType::TOKEN_WIRE  ||
                     current.type == TokenType::TOKEN_REG) {
                advance();

                std::unique_ptr<ExpressionNode> msb = nullptr;
                std::unique_ptr<ExpressionNode> lsb = nullptr;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    msb = parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in bus width");
                    lsb = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in bus width");
                }

                while (true) {
                    PortNode sig;
                    sig.direction = "internal";
                    if (msb && lsb) {
                        sig.msb_expr = msb->clone();
                        sig.lsb_expr = lsb->clone();
                    }
                    sig.name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected signal name");

                    bool already_port = false;
                    for (auto& p : mod->ports) {
                        if (p.name == sig.name) {
                            already_port = true;
                            if (msb && lsb) {
                                p.msb_expr = msb->clone();
                                p.lsb_expr = lsb->clone();
                            }
                            break;
                        }
                    }

                    if (match(TokenType::TOKEN_LBRACKET)) {
                        auto b1 = parse_expression();
                        if (match(TokenType::TOKEN_COLON)) {
                            auto b2 = parse_expression();
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing array dimension");
                            sig.is_array = true;
                            sig.bound1_expr = std::move(b1);
                            sig.bound2_expr = std::move(b2);
                        } else {
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing array dimension");
                            sig.is_array = true;
                            sig.bound1_expr = std::move(b1);
                            sig.bound2_expr = nullptr;
                        }
                    }

                    if (!already_port) {
                        mod->signals.push_back(std::move(sig));
                    }

                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after signal declaration");
            }
            // 3. Built-in Gate Primitives
            else if (current.type == TokenType::TOKEN_GATE_AND  ||
                     current.type == TokenType::TOKEN_GATE_OR   ||
                     current.type == TokenType::TOKEN_OR        ||
                     current.type == TokenType::TOKEN_GATE_XOR  ||
                     current.type == TokenType::TOKEN_GATE_NAND ||
                     current.type == TokenType::TOKEN_GATE_NOR  ||
                     current.type == TokenType::TOKEN_GATE_XNOR ||
                     current.type == TokenType::TOKEN_GATE_NOT  ||
                     current.type == TokenType::TOKEN_GATE_BUF) {

                auto gate = std::make_unique<GatePrimitiveNode>();
                if (current.type == TokenType::TOKEN_GATE_AND) gate->type = GateType::AND;
                else if (current.type == TokenType::TOKEN_GATE_OR || current.type == TokenType::TOKEN_OR) gate->type = GateType::OR;
                else if (current.type == TokenType::TOKEN_GATE_XOR) gate->type = GateType::XOR;
                else if (current.type == TokenType::TOKEN_GATE_NAND) gate->type = GateType::NAND;
                else if (current.type == TokenType::TOKEN_GATE_NOR) gate->type = GateType::NOR;
                else if (current.type == TokenType::TOKEN_GATE_XNOR) gate->type = GateType::XNOR;
                else if (current.type == TokenType::TOKEN_GATE_NOT) gate->type = GateType::NOT;
                else if (current.type == TokenType::TOKEN_GATE_BUF) gate->type = GateType::BUF;
                advance();

                if (current.type == TokenType::TOKEN_IDENTIFIER) {
                    gate->instance_name = current.text;
                    advance();
                } else {
                    gate->instance_name = "gate_" + std::to_string(mod->gates.size());
                }

                expect(TokenType::TOKEN_LPAREN, "Expected '(' in gate connection list");
                gate->out_expr = parse_expression();

                while (match(TokenType::TOKEN_COMMA)) {
                    gate->in_exprs.push_back(parse_expression());
                }
                expect(TokenType::TOKEN_RPAREN, "Expected ')' closing gate connections");
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after gate instantiation");
                mod->gates.push_back(std::move(gate));
            }
            // 4. Continuous assign
            else if (match(TokenType::TOKEN_ASSIGN)) {
                std::string target = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected target identifier in continuous assign");
                expect(TokenType::TOKEN_EQUAL, "Expected '=' in continuous assign");
                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after continuous assign");
                mod->continuous_assigns.push_back(std::make_unique<ContinuousAssignNode>(target, std::move(rhs)));
            }
            // 5. Tasks
            else if (match(TokenType::TOKEN_TASK)) {
                auto task = std::make_unique<TaskNode>();
                task->name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected task name");
                match(TokenType::TOKEN_SEMICOLON);

                bool has_begin = match(TokenType::TOKEN_BEGIN);
                while (current.type != TokenType::TOKEN_ENDTASK && current.type != TokenType::TOKEN_EOF) {
                    if (has_begin && current.type == TokenType::TOKEN_END) {
                        advance();
                        break;
                    }
                    task->body.push_back(parse_statement());
                }
                expect(TokenType::TOKEN_ENDTASK, "Expected 'endtask'");
                mod->tasks.push_back(std::move(task));
            }
            // 6. always_comb
            else if (match(TokenType::TOKEN_ALWAYS_COMB)) {
                auto proc = std::make_unique<ProcessNode>(ProcessType::COMBINATIONAL);
                expect(TokenType::TOKEN_BEGIN, "Expected 'begin' block");
                while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
                    proc->body.push_back(parse_statement());
                }
                expect(TokenType::TOKEN_END, "Expected 'end'");
                mod->processes.push_back(std::move(proc));
            }
            // 7. always_ff or generic always
            else if (match(TokenType::TOKEN_ALWAYS_FF) || match(TokenType::TOKEN_ALWAYS)) {
                auto proc = std::make_unique<ProcessNode>(ProcessType::SEQUENTIAL);
                expect(TokenType::TOKEN_AT, "Expected '@' in always block sensitivity list");
                expect(TokenType::TOKEN_LPAREN, "Expected '(' in sensitivity list");

                bool has_edge = false;
                while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                    while (current.type == TokenType::TOKEN_OR ||
                           current.type == TokenType::TOKEN_GATE_OR ||
                           current.type == TokenType::TOKEN_OR_BIT ||
                           current.type == TokenType::TOKEN_COMMA) {
                        advance();
                    }

                    if (current.type == TokenType::TOKEN_RPAREN) break;

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

                    while (current.type == TokenType::TOKEN_OR ||
                           current.type == TokenType::TOKEN_GATE_OR ||
                           current.type == TokenType::TOKEN_OR_BIT ||
                           current.type == TokenType::TOKEN_COMMA) {
                        advance();
                    }
                }
                expect(TokenType::TOKEN_RPAREN, "Expected ')' closing sensitivity list");

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
            // 8. initial
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
            // 9. Module Instantiations
            else if (current.type == TokenType::TOKEN_IDENTIFIER) {
                std::string mod_type = current.text;
                advance();

                std::vector<std::unique_ptr<ExpressionNode>> param_overrides;
                if (match(TokenType::TOKEN_HASH)) {
                    expect(TokenType::TOKEN_LPAREN, "Expected '(' after '#' in parameter override");
                    while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                        param_overrides.push_back(parse_expression());
                        if (!match(TokenType::TOKEN_COMMA)) break;
                    }
                    expect(TokenType::TOKEN_RPAREN, "Expected ')' after parameter override list");
                }

                std::string inst_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected instance name");
                expect(TokenType::TOKEN_LPAREN, "Expected '(' for instance port map");

                bool is_pos = (current.type != TokenType::TOKEN_DOT);
                auto inst = std::make_unique<InstantiationNode>(mod_type, inst_name, is_pos);
                inst->param_overrides = std::move(param_overrides);

                while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                    PortConnection conn;
                    if (!is_pos) {
                        expect(TokenType::TOKEN_DOT, "Expected '.' before port name");
                        conn.port_name = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected port name");
                        expect(TokenType::TOKEN_LPAREN, "Expected '(' after port name");
                        conn.expr = parse_expression();
                        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing port expression");
                    } else {
                        conn.port_name = "";
                        conn.expr = parse_expression();
                    }
                    inst->connections.push_back(std::move(conn));
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