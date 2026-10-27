#include "../../include/frontend/parser.hpp"
#include <stdexcept>

std::vector<std::unique_ptr<ModuleNode>> Parser::parse_all() {
    std::vector<std::unique_ptr<ModuleNode>> modules;

    while (current.type != TokenType::TOKEN_EOF) {
        if (current.type == TokenType::TOKEN_PARAMETER || current.type == TokenType::TOKEN_LOCALPARAM) {
            advance();
            
            if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
                current.type == TokenType::TOKEN_WIRE || (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte"))) {
                advance();
            }
            if (match(TokenType::TOKEN_LBRACKET)) {
                parse_expression();
                expect(TokenType::TOKEN_COLON, "Expected ':' in parameter range");
                parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
            }

            while (true) {
                std::string pname = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter identifier");
                expect(TokenType::TOKEN_EQUAL, "Expected '=' in parameter assignment");
                auto val_expr = parse_expression();

                ParameterDef pdef;
                pdef.name = pname;
                pdef.default_expr = std::move(val_expr);
                global_parameters.push_back(std::move(pdef));

                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after parameter declaration");
            continue;
        }

        expect(TokenType::TOKEN_MODULE, "Expected 'module', 'parameter', or 'localparam' keyword");
        auto mod = std::make_unique<ModuleNode>();
        mod->defined_in_file = current.filename;
        mod->defined_on_line = current.line;
        mod->name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected module name");

        for (const auto& gp : global_parameters) {
            ParameterDef pcopy;
            pcopy.name = gp.name;
            pcopy.default_expr = gp.default_expr ? gp.default_expr->clone() : nullptr;
            mod->parameters.push_back(std::move(pcopy));
        }

        if (match(TokenType::TOKEN_HASH)) {
            expect(TokenType::TOKEN_LPAREN, "Expected '(' after '#' in module header");
            while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                if (current.type == TokenType::TOKEN_PARAMETER || current.type == TokenType::TOKEN_LOCALPARAM) {
                    advance();
                }
                std::string pname = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter identifier");
                expect(TokenType::TOKEN_EQUAL, "Expected '=' in parameter definition");
                auto pexpr = parse_expression();
                mod->parameters.push_back({pname, std::move(pexpr)});

                if (match(TokenType::TOKEN_COMMA)) continue;
                else break;
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing parameter list");
        }

        // Parse Ports in module header
        if (match(TokenType::TOKEN_LPAREN)) {
            while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                std::string dir = "";
                if (current.type == TokenType::TOKEN_INPUT) { dir = "input"; advance(); }
                else if (current.type == TokenType::TOKEN_OUTPUT) { dir = "output"; advance(); }
                else if (current.type == TokenType::TOKEN_INOUT) { dir = "inout"; advance(); }

                if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_REG) {
                    advance();
                }

                std::unique_ptr<ExpressionNode> msb = nullptr;
                std::unique_ptr<ExpressionNode> lsb = nullptr;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    msb = parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in port range");
                    lsb = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in port range");
                }

                while (true) {
                    std::string pname = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected port identifier");
                    PortNode port(pname, dir, msb ? msb->clone() : nullptr, lsb ? lsb->clone() : nullptr);
                    mod->ports.push_back(std::move(port));

                    if (match(TokenType::TOKEN_COMMA)) {
                        // Comma consumed; if next is another port with direction or type, break inner list
                        if (current.type == TokenType::TOKEN_INPUT || current.type == TokenType::TOKEN_OUTPUT || current.type == TokenType::TOKEN_INOUT ||
                            current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_REG) {
                            break;
                        }
                        continue;
                    }

                    // No comma: the only valid next token in a port list is ')'
                    if (current.type != TokenType::TOKEN_RPAREN) {
                        std::string loc = current.filename.empty() 
                            ? ("line " + std::to_string(current.line)) 
                            : (current.filename + ":" + std::to_string(current.line));
                        throw std::runtime_error(loc + ": error: Expected ',' or ')' in module port list (found: '" + current.text + "')");
                    }
                    break;
                }
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing port list");
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after module header");

        while (current.type != TokenType::TOKEN_ENDMODULE && current.type != TokenType::TOKEN_EOF) {
            if (current.type == TokenType::TOKEN_PARAMETER || current.type == TokenType::TOKEN_LOCALPARAM) {
                advance();
                if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
                    current.type == TokenType::TOKEN_WIRE) {
                    advance();
                }
                if (match(TokenType::TOKEN_LBRACKET)) {
                    parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in parameter range");
                    parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
                }
                while (true) {
                    std::string pname = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter identifier");
                    expect(TokenType::TOKEN_EQUAL, "Expected '=' in parameter assignment");
                    auto val_expr = parse_expression();
                    mod->parameters.push_back({pname, std::move(val_expr)});
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after parameter declaration");
                continue;
            }

            if (current.type == TokenType::TOKEN_INPUT || current.type == TokenType::TOKEN_OUTPUT || current.type == TokenType::TOKEN_INOUT) {
                std::string dir = current.text;
                advance();
                if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_REG) {
                    advance();
                }
                std::unique_ptr<ExpressionNode> msb = nullptr;
                std::unique_ptr<ExpressionNode> lsb = nullptr;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    msb = parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in range");
                    lsb = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in range");
                }
                while (true) {
                    std::string pname = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier");
                    bool found = false;
                    for (auto& p : mod->ports) {
                        if (p.name == pname) {
                            p.direction = dir;
                            if (msb) p.msb_expr = msb->clone();
                            if (lsb) p.lsb_expr = lsb->clone();
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        mod->ports.emplace_back(pname, dir, msb ? msb->clone() : nullptr, lsb ? lsb->clone() : nullptr);
                    }
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after port declaration");
                continue;
            }

            if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_REG) {
                advance();
                std::unique_ptr<ExpressionNode> msb = nullptr;
                std::unique_ptr<ExpressionNode> lsb = nullptr;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    msb = parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in range");
                    lsb = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in range");
                }
                while (true) {
                    std::string signame = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier");
                    PortNode sig(signame, "internal", msb ? msb->clone() : nullptr, lsb ? lsb->clone() : nullptr);

                    if (match(TokenType::TOKEN_LBRACKET)) {
                        sig.is_array = true;
                        sig.bound1_expr = parse_expression();
                        if (match(TokenType::TOKEN_COLON)) {
                            sig.bound2_expr = parse_expression();
                        }
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in memory dimension");
                    }

                    bool is_port = false;
                    for (auto& p : mod->ports) {
                        if (p.name == signame) {
                            if (msb) p.msb_expr = msb->clone();
                            if (lsb) p.lsb_expr = lsb->clone();
                            is_port = true;
                            break;
                        }
                    }
                    if (!is_port) {
                        mod->signals.push_back(std::move(sig));
                    }
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after signal declaration");
                continue;
            }

            if (match(TokenType::TOKEN_ASSIGN)) {
                std::string target = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected target identifier in assign");
                expect(TokenType::TOKEN_EQUAL, "Expected '=' in assign statement");
                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assign");
                mod->continuous_assigns.push_back(std::make_unique<ContinuousAssignNode>(target, std::move(rhs)));
                continue;
            }

            if (current.type == TokenType::TOKEN_GATE_AND  || current.type == TokenType::TOKEN_GATE_OR   ||
                current.type == TokenType::TOKEN_GATE_XOR  || current.type == TokenType::TOKEN_GATE_NAND ||
                current.type == TokenType::TOKEN_GATE_NOR  || current.type == TokenType::TOKEN_GATE_XNOR ||
                current.type == TokenType::TOKEN_GATE_NOT  || current.type == TokenType::TOKEN_GATE_BUF   ||
                current.type == TokenType::TOKEN_OR) {
                
                GateType gt = GateType::AND;
                if (current.type == TokenType::TOKEN_GATE_OR || current.type == TokenType::TOKEN_OR) gt = GateType::OR;
                else if (current.type == TokenType::TOKEN_GATE_XOR) gt = GateType::XOR;
                else if (current.type == TokenType::TOKEN_GATE_NAND) gt = GateType::NAND;
                else if (current.type == TokenType::TOKEN_GATE_NOR) gt = GateType::NOR;
                else if (current.type == TokenType::TOKEN_GATE_XNOR) gt = GateType::XNOR;
                else if (current.type == TokenType::TOKEN_GATE_NOT) gt = GateType::NOT;
                else if (current.type == TokenType::TOKEN_GATE_BUF) gt = GateType::BUF;
                advance();

                std::string inst_name = "";
                if (current.type == TokenType::TOKEN_IDENTIFIER) {
                    inst_name = current.text;
                    advance();
                } else {
                    inst_name = "gate_" + std::to_string(mod->gates.size());
                }

                expect(TokenType::TOKEN_LPAREN, "Expected '(' in gate instance");
                auto out_expr = parse_expression();
                expect(TokenType::TOKEN_COMMA, "Expected ',' after gate output");
                std::vector<std::unique_ptr<ExpressionNode>> in_exprs;
                while (true) {
                    in_exprs.push_back(parse_expression());
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_RPAREN, "Expected ')' closing gate instance");
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after gate instance");

                auto gate = std::make_unique<GatePrimitiveNode>(gt, std::move(out_expr), std::move(in_exprs), inst_name);
                mod->gates.push_back(std::move(gate));
                continue;
            }

            if (match(TokenType::TOKEN_TASK)) {
                auto t = std::make_unique<TaskNode>();
                t->name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected task name");
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after task name");
                
                if (match(TokenType::TOKEN_BEGIN)) {
                    while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
                        t->body.push_back(parse_statement());
                    }
                    expect(TokenType::TOKEN_END, "Expected 'end' closing task begin block");
                    match(TokenType::TOKEN_SEMICOLON);
                } else {
                    while (current.type != TokenType::TOKEN_ENDTASK && current.type != TokenType::TOKEN_EOF) {
                        t->body.push_back(parse_statement());
                    }
                }
                expect(TokenType::TOKEN_ENDTASK, "Expected 'endtask'");
                mod->tasks.push_back(std::move(t));
                continue;
            }

            if (match(TokenType::TOKEN_INITIAL)) {
                auto proc = std::make_unique<ProcessNode>(ProcessType::INITIAL);
                proc->body = parse_statement_or_block();
                mod->processes.push_back(std::move(proc));
                continue;
            }

            if (match(TokenType::TOKEN_ALWAYS_COMB)) {
                auto proc = std::make_unique<ProcessNode>(ProcessType::COMBINATIONAL);
                proc->body = parse_statement_or_block();
                mod->processes.push_back(std::move(proc));
                continue;
            }

            if (match(TokenType::TOKEN_ALWAYS_FF)) {
                mod->processes.push_back(parse_always_ff());
                continue;
            }

            if (match(TokenType::TOKEN_ALWAYS)) {
                if (current.type == TokenType::TOKEN_AT) {
                    advance();
                    expect(TokenType::TOKEN_LPAREN, "Expected '(' after '@'");
                    auto proc = std::make_unique<ProcessNode>(ProcessType::SEQUENTIAL);
                    while (true) {
                        EdgeType edge = EdgeType::LEVEL;
                        if (match(TokenType::TOKEN_POSEDGE)) edge = EdgeType::POSEDGE;
                        else if (match(TokenType::TOKEN_NEGEDGE)) edge = EdgeType::NEGEDGE;

                        std::string sig = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected signal name in sensitivity list");
                        proc->sensitivity_list.push_back({edge, sig});

                        if (match(TokenType::TOKEN_OR) || match(TokenType::TOKEN_GATE_OR) || match(TokenType::TOKEN_COMMA)) continue;
                        break;
                    }
                    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing sensitivity list");
                    proc->body = parse_statement_or_block();
                    mod->processes.push_back(std::move(proc));
                } else {
                    auto proc = std::make_unique<ProcessNode>(ProcessType::COMBINATIONAL);
                    proc->body = parse_statement_or_block();
                    mod->processes.push_back(std::move(proc));
                }
                continue;
            }

            if (current.type == TokenType::TOKEN_IDENTIFIER) {
                std::string mtype = current.text;
                advance();

                std::vector<std::unique_ptr<ExpressionNode>> param_overrides;
                if (match(TokenType::TOKEN_HASH)) {
                    expect(TokenType::TOKEN_LPAREN, "Expected '(' in parameter override list");
                    while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                        if (match(TokenType::TOKEN_DOT)) {
                            expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter name after '.'");
                            expect(TokenType::TOKEN_LPAREN, "Expected '(' in named parameter override");
                            param_overrides.push_back(parse_expression());
                            expect(TokenType::TOKEN_RPAREN, "Expected ')' in named parameter override");
                        } else {
                            param_overrides.push_back(parse_expression());
                        }
                        if (match(TokenType::TOKEN_COMMA)) continue;
                        break;
                    }
                    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing parameter override list");
                }

                std::string iname = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected instance identifier");
                expect(TokenType::TOKEN_LPAREN, "Expected '(' in instance port connections");

                bool positional = false;
                std::vector<PortConnection> conns;
                while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                    if (match(TokenType::TOKEN_DOT)) {
                        std::string port_name = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected port name after '.'");
                        expect(TokenType::TOKEN_LPAREN, "Expected '(' in port connection");
                        auto expr = parse_expression();
                        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing port connection");
                        conns.push_back({port_name, std::move(expr)});
                    } else {
                        positional = true;
                        auto expr = parse_expression();
                        conns.push_back({"", std::move(expr)});
                    }
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_RPAREN, "Expected ')' closing instance port connections");
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after instance");

                auto inst = std::make_unique<InstantiationNode>(mtype, iname, positional);
                inst->param_overrides = std::move(param_overrides);
                inst->connections = std::move(conns);
                mod->instantiations.push_back(std::move(inst));
                continue;
            }

            advance();
        }
        expect(TokenType::TOKEN_ENDMODULE, "Expected 'endmodule'");
        modules.push_back(std::move(mod));
    }

    return modules;
}