#include "../../include/frontend/parser.hpp"
#include <stdexcept>

std::unique_ptr<StatementNode> Parser::parse_statement() {
    // 0. Handle 'begin ... end' block statements
    if (match(TokenType::TOKEN_BEGIN)) {
        std::vector<std::unique_ptr<StatementNode>> stmts;
        while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
            stmts.push_back(parse_statement());
        }
        expect(TokenType::TOKEN_END, "Expected 'end' closing begin block");
        match(TokenType::TOKEN_SEMICOLON);
        return std::make_unique<BlockStatementNode>(std::move(stmts));
    }

    // 1. Variable Declarations in Procedural Scopes (integer i; logic [7:0] temp;)
    if (current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_LOGIC ||
        current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_WIRE) {
        bool is_signed_var = (current.type == TokenType::TOKEN_INTEGER);
        advance();
        if (current.type == TokenType::TOKEN_SIGNED) {
            is_signed_var = true;
            advance();
        }

        uint32_t width = 32;
        if (match(TokenType::TOKEN_LBRACKET)) {
            auto msb_e = parse_expression();
            expect(TokenType::TOKEN_COLON, "Expected ':' in range");
            auto lsb_e = parse_expression();
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in range");
            if (auto nm = dynamic_cast<NumberNode*>(msb_e.get())) {
                if (auto nl = dynamic_cast<NumberNode*>(lsb_e.get())) {
                    width = (nm->aval >= nl->aval) ? (uint32_t)(nm->aval - nl->aval + 1) : (uint32_t)(nl->aval - nm->aval + 1);
                }
            }
        }

        std::string vname = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected variable identifier");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after variable declaration");
        return std::make_unique<VarDeclStatementNode>(vname, width, is_signed_var);
    }

    // 2. Delay Statement (#5; or #5 clk = ~clk;)
    if (match(TokenType::TOKEN_HASH)) {
        uint64_t d = current.num_val;
        expect(TokenType::TOKEN_NUMBER, "Expected numerical delay value after '#'");
        
        if (match(TokenType::TOKEN_SEMICOLON)) {
            return std::make_unique<DelayStatementNode>(d);
        }

        auto target_stmt = parse_statement();
        std::vector<std::unique_ptr<StatementNode>> stmts;
        stmts.push_back(std::make_unique<DelayStatementNode>(d));
        stmts.push_back(std::move(target_stmt));
        return std::make_unique<BlockStatementNode>(std::move(stmts));
    }

    // 3. Procedural Event Control: @(posedge clk), @(negedge clk), @(signal)
    if (match(TokenType::TOKEN_AT)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after '@'");
        EdgeType edge = EdgeType::LEVEL;
        if (match(TokenType::TOKEN_POSEDGE)) edge = EdgeType::POSEDGE;
        else if (match(TokenType::TOKEN_NEGEDGE)) edge = EdgeType::NEGEDGE;

        std::string sig = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier in event control");
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing event control");
        match(TokenType::TOKEN_SEMICOLON);
        return std::make_unique<EventControlStatementNode>(edge, sig);
    }

    // 4. Forever Loop
    if (match(TokenType::TOKEN_FOREVER)) {
        auto body = parse_statement_or_block();
        return std::make_unique<ForeverStatementNode>(std::move(body));
    }

    // 5. For Loop
    if (match(TokenType::TOKEN_FOR)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'for'");
        auto init_stmt = parse_statement();
        auto cond_expr = parse_expression();
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after for condition");
        
        std::string target = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected loop variable in for step");
        expect(TokenType::TOKEN_EQUAL, "Expected '=' in for step");
        auto step_rhs = parse_expression();
        auto step_stmt = std::make_unique<BlockingAssignNode>(target, std::move(step_rhs));
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing for header");

        auto body = parse_statement_or_block();
        return std::make_unique<ForStatementNode>(std::move(init_stmt), std::move(cond_expr), std::move(step_stmt), std::move(body));
    }

    // 6. While Loop
    if (match(TokenType::TOKEN_WHILE)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'while'");
        auto cond = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing while condition");
        auto body = parse_statement_or_block();
        return std::make_unique<WhileStatementNode>(std::move(cond), std::move(body));
    }

    // 7. Repeat Loop
    if (match(TokenType::TOKEN_REPEAT)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'repeat'");
        auto count = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing repeat count");
        auto body = parse_statement_or_block();
        return std::make_unique<RepeatStatementNode>(std::move(count), std::move(body));
    }

    // 8. System Tasks ($display / $monitor)
    if (current.type == TokenType::TOKEN_DISPLAY || current.type == TokenType::TOKEN_MONITOR) {
        bool is_mon = (current.type == TokenType::TOKEN_MONITOR);
        std::string origin_file = current.filename;
        size_t origin_line = current.line;

        advance();
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

        std::vector<std::unique_ptr<ExpressionNode>> args;
        while (match(TokenType::TOKEN_COMMA)) {
            args.push_back(parse_expression());
        }

        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $display / $monitor argument list");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $display / $monitor");
        return std::make_unique<DisplayStatementNode>(fmt, std::move(args), is_mon, has_lead_time, origin_file, origin_line);
    }

    // 9. $dumpfile
    if (match(TokenType::TOKEN_DUMPFILE)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after $dumpfile");
        std::string fname = current.text;
        expect(TokenType::TOKEN_STRING, "Expected string literal inside $dumpfile(\"...\")");
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $dumpfile");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $dumpfile");
        return std::make_unique<DumpFileStatementNode>(fname);
    }

    // 10. $dumpvars
    if (match(TokenType::TOKEN_DUMPVARS)) {
        uint32_t levels = 0;
        if (match(TokenType::TOKEN_LPAREN)) {
            if (current.type == TokenType::TOKEN_NUMBER) {
                levels = (uint32_t)current.num_val;
                advance();
            }
            while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                advance();
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $dumpvars");
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $dumpvars");
        return std::make_unique<DumpVarsStatementNode>(levels);
    }

    // 11. $finish / $stop
    if (match(TokenType::TOKEN_FINISH) || match(TokenType::TOKEN_STOP)) {
        std::string origin_file = current.filename;
        size_t origin_line = current.line;
        if (match(TokenType::TOKEN_LPAREN)) expect(TokenType::TOKEN_RPAREN, "Expected ')'");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $finish/$stop");
        return std::make_unique<FinishStatementNode>(origin_file, origin_line);
    }

    // 12. If Statement
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

    // 13. Case Statement
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

    // 14. Multi-target concatenation assignment ({a, b} = expr;)
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
        
        uint64_t intra_delay = 0;
        if (match(TokenType::TOKEN_HASH)) {
            intra_delay = current.num_val;
            expect(TokenType::TOKEN_NUMBER, "Expected numerical intra-assignment delay value");
        }

        auto rhs = parse_expression();
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after concatenation assignment");
        return std::make_unique<BlockingAssignNode>(targets, std::move(rhs), intra_delay);
    }

    // 15. Identifiers (Assignments, Bit-Selects, Memory Writes, Task Calls)
    if (current.type == TokenType::TOKEN_IDENTIFIER) {
        std::string ident = current.text;
        advance();

        if (match(TokenType::TOKEN_LBRACKET)) {
            auto idx_expr = parse_expression();
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in index select");

            if (match(TokenType::TOKEN_EQUAL)) {
                uint64_t intra_delay = 0;
                if (match(TokenType::TOKEN_HASH)) {
                    intra_delay = current.num_val;
                    expect(TokenType::TOKEN_NUMBER, "Expected numerical intra-assignment delay value");
                }
                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assignment");
                return std::make_unique<BlockingAssignNode>(ident, std::move(rhs), std::move(idx_expr), intra_delay);
            }
            if (match(TokenType::TOKEN_LE)) {
                uint64_t intra_delay = 0;
                if (match(TokenType::TOKEN_HASH)) {
                    intra_delay = current.num_val;
                    expect(TokenType::TOKEN_NUMBER, "Expected numerical intra-assignment delay value");
                }
                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after non-blocking assignment");
                return std::make_unique<MemAssignNode>(ident, std::move(idx_expr), std::move(rhs), true, intra_delay);
            }
        }

        if (match(TokenType::TOKEN_EQUAL)) {
            uint64_t intra_delay = 0;
            if (match(TokenType::TOKEN_HASH)) {
                intra_delay = current.num_val;
                expect(TokenType::TOKEN_NUMBER, "Expected numerical intra-assignment delay value");
            }
            auto rhs = parse_expression();
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after blocking assignment");
            return std::make_unique<BlockingAssignNode>(ident, std::move(rhs), nullptr, intra_delay);
        }

        if (match(TokenType::TOKEN_LE)) {
            uint64_t intra_delay = 0;
            if (match(TokenType::TOKEN_HASH)) {
                intra_delay = current.num_val;
                expect(TokenType::TOKEN_NUMBER, "Expected numerical intra-assignment delay value");
            }
            auto rhs = parse_expression();
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after non-blocking assignment");
            return std::make_unique<NonBlockingAssignNode>(ident, std::move(rhs), intra_delay);
        }

        if (match(TokenType::TOKEN_LPAREN)) {
            expect(TokenType::TOKEN_RPAREN, "Expected ')' in task call");
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after statement");
        return std::make_unique<TaskCallNode>(ident);
    }

    std::string loc = current.filename.empty() 
        ? ("line " + std::to_string(current.line)) 
        : (current.filename + ":" + std::to_string(current.line));
    throw std::runtime_error(loc + ": error: Unexpected statement beginning with '" + current.text + "'");
}

std::vector<std::unique_ptr<StatementNode>> Parser::parse_statement_or_block() {
    std::vector<std::unique_ptr<StatementNode>> stmts;
    if (match(TokenType::TOKEN_BEGIN)) {
        while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
            auto s = parse_statement();
            if (auto blk = dynamic_cast<BlockStatementNode*>(s.get())) {
                for (auto& sub : blk->statements) {
                    stmts.push_back(std::move(sub));
                }
            } else {
                stmts.push_back(std::move(s));
            }
        }
        expect(TokenType::TOKEN_END, "Expected 'end' closing begin block");
        match(TokenType::TOKEN_SEMICOLON);
    } else {
        auto s = parse_statement();
        if (auto blk = dynamic_cast<BlockStatementNode*>(s.get())) {
            for (auto& sub : blk->statements) {
                stmts.push_back(std::move(sub));
            }
        } else {
            stmts.push_back(std::move(s));
        }
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