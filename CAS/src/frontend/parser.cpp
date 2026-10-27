#include "../../include/frontend/parser.hpp"
#include <stdexcept>

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

// -------------------------------------------------------------
// Expression Parsing (Precedence Climbing)
// -------------------------------------------------------------

std::unique_ptr<ExpressionNode> Parser::parse_primary() {
    if (current.type == TokenType::TOKEN_NUMBER) {
        uint64_t val = current.num_val;
        uint32_t w = current.bit_width;
        bool is_unk = (current.text.find('x') != std::string::npos || 
                       current.text.find('X') != std::string::npos ||
                       current.text.find('z') != std::string::npos || 
                       current.text.find('Z') != std::string::npos);
        advance();
        return std::make_unique<NumberNode>(val, w, is_unk);
    }

    if (current.type == TokenType::TOKEN_IDENTIFIER) {
        std::string name = current.text;
        advance();

        // Check for bit-select or part-select: name[msb] or name[msb:lsb]
        if (match(TokenType::TOKEN_LBRACKET)) {
            auto msb = parse_expression();
            if (match(TokenType::TOKEN_COLON)) {
                auto lsb = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' after part-select range");
                return std::make_unique<IndexSelectNode>(name, std::move(msb), std::move(lsb));
            } else {
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' after index expression");
                return std::make_unique<IndexSelectNode>(name, std::move(msb));
            }
        }
        return std::make_unique<IdentifierNode>(name);
    }

    if (match(TokenType::TOKEN_LPAREN)) {
        auto expr = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' after nested expression");
        return expr;
    }

    // Concatenation expression: {a, b, c}
    if (match(TokenType::TOKEN_LBRACE)) {
        std::vector<std::unique_ptr<ExpressionNode>> elements;
        while (true) {
            elements.push_back(parse_expression());
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_RBRACE, "Expected '}' closing concatenation expression");
        return std::make_unique<ConcatNode>(std::move(elements));
    }

    std::string loc = current.filename.empty() ? ("line " + std::to_string(current.line)) : (current.filename + ":" + std::to_string(current.line));
    throw std::runtime_error(loc + ": error: Unexpected token '" + current.text + "' in expression");
}

std::unique_ptr<ExpressionNode> Parser::parse_unary() {
    if (current.type == TokenType::TOKEN_NOT || current.type == TokenType::TOKEN_TILDE) {
        char op = (current.type == TokenType::TOKEN_NOT) ? '!' : '~';
        advance();
        auto operand = parse_unary();
        return std::make_unique<UnaryOpNode>(op, std::move(operand));
    }
    return parse_primary();
}

