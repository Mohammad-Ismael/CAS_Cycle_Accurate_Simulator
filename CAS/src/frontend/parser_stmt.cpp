#include "../../include/frontend/parser.hpp"
#include <stdexcept>

std::unique_ptr<StatementNode> Parser::parse_statement() {
    if (match(TokenType::TOKEN_HASH)) {
        uint64_t d = current.num_val;
        expect(TokenType::TOKEN_NUMBER, "Expected numerical delay value after '#'");
        match(TokenType::TOKEN_SEMICOLON);
        return std::make_unique<DelayStatementNode>(d);
    }

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

    if (match(TokenType::TOKEN_DUMPFILE)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after $dumpfile");
        std::string fname = current.text;
        expect(TokenType::TOKEN_STRING, "Expected string literal inside $dumpfile(\"...\")");
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $dumpfile");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $dumpfile");
        return std::make_unique<DumpFileStatementNode>(fname);
    }

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

    if (match(TokenType::TOKEN_FINISH) || match(TokenType::TOKEN_STOP)) {
        std::string origin_file = current.filename;
        size_t origin_line = current.line;
        if (match(TokenType::TOKEN_LPAREN)) expect(TokenType::TOKEN_RPAREN, "Expected ')'");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $finish/$stop");
        return std::make_unique<FinishStatementNode>(origin_file, origin_line);
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

    if (current.type == TokenType::TOKEN_IDENTIFIER) {
        std::string ident = current.text;
        advance();

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

    std::string loc = current.filename.empty() 
        ? ("line " + std::to_string(current.line)) 
        : (current.filename + ":" + std::to_string(current.line));
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