std::unique_ptr<ExpressionNode> Parser::parse_multiplicative() {
    auto lhs = parse_unary();
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_additive() {
    auto lhs = parse_multiplicative();
    while (current.type == TokenType::TOKEN_PLUS || current.type == TokenType::TOKEN_MINUS) {
        std::string op = current.text;
        advance();
        auto rhs = parse_multiplicative();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_shift() {
    auto lhs = parse_additive();
    while (current.type == TokenType::TOKEN_SHL || current.type == TokenType::TOKEN_SHR) {
        std::string op = current.text;
        advance();
        auto rhs = parse_additive();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_relational() {
    auto lhs = parse_shift();
    while (current.type == TokenType::TOKEN_LT || current.type == TokenType::TOKEN_LE ||
           current.type == TokenType::TOKEN_GT || current.type == TokenType::TOKEN_GE) {
        std::string op = current.text;
        advance();
        auto rhs = parse_shift();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_equality() {
    auto lhs = parse_relational();
    while (current.type == TokenType::TOKEN_EQ || current.type == TokenType::TOKEN_NEQ) {
        std::string op = current.text;
        advance();
        auto rhs = parse_relational();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_bitwise_and() {
    auto lhs = parse_equality();
    while (current.type == TokenType::TOKEN_AND) {
        std::string op = current.text;
        advance();
        auto rhs = parse_equality();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_bitwise_xor() {
    auto lhs = parse_bitwise_and();
    while (current.type == TokenType::TOKEN_XOR) {
        std::string op = current.text;
        advance();
        auto rhs = parse_bitwise_and();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_bitwise_or() {
    auto lhs = parse_bitwise_xor();
    while (current.type == TokenType::TOKEN_OR_BIT) {
        std::string op = current.text;
        advance();
        auto rhs = parse_bitwise_xor();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_logical_and() {
    auto lhs = parse_bitwise_or();
    while (current.type == TokenType::TOKEN_LOGICAL_AND) {
        std::string op = current.text;
        advance();
        auto rhs = parse_bitwise_or();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
    }
    return lhs;
}

std::unique_ptr<ExpressionNode> Parser::parse_logical_or() {
    auto lhs = parse_logical_and();
    while (current.type == TokenType::TOKEN_LOGICAL_OR) {
        std::string op = current.text;
        advance();
        auto rhs = parse_logical_and();
        lhs = std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs));
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

// -------------------------------------------------------------
// Statement Parsing
// -------------------------------------------------------------

std::unique_ptr<StatementNode> Parser::parse_statement() {
    if (match(TokenType::TOKEN_HASH)) {
        uint64_t d = current.num_val;
        expect(TokenType::TOKEN_NUMBER, "Expected numerical delay value after '#'");
        match(TokenType::TOKEN_SEMICOLON);
        return std::make_unique<DelayStatementNode>(d);
    }

    if (current.type == TokenType::TOKEN_DISPLAY || current.type == TokenType::TOKEN_MONITOR) {
        bool is_mon = (current.type == TokenType::TOKEN_MONITOR);
        advance(); // consume $display or $monitor
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after $display / $monitor");

        bool has_lead_time = false;
        std::string fmt = "";

        if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "$time") {
            has_lead_time = true;
            advance();
            if (match(TokenType::TOKEN_COMMA)) {
                if (current.type == TokenType::TOKEN_STRING) {
                    fmt = current.text;
                    advance();
                }
            }
        } else if (current.type == TokenType::TOKEN_STRING) {
            fmt = current.text;
            advance();
        }

        std::vector<std::string> args;
        while (match(TokenType::TOKEN_COMMA)) {
            args.push_back(current.text);
            advance();
        }

        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $display / $monitor argument list");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $display / $monitor");
        return std::make_unique<DisplayStatementNode>(fmt, args, is_mon, has_lead_time);
    }

    if (match(TokenType::TOKEN_DUMPFILE)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after $dumpfile");
        std::string fname = current.text;
        expect(TokenType::TOKEN_STRING, "Expected string literal inside $dumpfile(\"...\")");
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $dumpfile");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $dumpfile");
        return std::make_unique<DumpFileStatementNode>(fname);
    }

    if (match(TokenType::TOKEN_DUMPVARS)) {
        if (match(TokenType::TOKEN_LPAREN)) {
            while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                advance();
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $dumpvars");
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $dumpvars");
        return std::make_unique<DumpVarsStatementNode>();
    }

    if (match(TokenType::TOKEN_FINISH) || match(TokenType::TOKEN_STOP)) {
        if (match(TokenType::TOKEN_LPAREN)) expect(TokenType::TOKEN_RPAREN, "Expected ')'");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $finish/$stop");
        return std::make_unique<FinishStatementNode>();
    }

    if (match(TokenType::TOKEN_IF)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'if'");
        auto cond = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' after 'if' condition");

        auto if_node = std::make_unique<IfStatementNode>(std::move(cond));
        if_node->then_branch = parse_statement_or_block();

        if (match(TokenType::TOKEN_ELSE)) {
            if_node->else_branch = parse_statement_or_block();
        }
        return if_node;
    }

    if (match(TokenType::TOKEN_CASE)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'case'");
        auto sel = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing case selector");

        auto case_node = std::make_unique<CaseStatementNode>(std::move(sel));

        while (current.type != TokenType::TOKEN_ENDCASE && current.type != TokenType::TOKEN_EOF) {
            CaseItem item;
            if (match(TokenType::TOKEN_DEFAULT)) {
                item.is_default = true;
                match(TokenType::TOKEN_COLON);
            } else {
                item.match_expr = parse_expression();
                expect(TokenType::TOKEN_COLON, "Expected ':' after case match expression");
            }
            item.statements = parse_statement_or_block();
            case_node->items.push_back(std::move(item));
        }

        expect(TokenType::TOKEN_ENDCASE, "Expected 'endcase'");
        return case_node;
    }

    // Concatenation assignment: {cout, sum} = a + b + cin;
    if (match(TokenType::TOKEN_LBRACE)) {
        std::vector<std::string> targets;
        while (true) {
            targets.push_back(current.text);
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier in concatenation target list");
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_RBRACE, "Expected '}' closing concatenation target list");
        expect(TokenType::TOKEN_EQUAL, "Expected '=' in concatenation blocking assignment");
        auto rhs = parse_expression();
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after concatenation assignment");
        return std::make_unique<BlockingAssignNode>(targets, std::move(rhs));
    }

    // Identifier-driven statements (assignment, memory write, or task call)
    if (current.type == TokenType::TOKEN_IDENTIFIER) {
        std::string ident = current.text;
        advance();

        // Memory array write: mem[addr] <= rhs or mem[addr] = rhs
        if (match(TokenType::TOKEN_LBRACKET)) {
            auto addr_expr = parse_expression();
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in memory assignment index");

            bool is_nba = false;
            if (match(TokenType::TOKEN_LE)) is_nba = true;
            else expect(TokenType::TOKEN_EQUAL, "Expected '=' or '<=' in memory assignment");

            auto rhs = parse_expression();
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after memory assignment");
            return std::make_unique<MemAssignNode>(ident, std::move(addr_expr), std::move(rhs), is_nba);
        }

        if (match(TokenType::TOKEN_EQUAL)) {
            auto rhs = parse_expression();
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after blocking assignment");
            return std::make_unique<BlockingAssignNode>(ident, std::move(rhs));
        }

        if (match(TokenType::TOKEN_LE)) {
            auto rhs = parse_expression();
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after non-blocking assignment");
            return std::make_unique<NonBlockingAssignNode>(ident, std::move(rhs));
        }

        if (match(TokenType::TOKEN_LPAREN)) {
            expect(TokenType::TOKEN_RPAREN, "Expected ')' in task call");
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after statement");
        return std::make_unique<TaskCallNode>(ident);
    }

    std::string loc = current.filename.empty() ? ("line " + std::to_string(current.line)) : (current.filename + ":" + std::to_string(current.line));
    throw std::runtime_error(loc + ": error: Unexpected statement beginning with '" + current.text + "'");
}

std::vector<std::unique_ptr<StatementNode>> Parser::parse_statement_or_block() {
    std::vector<std::unique_ptr<StatementNode>> stmts;
    if (match(TokenType::TOKEN_BEGIN)) {
        while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
            stmts.push_back(parse_statement());
        }
        expect(TokenType::TOKEN_END, "Expected 'end' closing begin block");
    } else {
        stmts.push_back(parse_statement());
    }
    return stmts;
}

std::unique_ptr<ProcessNode> Parser::parse_always_ff() {
    expect(TokenType::TOKEN_AT, "Expected '@' after always_ff");
    expect(TokenType::TOKEN_LPAREN, "Expected '(' in sensitivity list");

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
    return proc;
}

// -------------------------------------------------------------
// Top-Level Module & File Parsing
// -------------------------------------------------------------

std::vector<std::unique_ptr<ModuleNode>> Parser::parse_all() {
    std::vector<std::unique_ptr<ModuleNode>> modules;

    while (current.type != TokenType::TOKEN_EOF) {
        // Handle compilation-unit scope parameters (e.g. from `include "defs.vh"`)
        if (current.type == TokenType::TOKEN_PARAMETER || current.type == TokenType::TOKEN_LOCALPARAM) {
            advance(); // consume parameter / localparam
            
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

        // Module parsing
        expect(TokenType::TOKEN_MODULE, "Expected 'module', 'parameter', or 'localparam' keyword");
        auto mod = std::make_unique<ModuleNode>();
        mod->defined_in_file = current.filename;
        mod->defined_on_line = current.line;
        mod->name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected module name");

        // Inherit global compilation-unit parameters into this module
        for (const auto& gp : global_parameters) {
            ParameterDef pcopy;
            pcopy.name = gp.name;
            pcopy.default_expr = gp.default_expr ? gp.default_expr->clone() : nullptr;
            mod->parameters.push_back(std::move(pcopy));
        }

        // Check for parameterized port syntax: #(parameter ...)
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

        // Parse Ports in module header (ANSI or Non-ANSI list)
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
                        if (current.type == TokenType::TOKEN_INPUT || current.type == TokenType::TOKEN_OUTPUT || current.type == TokenType::TOKEN_INOUT ||
                            current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_REG) {
                            break;
                        }
                        continue;
                    }
                    break;
                }
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing port list");
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after module header");

        // Parse module body until endmodule
        while (current.type != TokenType::TOKEN_ENDMODULE && current.type != TokenType::TOKEN_EOF) {
            // Localparam & Parameter inside module body
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

            // Input / Output / Inout declarations (Non-ANSI definitions)
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

            // Internal signals: logic, wire, reg
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

                    // Check for memory array declaration: logic [7:0] mem [0:255];
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

            // Continuous Assign
            if (match(TokenType::TOKEN_ASSIGN)) {
                std::string target = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected target identifier in assign");
                expect(TokenType::TOKEN_EQUAL, "Expected '=' in assign statement");
                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assign");
                mod->continuous_assigns.push_back(std::make_unique<ContinuousAssignNode>(target, std::move(rhs)));
                continue;
            }

            // Gate Primitives (and, or, xor, nand, nor, xnor, not, buf)
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

            // Tasks
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

            // Initial block
            if (match(TokenType::TOKEN_INITIAL)) {
                auto proc = std::make_unique<ProcessNode>(ProcessType::INITIAL);
                proc->body = parse_statement_or_block();
                mod->processes.push_back(std::move(proc));
                continue;
            }

            // always_comb block
            if (match(TokenType::TOKEN_ALWAYS_COMB)) {
                auto proc = std::make_unique<ProcessNode>(ProcessType::COMBINATIONAL);
                proc->body = parse_statement_or_block();
                mod->processes.push_back(std::move(proc));
                continue;
            }

            // always_ff block
            if (match(TokenType::TOKEN_ALWAYS_FF)) {
                mod->processes.push_back(parse_always_ff());
                continue;
            }

            // always block (check for @(posedge ...))
            if (match(TokenType::TOKEN_ALWAYS)) {
                if (current.type == TokenType::TOKEN_AT) {
                    advance(); // consume '@'
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

            // Module Instantiation: <mod_type> #(.PARAM(val)) inst_name (.port(sig), ...);
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

            advance(); // Skip unexpected token safely
        }
        expect(TokenType::TOKEN_ENDMODULE, "Expected 'endmodule'");
        modules.push_back(std::move(mod));
    }

    return modules;
}