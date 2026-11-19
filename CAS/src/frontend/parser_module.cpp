#include "../../include/frontend/parser.hpp"
#include <functional>
#include <stdexcept>

static bool validate_time_literal_str(const std::string& s) {
    if (s.empty()) return false;
    std::string num_part = "";
    size_t i = 0;
    while (i < s.size() && (std::isdigit(s[i]) || s[i] == '_')) {
        if (s[i] == '_') return false;
        num_part += s[i];
        i++;
    }
    std::string unit_part = s.substr(i);
    if (num_part != "1" && num_part != "10" && num_part != "100") return false;
    if (unit_part != "s" && unit_part != "ms" && unit_part != "us" && unit_part != "ns" && unit_part != "ps" && unit_part != "fs") return false;
    return true;
}

static bool expr_has_xz(const ExpressionNode* node) {
    if (!node) return false;
    if (auto num = dynamic_cast<const NumberNode*>(node)) {
        return num->bval != 0;
    }
    if (auto rep = dynamic_cast<const ReplicateNode*>(node)) {
        return expr_has_xz(rep->count_expr.get()) || expr_has_xz(rep->target_expr.get());
    }
    if (auto cat = dynamic_cast<const ConcatNode*>(node)) {
        for (const auto& elem : cat->elements) {
            if (expr_has_xz(elem.get())) return true;
        }
        return false;
    }
    if (auto un = dynamic_cast<const UnaryOpNode*>(node)) {
        return expr_has_xz(un->operand.get());
    }
    if (auto bin = dynamic_cast<const BinaryOpNode*>(node)) {
        return expr_has_xz(bin->lhs.get()) || expr_has_xz(bin->rhs.get());
    }
    if (auto tern = dynamic_cast<const TernaryOpNode*>(node)) {
        return expr_has_xz(tern->condition.get()) || expr_has_xz(tern->true_expr.get()) || expr_has_xz(tern->false_expr.get());
    }
    if (auto cast_node = dynamic_cast<const CastNode*>(node)) {
        return expr_has_xz(cast_node->expr.get());
    }
    return false;
}

std::unique_ptr<ExpressionNode> Parser::parse_net_lvalue() {
    std::string name = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected net identifier in alias statement");
    while (match(TokenType::TOKEN_DOT)) {
        name += "." + current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in alias statement");
    }
    if (match(TokenType::TOKEN_LBRACKET)) {
        auto msb = parse_expression();
        if (match(TokenType::TOKEN_COLON)) {
            auto lsb = parse_expression();
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing part-select range in alias statement");
            return std::make_unique<IndexSelectNode>(name, std::move(msb), std::move(lsb));
        }
        expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing bit-select index in alias statement");
        return std::make_unique<IndexSelectNode>(name, std::move(msb));
    }
    return std::make_unique<IdentifierNode>(name);
}

std::shared_ptr<UdpDefinitionNode> Parser::parse_udp() {
    expect(TokenType::TOKEN_PRIMITIVE, "Expected 'primitive'");
    auto udp = std::make_shared<UdpDefinitionNode>();
    udp->name = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected primitive name");
    expect(TokenType::TOKEN_LPAREN, "Expected '(' after primitive name");

    // Check if ANSI style or non-ANSI port list
    if (current.type == TokenType::TOKEN_OUTPUT) {
        // ANSI style: output [reg] out, input a, b, c
        advance();
        if (current.type == TokenType::TOKEN_REG || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "reg")) {
            advance();
            udp->is_sequential = true;
        }
        udp->output_port = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected output port name");

        while (match(TokenType::TOKEN_COMMA)) {
            if (current.type == TokenType::TOKEN_INPUT) {
                advance();
            }
            while (true) {
                udp->input_ports.push_back(current.text);
                expect(TokenType::TOKEN_IDENTIFIER, "Expected input port name");
                if (match(TokenType::TOKEN_COMMA)) {
                    if (current.type == TokenType::TOKEN_INPUT) {
                        advance();
                        continue;
                    }
                    continue;
                }
                break;
            }
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing primitive port list");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after primitive header");
    } else {
        // Non-ANSI style: out, a, b, c
        std::vector<std::string> ports;
        while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
            ports.push_back(current.text);
            expect(TokenType::TOKEN_IDENTIFIER, "Expected port name in primitive");
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing primitive port list");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after primitive header");

        if (!ports.empty()) {
            udp->output_port = ports[0];
            for (size_t i = 1; i < ports.size(); ++i) {
                udp->input_ports.push_back(ports[i]);
            }
        }

        // Parse declarations before 'table'
        while (current.type != TokenType::TOKEN_TABLE && current.type != TokenType::TOKEN_EOF) {
            if (match(TokenType::TOKEN_OUTPUT)) {
                if (current.type == TokenType::TOKEN_REG || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "reg")) {
                    advance();
                    udp->is_sequential = true;
                }
                udp->output_port = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected output port identifier");
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after output declaration");
            } else if (match(TokenType::TOKEN_INPUT)) {
                while (true) {
                    std::string in_name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected input port identifier");
                    if (ports.empty()) udp->input_ports.push_back(in_name);
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after input declaration");
            } else if (match(TokenType::TOKEN_REG)) {
                udp->is_sequential = true;
                advance(); // port name
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after reg declaration");
            } else if (match(TokenType::TOKEN_INITIAL)) {
                while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_EOF) {
                    advance();
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after initial statement");
            } else {
                break;
            }
        }
    }

    if (match(TokenType::TOKEN_INITIAL)) {
        while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_EOF) {
            advance();
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after initial statement");
    }

    expect(TokenType::TOKEN_TABLE, "Expected 'table' in primitive");
    while (current.type != TokenType::TOKEN_ENDTABLE && current.type != TokenType::TOKEN_EOF) {
        UdpTableRow row;
        while (current.type != TokenType::TOKEN_COLON && current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_ENDTABLE && current.type != TokenType::TOKEN_EOF) {
            for (char ch : current.text) {
                if (!std::isspace(ch)) {
                    row.inputs.push_back(ch);
                }
            }
            advance();
        }
        expect(TokenType::TOKEN_COLON, "Expected ':' in UDP table row");
        char out_char = 'x';
        if (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_COLON && current.type != TokenType::TOKEN_ENDTABLE) {
            if (!current.text.empty()) out_char = current.text[0];
            advance();
        }
        if (match(TokenType::TOKEN_COLON)) {
            udp->is_sequential = true;
            char next_char = 'x';
            if (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_ENDTABLE) {
                if (!current.text.empty()) next_char = current.text[0];
                advance();
            }
            row.output = next_char;
        } else {
            row.output = out_char;
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' at end of table row");
        udp->table.push_back(std::move(row));
    }
    expect(TokenType::TOKEN_ENDTABLE, "Expected 'endtable'");
    expect(TokenType::TOKEN_ENDPRIMITIVE, "Expected 'endprimitive'");
    if (match(TokenType::TOKEN_COLON)) {
        expect(TokenType::TOKEN_IDENTIFIER, "Expected primitive name after ':'");
    }

    return udp;
}

std::vector<std::unique_ptr<ModuleNode>> Parser::parse_all() {
    std::vector<std::unique_ptr<ModuleNode>> modules;

    while (current.type != TokenType::TOKEN_EOF) {
        if (match(TokenType::TOKEN_SEMICOLON)) continue;
        if (current.type == TokenType::TOKEN_PARAMETER || current.type == TokenType::TOKEN_LOCALPARAM) {
            bool is_local = (current.type == TokenType::TOKEN_LOCALPARAM);
            advance();
            std::string ptype = "";
            while (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
                   current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_INTEGER ||
                   current.type == TokenType::TOKEN_SIGNED || current.type == TokenType::TOKEN_UNSIGNED || 
                   current.type == TokenType::TOKEN_BIT ||
                   (!ptype.empty() ? false : is_type_identifier(current.text)) ||
                   (current.type == TokenType::TOKEN_IDENTIFIER && 
                    (current.text == "int" || current.text == "byte" || current.text == "shortint" || 
                     current.text == "longint" || current.text == "unsigned" || current.text == "signed" ||
                     current.text == "string" || current.text == "real" || current.text == "chandle" ||
                     peek_token().type == TokenType::TOKEN_IDENTIFIER ||
                     peek_token().type == TokenType::TOKEN_UNSIGNED ||
                     peek_token().type == TokenType::TOKEN_SIGNED))) {
                if (ptype.empty()) ptype = current.text;
                advance();
                if (current.type == TokenType::TOKEN_LBRACKET) {
                    advance();
                    parse_expression();
                    if (match(TokenType::TOKEN_COLON)) {
                        parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
                }
            }
            if (match(TokenType::TOKEN_LBRACKET)) {
                parse_expression();
                if (match(TokenType::TOKEN_COLON)) {
                    parse_expression();
                }
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
            }

            while (true) {
                std::string pname = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter identifier");
                while (match(TokenType::TOKEN_LBRACKET)) {
                    parse_expression();
                    if (match(TokenType::TOKEN_COLON)) {
                        parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
                }
                expect(TokenType::TOKEN_EQUAL, "Expected '=' in parameter assignment");
                auto val_expr = parse_expression();

                ParameterDef pdef(pname, std::move(val_expr), is_local, ptype);
                ParameterDef pdef_cu(pname, pdef.default_expr ? pdef.default_expr->clone() : nullptr, is_local, ptype);
                cu_parameters[current_compilation_unit_id].push_back(std::move(pdef_cu));
                global_parameters.push_back(std::move(pdef));

                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after parameter declaration");
            continue;
        }

        if (current.type == TokenType::TOKEN_TYPEDEF) {
            auto td = parse_typedef();
            if (td) global_typedefs.push_back(std::move(td));
            continue;
        }

        if (current.type == TokenType::TOKEN_VIRTUAL && peek_token().type == TokenType::TOKEN_CLASS) {
            advance(); // consume 'virtual'
            auto cls = parse_class(false, true);
            if (cls) {
                std::shared_ptr<ClassNode> scls = std::move(cls);
                class_map[scls->name] = scls;
                bool found = false;
                for (auto& existing : global_classes) {
                    if (existing->name == scls->name) {
                        existing = scls;
                        found = true;
                        break;
                    }
                }
                if (!found) global_classes.push_back(scls);
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_INTERFACE && peek_token().type == TokenType::TOKEN_CLASS) {
            advance(); // consume 'interface'
            auto cls = parse_class(true);
            if (cls) {
                std::shared_ptr<ClassNode> scls = std::move(cls);
                class_map[scls->name] = scls;
                bool found = false;
                for (auto& existing : global_classes) {
                    if (existing->name == scls->name) {
                        existing = scls;
                        found = true;
                        break;
                    }
                }
                if (!found) global_classes.push_back(scls);
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_CLASS) {
            auto cls = parse_class();
            if (cls) {
                std::shared_ptr<ClassNode> scls = std::move(cls);
                class_map[scls->name] = scls;
                bool found = false;
                for (auto& existing : global_classes) {
                    if (existing->name == scls->name) {
                        existing = scls;
                        found = true;
                        break;
                    }
                }
                if (!found) global_classes.push_back(scls);
            }
            continue;
        }

        if (match(TokenType::TOKEN_TIMEUNIT)) {
            std::string t_unit = current.text;
            advance();
            if (current.type == TokenType::TOKEN_IDENTIFIER) {
                t_unit += current.text;
                advance();
            }
            std::string t_prec = "";
            if (match(TokenType::TOKEN_SLASH)) {
                t_prec = current.text;
                advance();
                if (current.type == TokenType::TOKEN_IDENTIFIER) {
                    t_prec += current.text;
                    advance();
                }
            }
            if (!validate_time_literal_str(t_unit)) {
                throw std::runtime_error("[CAS-TIME] Invalid time literal '" + t_unit + "' in timeunit per IEEE 1800-2023 §3.14.2");
            }
            if (!t_prec.empty() && !validate_time_literal_str(t_prec)) {
                throw std::runtime_error("[CAS-TIME] Invalid time literal '" + t_prec + "' in timeunit precision per IEEE 1800-2023 §3.14.2");
            }
            if (!global_timeunit_val.empty() && global_timeunit_val != t_unit) {
                throw std::runtime_error("[CAS-TIME] Repeated timeunit declaration '" + t_unit + "' differs from previous '" + global_timeunit_val + "' per IEEE 1800-2023 §3.14.2");
            }
            global_timeunit_val = t_unit;
            if (!t_prec.empty()) {
                if (!global_timeprec_val.empty() && global_timeprec_val != t_prec) {
                    throw std::runtime_error("[CAS-TIME] Repeated timeprecision declaration '" + t_prec + "' differs from previous '" + global_timeprec_val + "' per IEEE 1800-2023 §3.14.2");
                }
                global_timeprec_val = t_prec;
            }
            while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_EOF) advance();
            match(TokenType::TOKEN_SEMICOLON);
            continue;
        }
        if (match(TokenType::TOKEN_TIMEPRECISION)) {
            std::string t_prec = current.text;
            advance();
            if (current.type == TokenType::TOKEN_IDENTIFIER) {
                t_prec += current.text;
                advance();
            }
            if (!validate_time_literal_str(t_prec)) {
                throw std::runtime_error("[CAS-TIME] Invalid time literal '" + t_prec + "' in timeprecision per IEEE 1800-2023 §3.14.2");
            }
            if (!global_timeprec_val.empty() && global_timeprec_val != t_prec) {
                throw std::runtime_error("[CAS-TIME] Repeated timeprecision declaration '" + t_prec + "' differs from previous '" + global_timeprec_val + "' per IEEE 1800-2023 §3.14.2");
            }
            global_timeprec_val = t_prec;
            while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_EOF) advance();
            match(TokenType::TOKEN_SEMICOLON);
            continue;
        }

        if (current.type == TokenType::TOKEN_COVERGROUP) {
            auto cg = parse_covergroup();
            if (cg) {
                std::shared_ptr<CovergroupNode> scg = std::move(cg);
                covergroup_map[scg->name] = scg;
                global_covergroups.push_back(scg);
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_PROPERTY) {
            auto prop = parse_property();
            if (prop) {
                property_map[prop->name] = std::move(prop);
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_SEQUENCE) {
            auto seq = parse_sequence();
            if (seq) {
                sequence_map[seq->name] = std::move(seq);
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_PACKAGE) {
            auto pkg = parse_package();
            if (pkg) {
                global_packages[pkg->name] = pkg;
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_CONSTRAINT) {
            advance();
            if (match(TokenType::TOKEN_STATIC)) {}
            std::string class_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected class name in out-of-block constraint");
            expect(TokenType::TOKEN_COLON_COLON, "Expected '::' after class name");
            std::string cname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected constraint name");
            ClassNode* target_cls = find_class(class_name);
            if (target_cls) {
                auto it_c = target_cls->constraint_map.find(cname);
                if (it_c != target_cls->constraint_map.end()) {
                    it_c->second->is_extern = false;
                }
            }
            expect(TokenType::TOKEN_LBRACE, "Expected '{' after constraint name");
            int cdepth = 1;
            while (cdepth > 0 && current.type != TokenType::TOKEN_EOF) {
                if (current.type == TokenType::TOKEN_LBRACE) cdepth++;
                else if (current.type == TokenType::TOKEN_RBRACE) cdepth--;
                advance();
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_CONFIG) {
            auto cfg = parse_config();
            if (cfg) {
                global_configs[cfg->name] = cfg;
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_LET) {
            auto let_decl = parse_let_declaration();
            if (let_decl) {
                global_let_decls[let_decl->name] = let_decl;
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_IMPORT) {
            if (peek_token().type == TokenType::TOKEN_STRING) {
                auto dpi = parse_dpi_import();
                if (dpi) {
                    std::shared_ptr<DpiImportNode> sdpi = std::move(dpi);
                    dpi_import_map[sdpi->sv_name] = sdpi;
                    global_dpi_imports.push_back(sdpi);
                }
            } else {
                advance(); // consume 'import'
                while (true) {
                    std::string pkg_name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected package name in import");
                    expect(TokenType::TOKEN_COLON_COLON, "Expected '::' after package name in import");
                    if (match(TokenType::TOKEN_STAR)) {
                        global_wildcard_imports.push_back(pkg_name);
                    } else {
                        std::string item_name = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected item name or '*' in import");
                        global_explicit_imports[item_name] = {pkg_name, item_name};
                    }
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after import declaration");
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_EXPORT) {
            auto exp = parse_dpi_export();
            if (exp) {
                std::shared_ptr<DpiExportNode> sexp = std::move(exp);
                dpi_export_map[sexp->c_name] = sexp;
                dpi_export_map[sexp->sv_name] = sexp;
                global_dpi_exports.push_back(sexp);
            }
            continue;
        }

        if (match(TokenType::TOKEN_TIMEUNIT)) {
            std::string t_unit = current.text;
            advance();
            if (current.type == TokenType::TOKEN_IDENTIFIER) {
                t_unit += current.text;
                advance();
            }
            std::string t_prec = "";
            if (match(TokenType::TOKEN_SLASH)) {
                t_prec = current.text;
                advance();
                if (current.type == TokenType::TOKEN_IDENTIFIER) {
                    t_prec += current.text;
                    advance();
                }
            }
            if (!validate_time_literal_str(t_unit)) {
                throw std::runtime_error("[CAS-TIME] Invalid time literal '" + t_unit + "' in timeunit per IEEE 1800-2023 §3.14.2");
            }
            if (!t_prec.empty() && !validate_time_literal_str(t_prec)) {
                throw std::runtime_error("[CAS-TIME] Invalid time literal '" + t_prec + "' in timeunit precision per IEEE 1800-2023 §3.14.2");
            }
            if (!global_timeunit_val.empty() && global_timeunit_val != t_unit) {
                throw std::runtime_error("[CAS-TIME] Repeated timeunit declaration '" + t_unit + "' differs from previous '" + global_timeunit_val + "' per IEEE 1800-2023 §3.14.2");
            }
            global_timeunit_val = t_unit;
            if (!t_prec.empty()) {
                if (!global_timeprec_val.empty() && global_timeprec_val != t_prec) {
                    throw std::runtime_error("[CAS-TIME] Repeated timeprecision declaration '" + t_prec + "' differs from previous '" + global_timeprec_val + "' per IEEE 1800-2023 §3.14.2");
                }
                global_timeprec_val = t_prec;
            }
            while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_EOF) advance();
            match(TokenType::TOKEN_SEMICOLON);
            continue;
        }
        if (match(TokenType::TOKEN_TIMEPRECISION)) {
            std::string t_prec = current.text;
            advance();
            if (current.type == TokenType::TOKEN_IDENTIFIER) {
                t_prec += current.text;
                advance();
            }
            if (!validate_time_literal_str(t_prec)) {
                throw std::runtime_error("[CAS-TIME] Invalid time literal '" + t_prec + "' in timeprecision per IEEE 1800-2023 §3.14.2");
            }
            if (!global_timeprec_val.empty() && global_timeprec_val != t_prec) {
                throw std::runtime_error("[CAS-TIME] Repeated timeprecision declaration '" + t_prec + "' differs from previous '" + global_timeprec_val + "' per IEEE 1800-2023 §3.14.2");
            }
            global_timeprec_val = t_prec;
            while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_EOF) advance();
            match(TokenType::TOKEN_SEMICOLON);
            continue;
        }

        if (current.type == TokenType::TOKEN_PRIMITIVE) {
            auto udp = parse_udp();
            if (udp) {
                global_udp_defs[udp->name] = udp;
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_FATAL || current.type == TokenType::TOKEN_ERROR ||
            current.type == TokenType::TOKEN_WARNING || current.type == TokenType::TOKEN_INFO) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: severity system task '" + current.text + "' is not permitted in compilation unit scope per IEEE 1800-2023 section 20.10");
        }

        bool has_file_virt_qual = false;
        bool has_file_stat_qual = false;
        bool has_file_ext_qual = false;
        if (current.type == TokenType::TOKEN_VIRTUAL && peek_token().type != TokenType::TOKEN_CLASS && peek_token().type != TokenType::TOKEN_INTERFACE) {
            has_file_virt_qual = true;
            advance();
        }
        if (current.type == TokenType::TOKEN_STATIC) {
            has_file_stat_qual = true;
            advance();
        }
        if (current.type == TokenType::TOKEN_EXTERN) {
            has_file_ext_qual = true;
            advance();
        }

        if (match(TokenType::TOKEN_FUNCTION)) {
            auto fn = parse_function();
            if (fn) {
                if (fn->name.find("::") != std::string::npos) {
                    if (has_file_virt_qual || has_file_stat_qual || has_file_ext_qual || fn->is_static) {
                        throw std::runtime_error("[CAS-DTINPCIL] Out-of-body method implementation cannot specify qualifiers ('virtual', 'static', 'extern') per IEEE 1800-2023 section 8.24: '" + fn->name + "'");
                    }
                    bind_out_of_body_function(std::move(fn));
                } else {
                    fn->compilation_unit_id = current_compilation_unit_id;
                    global_functions.push_back(std::move(fn));
                }
            }
            continue;
        }

        if (match(TokenType::TOKEN_TASK)) {
            auto t = parse_task();
            if (t) {
                if (t->name.find("::") != std::string::npos) {
                    if (has_file_virt_qual || has_file_stat_qual || has_file_ext_qual || t->is_static) {
                        throw std::runtime_error("[CAS-DTINPCIL] Out-of-body task implementation cannot specify qualifiers ('virtual', 'static', 'extern') per IEEE 1800-2023 section 8.24: '" + t->name + "'");
                    }
                    bind_out_of_body_task(std::move(t));
                } else {
                    t->compilation_unit_id = current_compilation_unit_id;
                    global_tasks.push_back(std::move(t));
                }
            }
            continue;
        }

        bool is_cu_var = false;
        std::string cu_type = "";
        if (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "const" || current.text == "var")) {
            is_cu_var = true;
            advance();
        }
        if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_BIT ||
            current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_WIRE ||
            current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_REAL ||
            current.type == TokenType::TOKEN_SHORTREAL || current.type == TokenType::TOKEN_EVENT ||
            (current.type == TokenType::TOKEN_IDENTIFIER &&
             (current.text == "string" || current.text == "int" || current.text == "byte" ||
              current.text == "shortint" || current.text == "longint" || is_type_identifier(current.text)))) {
            TokenType nt = peek_token().type;
            if (nt == TokenType::TOKEN_IDENTIFIER || nt == TokenType::TOKEN_LBRACKET || is_cu_var) {
                is_cu_var = true;
                cu_type = current.text;
                advance();
            }
        }
        if (is_cu_var) {
            if (match(TokenType::TOKEN_LBRACKET)) {
                parse_expression();
                if (match(TokenType::TOKEN_COLON)) parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in range");
            }
            while (true) {
                std::string vname = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected compilation unit variable identifier");
                std::unique_ptr<ExpressionNode> val_expr = nullptr;
                if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                    val_expr = parse_expression();
                }
                if (cu_type == "event") {
                    global_events.push_back(vname);
                } else if (val_expr != nullptr && cu_type != "reg" && cu_type != "wire") {
                    ParameterDef pdef_cu(vname, val_expr->clone(), false, cu_type);
                    cu_parameters[current_compilation_unit_id].push_back(std::move(pdef_cu));
                    ParameterDef pdef(vname, std::move(val_expr), false, cu_type);
                    global_parameters.push_back(std::move(pdef));
                } else {
                    PortNode sig(vname, "internal", nullptr, nullptr, false, NetKind::VAR);
                    sig.type_name = cu_type;
                    cu_signals[current_compilation_unit_id].push_back(sig.clone());
                    global_signals.push_back(std::move(sig));
                }
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after compilation unit variable declaration");
            continue;
        }

        bool is_iface = false;
        bool is_prog = false;
        if (current.type == TokenType::TOKEN_INTERFACE) {
            if (peek_token().type == TokenType::TOKEN_CLASS) {
                advance(); // consume 'interface'
                auto cls = parse_class(true);
                if (cls) {
                    std::shared_ptr<ClassNode> scls = std::move(cls);
                    class_map[scls->name] = scls;
                    global_classes.push_back(scls);
                }
                continue;
            }
            is_iface = true;
            advance();
        } else if (current.type == TokenType::TOKEN_PROGRAM) {
            is_prog = true;
            advance();
            if (current.type == TokenType::TOKEN_AUTOMATIC || current.type == TokenType::TOKEN_STATIC) {
                advance();
            }
        } else {
            expect(TokenType::TOKEN_MODULE, "Expected 'module', 'interface', 'program', 'package', 'config', 'primitive', 'parameter', or 'localparam' keyword");
        }
        auto mod = is_iface ? std::make_unique<InterfaceNode>() : 
                   (is_prog ? std::make_unique<ProgramNode>() : std::make_unique<ModuleNode>());
        current_parsing_module = mod.get();
        if (Lexer::global_timescale_encountered) {
            mod->has_timescale_directive = true;
            mod->default_timescale_unit = Lexer::global_time_unit_str;
            mod->default_timescale_prec = Lexer::global_time_precision_str;
        } else if (!global_timeunit_val.empty()) {
            mod->has_compilation_unit_timeunit = true;
            mod->default_timescale_unit = global_timeunit_val;
            mod->default_timescale_prec = !global_timeprec_val.empty() ? global_timeprec_val : Lexer::global_time_precision_str;
        } else {
            mod->default_timescale_unit = Lexer::global_time_unit_str;
            mod->default_timescale_prec = Lexer::global_time_precision_str;
        }
        for (const auto& w : global_wildcard_imports) {
            mod->wildcard_imports.push_back(w);
        }
        for (const auto& kv : global_explicit_imports) {
            mod->explicit_imports[kv.first] = kv.second;
        }
        if (current.type == TokenType::TOKEN_AUTOMATIC || current.type == TokenType::TOKEN_STATIC) {
            advance();
        }
        mod->defined_in_file = current.filename;
        mod->defined_on_line = current.line;
        mod->default_nettype = Lexer::get_default_nettype();
        mod->compilation_unit_id = current_compilation_unit_id;
        mod->name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, is_iface ? "Expected interface name" : (is_prog ? "Expected program name" : "Expected module name"));
        if (is_iface) known_interfaces.insert(mod->name);

        while (current.type == TokenType::TOKEN_IMPORT) {
            advance(); // consume 'import'
            while (true) {
                std::string pkg_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected package name in import");
                expect(TokenType::TOKEN_COLON_COLON, "Expected '::' after package name in import");
                if (match(TokenType::TOKEN_STAR)) {
                    if (std::find(mod->wildcard_imports.begin(), mod->wildcard_imports.end(), pkg_name) == mod->wildcard_imports.end()) {
                        mod->wildcard_imports.push_back(pkg_name);
                    }
                } else {
                    std::string item_name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected item name or '*' in import");
                    mod->explicit_imports[item_name] = {pkg_name, item_name};
                }
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after import declaration");
        }

        for (const auto& gp : global_parameters) {
            mod->parameters.push_back(gp);
        }
        for (const auto& gs : global_signals) {
            mod->signals.push_back(gs.clone());
        }
        for (const auto& ge : global_events) {
            mod->events.push_back(ge);
        }

        for (const auto& gt : global_typedefs) {
            if (gt->enum_type) mod->typedefs.push_back(std::make_unique<TypedefNode>(gt->type_name, gt->enum_type));
            else if (gt->struct_type) mod->typedefs.push_back(std::make_unique<TypedefNode>(gt->type_name, gt->struct_type));
        }

        for (const auto& gc : global_classes) {
            mod->classes.push_back(gc);
            mod->class_map[gc->name] = gc.get();
        }

        for (const auto& gdpi : global_dpi_imports) {
            mod->dpi_imports.push_back(gdpi);
            mod->dpi_import_map[gdpi->sv_name] = gdpi.get();
        }

        if (match(TokenType::TOKEN_HASH)) {
            expect(TokenType::TOKEN_LPAREN, "Expected '(' after '#' in module header");
            while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                bool is_local = (current.type == TokenType::TOKEN_LOCALPARAM);
                if (current.type == TokenType::TOKEN_PARAMETER || current.type == TokenType::TOKEN_LOCALPARAM) {
                    advance();
                }
                bool is_type_param = false;
                if (current.type == TokenType::TOKEN_TYPE || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "type")) {
                    is_type_param = true;
                    advance();
                }
                if (is_type_param) {
                    while (true) {
                        std::string pname = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter identifier");
                        std::unique_ptr<ExpressionNode> def_expr = nullptr;
                        if (match(TokenType::TOKEN_EQUAL)) {
                            if (current.type == TokenType::TOKEN_TYPE) {
                                def_expr = parse_expression();
                            } else {
                                std::string def_type = current.text;
                                advance();
                                def_expr = std::make_unique<IdentifierNode>(def_type);
                            }
                        }
                        mod->parameters.push_back(ParameterDef(pname, std::move(def_expr), is_local, "type"));
                        if (match(TokenType::TOKEN_COMMA)) continue;
                        break;
                    }
                    continue;
                }
                bool has_type = false;
                std::string ptype = "";
                bool is_signed = false;
                std::unique_ptr<ExpressionNode> p_msb;
                std::unique_ptr<ExpressionNode> p_lsb;
                bool has_range = false;
                while (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
                       current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_INTEGER ||
                       current.type == TokenType::TOKEN_SIGNED || current.type == TokenType::TOKEN_UNSIGNED || 
                       current.type == TokenType::TOKEN_BIT ||
                       (!has_type && is_type_identifier(current.text)) ||
                       (!has_type && current.type == TokenType::TOKEN_IDENTIFIER && (peek_token().type == TokenType::TOKEN_COLON_COLON || peek_token().type == TokenType::TOKEN_IDENTIFIER))) {
                    has_type = true;
                    if (current.type == TokenType::TOKEN_SIGNED) is_signed = true;
                    if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON_COLON) {
                        ptype = current.text;
                        advance();
                        ptype += current.text;
                        advance();
                        ptype += current.text;
                        advance();
                    } else {
                        if (ptype.empty()) ptype = current.text;
                        advance();
                    }
                    while (current.type == TokenType::TOKEN_LBRACKET) {
                        advance();
                        p_msb = parse_expression();
                        if (match(TokenType::TOKEN_COLON)) {
                            p_lsb = parse_expression();
                        }
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
                        has_range = true;
                    }
                }
                while (match(TokenType::TOKEN_LBRACKET)) {
                    p_msb = parse_expression();
                    if (match(TokenType::TOKEN_COLON)) {
                        p_lsb = parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
                    has_range = true;
                }
                std::string pname = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter identifier");
                while (match(TokenType::TOKEN_LBRACKET)) {
                    parse_expression();
                    if (match(TokenType::TOKEN_COLON)) {
                        parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
                }
                expect(TokenType::TOKEN_EQUAL, "Expected '=' in parameter definition");
                auto pexpr = parse_expression();
                ParameterDef pdef(pname, std::move(pexpr), is_local, ptype);
                if (has_range && p_msb) {
                    pdef.has_range = true;
                    pdef.msb_expr = p_msb->clone();
                    if (p_lsb) pdef.lsb_expr = p_lsb->clone();
                }
                pdef.is_signed = is_signed;
                mod->parameters.push_back(std::move(pdef));

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
                else if (current.type == TokenType::TOKEN_INTERFACE) {
                    advance();
                    std::string pname = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected interface port identifier");
                    PortNode port(pname, "interface");
                    port.is_interface = true;
                    mod->ports.push_back(std::move(port));
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                if (dir.empty() && current.type == TokenType::TOKEN_IDENTIFIER) {
                    Token next_tok = peek_token();
                    if (next_tok.type == TokenType::TOKEN_DOT) {
                        std::string iface_type = current.text;
                        advance();
                        expect(TokenType::TOKEN_DOT, "Expected '.' in modport port typing");
                        std::string mp_name = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected modport name");
                        std::string pname = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected port identifier");
                        PortNode port(pname, "interface");
                        port.is_interface = true;
                        port.interface_type_name = iface_type;
                        port.modport_name = mp_name;
                        port.type_name = iface_type + "." + mp_name;
                        mod->ports.push_back(std::move(port));
                        if (match(TokenType::TOKEN_COMMA)) continue;
                        break;
                    }
                    if (next_tok.type == TokenType::TOKEN_IDENTIFIER) {
                        std::string iface_type = current.text;
                        advance();
                        std::string pname = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected port identifier");
                        PortNode port(pname, "interface");
                        port.is_interface = true;
                        port.interface_type_name = iface_type;
                        port.type_name = iface_type;
                        mod->ports.push_back(std::move(port));
                        if (match(TokenType::TOKEN_COMMA)) continue;
                        break;
                    }
                }

                bool is_signed_port = false;
                bool is_int_port = false;
                std::string int_port_type = "";
                bool is_real_port = false;
                bool is_shortreal_port = false;
                if (current.type == TokenType::TOKEN_SIGNED) {
                    is_signed_port = true;
                    advance();
                }
                std::string port_user_type = "";
                if (current.type == TokenType::TOKEN_IDENTIFIER) {
                    Token next_tok = peek_token();
                    if (next_tok.type == TokenType::TOKEN_COLON_COLON) {
                        std::string pkg = current.text;
                        advance();
                        advance();
                        port_user_type = pkg + "::" + current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected type identifier after '::'");
                    } else if (next_tok.type == TokenType::TOKEN_IDENTIFIER ||
                               (next_tok.type == TokenType::TOKEN_LBRACKET && is_type_identifier(current.text))) {
                        if (current.text != "input" && current.text != "output" && current.text != "inout" &&
                            current.text != "int" && current.text != "byte" && current.text != "shortint" && current.text != "longint") {
                            port_user_type = current.text;
                            auto enum_info = get_enum_typedef(current.text);
                            if (enum_info && enum_info->is_signed) is_signed_port = true;
                            advance();
                        }
                    }
                }
                bool is_chandle_port = (current.type == TokenType::TOKEN_CHANDLE || 
                                       (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "chandle") ||
                                       port_user_type == "chandle");
                bool is_semaphore_port = (current.type == TokenType::TOKEN_SEMAPHORE || 
                                         (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "semaphore") ||
                                         port_user_type == "semaphore");
                bool is_mailbox_port = (current.type == TokenType::TOKEN_MAILBOX || 
                                       (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "mailbox") ||
                                       port_user_type == "mailbox");
                bool is_string_port = (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "string") ||
                                      port_user_type == "string";
                NetKind port_kind = (dir == "input" || dir == "inout") ? NetKind::WIRE : NetKind::WIRE;
                if (!port_user_type.empty()) {
                    if (is_class(port_user_type) || is_chandle_port || is_semaphore_port || is_mailbox_port || is_string_port) {
                        port_kind = NetKind::VAR;
                    }
                }
                if (is_chandle_port || is_semaphore_port || is_mailbox_port || is_string_port) {
                    port_kind = NetKind::VAR;
                    if (current.type == TokenType::TOKEN_CHANDLE || current.type == TokenType::TOKEN_SEMAPHORE ||
                        current.type == TokenType::TOKEN_MAILBOX ||
                        (current.type == TokenType::TOKEN_IDENTIFIER &&
                         (current.text == "chandle" || current.text == "semaphore" || current.text == "mailbox" || current.text == "string"))) {
                        advance();
                    }
                    if (is_mailbox_port && match(TokenType::TOKEN_HASH)) {
                        expect(TokenType::TOKEN_LPAREN, "Expected '(' in mailbox parameter");
                        while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) advance();
                        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing mailbox parameter");
                    }
                }
                if (current.type == TokenType::TOKEN_REAL || (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "real" || current.text == "realtime"))) {
                    is_real_port = true;
                    port_user_type = "real";
                    port_kind = NetKind::VAR;
                    advance();
                } else if (current.type == TokenType::TOKEN_SHORTREAL || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "shortreal")) {
                    is_shortreal_port = true;
                    port_user_type = "shortreal";
                    port_kind = NetKind::VAR;
                    advance();
                } else if (current.type == TokenType::TOKEN_IDENTIFIER &&
                    (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint")) {
                    is_int_port = true;
                    int_port_type = current.text;
                    is_signed_port = true;
                    port_kind = NetKind::VAR;
                    advance();
                } else if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
                    current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_BIT) {
                    port_kind = NetKind::VAR;
                    if (current.type == TokenType::TOKEN_INTEGER) is_signed_port = true;
                    if (current.type == TokenType::TOKEN_BIT) port_user_type = "bit";
                    advance();
                } else if (current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_WAND ||
                           current.type == TokenType::TOKEN_WOR || current.type == TokenType::TOKEN_TRI ||
                           (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "uwire" || current.text == "trireg" || current.text == "tri0" || current.text == "tri1"))) {
                    port_kind = (current.type == TokenType::TOKEN_WAND) ? NetKind::WAND :
                                ((current.type == TokenType::TOKEN_WOR) ? NetKind::WOR :
                                 ((current.type == TokenType::TOKEN_TRI) ? NetKind::TRI : NetKind::WIRE));
                    advance();
                    if (current.type == TokenType::TOKEN_SIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "signed")) {
                        is_signed_port = true; advance();
                    } else if (current.type == TokenType::TOKEN_UNSIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned")) {
                        is_signed_port = false; advance();
                    }
                    if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_BIT ||
                        current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_REAL ||
                        current.type == TokenType::TOKEN_SHORTREAL || current.type == TokenType::TOKEN_INTEGER ||
                        (current.type == TokenType::TOKEN_IDENTIFIER &&
                         (current.text == "int" || current.text == "byte" || current.text == "shortint" ||
                          current.text == "longint" || is_type_identifier(current.text)))) {
                        if (current.type == TokenType::TOKEN_INTEGER) is_signed_port = true;
                        if (current.type == TokenType::TOKEN_REAL || (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "real" || current.text == "realtime"))) {
                            is_real_port = true;
                            port_user_type = "real";
                        } else if (current.type == TokenType::TOKEN_SHORTREAL || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "shortreal")) {
                            is_shortreal_port = true;
                            port_user_type = "shortreal";
                        } else if (current.type == TokenType::TOKEN_BIT) {
                            port_user_type = "bit";
                        } else if (current.type == TokenType::TOKEN_IDENTIFIER) {
                            if (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint") {
                                is_int_port = true;
                                int_port_type = current.text;
                                is_signed_port = true;
                            } else {
                                port_user_type = current.text;
                                auto enum_info = get_enum_typedef(current.text);
                                if (enum_info && enum_info->is_signed) is_signed_port = true;
                            }
                        }
                        advance();
                    }
                }
                if (current.type == TokenType::TOKEN_SIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "signed")) {
                    is_signed_port = true;
                    advance();
                } else if (current.type == TokenType::TOKEN_UNSIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned")) {
                    is_signed_port = false;
                    advance();
                }

                std::vector<AstDimension> packed_dims;
                std::unique_ptr<ExpressionNode> msb = nullptr;
                std::unique_ptr<ExpressionNode> lsb = nullptr;
                while (match(TokenType::TOKEN_LBRACKET)) {
                    auto m = parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in port range");
                    auto l = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in port range");
                    AstDimension ad;
                    ad.left_expr = m->clone();
                    ad.right_expr = l->clone();
                    ad.is_single = false;
                    packed_dims.push_back(std::move(ad));
                    if (!msb) {
                        msb = std::move(m);
                        lsb = std::move(l);
                    }
                }
                if (packed_dims.size() > 1) {
                    uint32_t total_w = 1;
                    bool all_const = true;
                    for (const auto& pd : packed_dims) {
                        int64_t v1 = 0, v2 = 0;
                        if (auto nl = dynamic_cast<NumberNode*>(pd.left_expr.get())) v1 = (int64_t)nl->aval;
                        else all_const = false;
                        if (auto nr = dynamic_cast<NumberNode*>(pd.right_expr.get())) v2 = (int64_t)nr->aval;
                        else all_const = false;
                        if (all_const) {
                            uint32_t d_w = (v1 >= v2) ? (uint32_t)(v1 - v2 + 1) : (uint32_t)(v2 - v1 + 1);
                            total_w *= d_w;
                        }
                    }
                    if (all_const && total_w > 0) {
                        msb = std::make_unique<NumberNode>((uint64_t)(total_w - 1), 32);
                        lsb = std::make_unique<NumberNode>(0ULL, 32);
                    }
                } else if (packed_dims.empty()) {
                    if (is_chandle_port || is_semaphore_port || is_mailbox_port || is_string_port || is_real_port) {
                        msb = std::make_unique<NumberNode>(63, 32);
                        lsb = std::make_unique<NumberNode>(0, 32);
                    } else if (is_shortreal_port) {
                        msb = std::make_unique<NumberNode>(31, 32);
                        lsb = std::make_unique<NumberNode>(0, 32);
                    } else if (is_int_port) {
                        uint32_t iw = 32;
                        if (int_port_type == "byte") iw = 8;
                        else if (int_port_type == "shortint") iw = 16;
                        else if (int_port_type == "longint") iw = 64;
                        msb = std::make_unique<NumberNode>(iw - 1, 32);
                        lsb = std::make_unique<NumberNode>(0, 32);
                    }
                }

                while (true) {
                    std::string pname = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected port identifier");
                    PortNode port(pname, dir, msb ? msb->clone() : nullptr, lsb ? lsb->clone() : nullptr, is_signed_port, port_kind);
                    port.packed_dimensions = packed_dims;
                    while (match(TokenType::TOKEN_LBRACKET)) {
                        port.is_array = true;
                        auto l = parse_expression();
                        std::unique_ptr<ExpressionNode> r = nullptr;
                        bool is_single = true;
                        if (match(TokenType::TOKEN_COLON)) {
                            r = parse_expression();
                            is_single = false;
                        }
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                        AstDimension ad;
                        ad.left_expr = std::move(l);
                        ad.right_expr = std::move(r);
                        ad.is_single = is_single;
                        port.unpacked_dimensions.push_back(std::move(ad));
                    }
                    if (!port.unpacked_dimensions.empty()) {
                        port.bound1_expr = port.unpacked_dimensions[0].left_expr->clone();
                        if (port.unpacked_dimensions[0].right_expr) {
                            port.bound2_expr = port.unpacked_dimensions[0].right_expr->clone();
                        }
                    }
                    if (is_int_port) {
                        port.type_name = int_port_type;
                    }
                    if (is_chandle_port) {
                        port.is_chandle = true;
                        port.type_name = "chandle";
                    }
                    if (is_semaphore_port) {
                        port.is_semaphore = true;
                        port.type_name = "semaphore";
                    }
                    if (is_mailbox_port) {
                        port.is_mailbox = true;
                        port.type_name = "mailbox";
                    }
                    if (is_string_port) {
                        port.type_name = "string";
                    }
                    if (is_real_port) {
                        port.type_name = "real";
                    }
                    if (is_shortreal_port) {
                        port.type_name = "shortreal";
                    }
                    if (port_user_type == "bit") {
                        port.is_2state = true;
                        port.type_name = "bit";
                    }
                    if (port.type_name.empty() && !port_user_type.empty()) {
                        port.type_name = port_user_type;
                    }
                    if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                        port.default_value = parse_expression();
                    }
                    mod->ports.push_back(std::move(port));

                    if (match(TokenType::TOKEN_COMMA)) {
                        if (current.type == TokenType::TOKEN_RPAREN) {
                            break;
                        }
                        if (current.type == TokenType::TOKEN_INPUT || current.type == TokenType::TOKEN_OUTPUT || current.type == TokenType::TOKEN_INOUT ||
                            current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE || 
                            current.type == TokenType::TOKEN_WAND || current.type == TokenType::TOKEN_WOR || current.type == TokenType::TOKEN_TRI ||
                            current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_SIGNED ||
                            current.type == TokenType::TOKEN_UNSIGNED || current.type == TokenType::TOKEN_BIT ||
                            current.type == TokenType::TOKEN_INTERFACE ||
                            (current.type == TokenType::TOKEN_IDENTIFIER && (peek_token().type == TokenType::TOKEN_IDENTIFIER || peek_token().type == TokenType::TOKEN_DOT || peek_token().type == TokenType::TOKEN_SIGNED || peek_token().type == TokenType::TOKEN_UNSIGNED))) {
                            break;
                        }
                        continue;
                    }

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

        TokenType end_token = is_iface ? TokenType::TOKEN_ENDINTERFACE : (is_prog ? TokenType::TOKEN_ENDPROGRAM : TokenType::TOKEN_ENDMODULE);
        while (current.type != end_token && current.type != TokenType::TOKEN_EOF) {
            if (match(TokenType::TOKEN_TIMEUNIT)) {
                std::string t_unit = current.text;
                advance();
                if (current.type == TokenType::TOKEN_IDENTIFIER) {
                    t_unit += current.text;
                    advance();
                }
                std::string t_prec = "";
                if (match(TokenType::TOKEN_SLASH)) {
                    t_prec = current.text;
                    advance();
                    if (current.type == TokenType::TOKEN_IDENTIFIER) {
                        t_prec += current.text;
                        advance();
                    }
                }
                if (!validate_time_literal_str(t_unit)) {
                    throw std::runtime_error("[CAS-TIME] Invalid time literal '" + t_unit + "' in timeunit per IEEE 1800-2023 §3.14.2");
                }
                if (!t_prec.empty() && !validate_time_literal_str(t_prec)) {
                    throw std::runtime_error("[CAS-TIME] Invalid time literal '" + t_prec + "' in timeunit precision per IEEE 1800-2023 §3.14.2");
                }
                if (!mod->timeunit_val.empty() && mod->timeunit_val != t_unit) {
                    throw std::runtime_error("[CAS-TIME] Repeated timeunit declaration '" + t_unit + "' differs from previous '" + mod->timeunit_val + "' per IEEE 1800-2023 §3.14.2");
                }
                mod->timeunit_val = t_unit;
                if (!t_prec.empty()) {
                    if (!mod->timeprecision_val.empty() && mod->timeprecision_val != t_prec) {
                        throw std::runtime_error("[CAS-TIME] Repeated timeprecision declaration '" + t_prec + "' differs from previous '" + mod->timeprecision_val + "' per IEEE 1800-2023 §3.14.2");
                    }
                    mod->timeprecision_val = t_prec;
                }
                while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_EOF) advance();
                match(TokenType::TOKEN_SEMICOLON);
                continue;
            }
            if (match(TokenType::TOKEN_TIMEPRECISION)) {
                std::string t_prec = current.text;
                advance();
                if (current.type == TokenType::TOKEN_IDENTIFIER) {
                    t_prec += current.text;
                    advance();
                }
                if (!validate_time_literal_str(t_prec)) {
                    throw std::runtime_error("[CAS-TIME] Invalid time literal '" + t_prec + "' in timeprecision per IEEE 1800-2023 §3.14.2");
                }
                if (!mod->timeprecision_val.empty() && mod->timeprecision_val != t_prec) {
                    throw std::runtime_error("[CAS-TIME] Repeated timeprecision declaration '" + t_prec + "' differs from previous '" + mod->timeprecision_val + "' per IEEE 1800-2023 §3.14.2");
                }
                mod->timeprecision_val = t_prec;
                while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_EOF) advance();
                match(TokenType::TOKEN_SEMICOLON);
                continue;
            }
            if (current.type == TokenType::TOKEN_LET) {
                auto let_decl = parse_let_declaration();
                if (let_decl) {
                    mod->let_decls.push_back(let_decl);
                    mod->let_decl_map[let_decl->name] = let_decl;
                }
                continue;
            }
            if (is_prog && (current.type == TokenType::TOKEN_MODULE || current.type == TokenType::TOKEN_INTERFACE || current.type == TokenType::TOKEN_PROGRAM)) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: module, interface, or program declaration is illegal in program scope per IEEE 1800-2023 section 24.3");
            }
            if (current.type == TokenType::TOKEN_MODULE || (current.type == TokenType::TOKEN_INTERFACE && peek_token().type != TokenType::TOKEN_CLASS)) {
                // Nested module or interface declaration (IEEE 1800-2023 §23.4)
                advance();
                std::string nested_name = "";
                if (current.type == TokenType::TOKEN_IDENTIFIER) {
                    nested_name = current.text;
                    advance();
                }
                std::string nested_tu = "", nested_tp = "";
                while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_EOF) advance();
                match(TokenType::TOKEN_SEMICOLON);
                int nest_depth = 1;
                while (nest_depth > 0 && current.type != TokenType::TOKEN_EOF) {
                    if (current.type == TokenType::TOKEN_MODULE || (current.type == TokenType::TOKEN_INTERFACE && peek_token().type != TokenType::TOKEN_CLASS)) {
                        nest_depth++;
                    } else if (current.type == TokenType::TOKEN_ENDMODULE || current.type == TokenType::TOKEN_ENDINTERFACE) {
                        nest_depth--;
                        if (nest_depth == 0) {
                            advance();
                            break;
                        }
                    } else if (current.type == TokenType::TOKEN_TIMEUNIT) {
                        advance();
                        if (current.type == TokenType::TOKEN_IDENTIFIER || current.type == TokenType::TOKEN_NUMBER) {
                            nested_tu = current.text;
                            advance();
                            if (current.type == TokenType::TOKEN_IDENTIFIER) {
                                nested_tu += current.text;
                                advance();
                            }
                            if (match(TokenType::TOKEN_SLASH)) {
                                if (current.type == TokenType::TOKEN_IDENTIFIER || current.type == TokenType::TOKEN_NUMBER) {
                                    nested_tp = current.text;
                                    advance();
                                    if (current.type == TokenType::TOKEN_IDENTIFIER) {
                                        nested_tp += current.text;
                                        advance();
                                    }
                                }
                            }
                            continue;
                        }
                    } else if (current.type == TokenType::TOKEN_TIMEPRECISION) {
                        advance();
                        if (current.type == TokenType::TOKEN_IDENTIFIER || current.type == TokenType::TOKEN_NUMBER) {
                            nested_tp = current.text;
                            advance();
                            if (current.type == TokenType::TOKEN_IDENTIFIER) {
                                nested_tp += current.text;
                                advance();
                            }
                            continue;
                        }
                    }
                    advance();
                }
                if (!nested_name.empty()) {
                    mod->nested_timescales[nested_name] = {nested_tu, nested_tp};
                }
                if (match(TokenType::TOKEN_COLON)) {
                    if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
                }
                match(TokenType::TOKEN_SEMICOLON);
                continue;
            }
            if (current.type == TokenType::TOKEN_PROGRAM) {
                advance(); // consume 'program'
                std::string prog_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected program identifier");
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after program name");
                continue;
            }
            if (match(TokenType::TOKEN_ENDPROGRAM)) {
                if (match(TokenType::TOKEN_COLON)) {
                    if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
                }
                match(TokenType::TOKEN_SEMICOLON);
                continue;
            }
            if (current.type == TokenType::TOKEN_MODPORT || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "modport")) {
                advance();
                while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_EOF) {
                    std::string mp_name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected modport identifier");
                    auto mp_node = std::make_unique<ModportItemNode>(mp_name);
                    expect(TokenType::TOKEN_LPAREN, "Expected '(' in modport");
                    while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                        std::string pdir = "";
                        if (current.type == TokenType::TOKEN_INPUT) { pdir = "input"; advance(); }
                        else if (current.type == TokenType::TOKEN_OUTPUT) { pdir = "output"; advance(); }
                        else if (current.type == TokenType::TOKEN_INOUT) { pdir = "inout"; advance(); }
                        else {
                            throw std::runtime_error(current.filename + ":" + std::to_string(current.line) + 
                                ": error: Expected 'input', 'output', or 'inout' in modport port list (found: '" + current.text + "')");
                        }
                        while (true) {
                            std::string sig_name = current.text;
                            expect(TokenType::TOKEN_IDENTIFIER, "Expected signal name in modport");
                            mp_node->add_port(pdir, sig_name);
                            if (match(TokenType::TOKEN_COMMA)) {
                                if (current.type == TokenType::TOKEN_INPUT || 
                                    current.type == TokenType::TOKEN_OUTPUT || 
                                    current.type == TokenType::TOKEN_INOUT ||
                                    current.type == TokenType::TOKEN_RPAREN) {
                                    break;
                                }
                                continue;
                            }
                            break;
                        }
                    }
                    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing modport");
                    mod->modport_map[mp_name] = mp_node.get();
                    mod->modports.push_back(std::move(mp_node));
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after modport declaration");
                continue;
            }

            bool is_def_cb = false;
            if (current.type == TokenType::TOKEN_DEFAULT && peek_token().type == TokenType::TOKEN_CLOCKING) {
                advance(); // consume 'default'
                advance(); // consume 'clocking'
                if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_SEMICOLON) {
                    std::string cb_name = current.text;
                    advance(); // consume cb_name
                    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after default clocking");
                    mod->default_clocking_name = cb_name;
                    mod->default_clocking_nodes.push_back(std::make_unique<DefaultClockingNode>(cb_name));
                    continue;
                }
                is_def_cb = true;
            }
            if (current.type == TokenType::TOKEN_CLOCKING || is_def_cb) {
                if (!is_def_cb) advance();
                std::string cb_name = "";
                if (current.type == TokenType::TOKEN_IDENTIFIER) {
                    cb_name = current.text;
                    advance();
                } else if (is_def_cb) {
                    cb_name = "__default_cb";
                } else {
                    throw std::runtime_error(current.filename + ":" + std::to_string(current.line) + 
                        ": error: Expected clocking identifier");
                }
                expect(TokenType::TOKEN_AT, "Expected '@' in clocking block event control");
                expect(TokenType::TOKEN_LPAREN, "Expected '(' after '@'");
                EdgeType edge = EdgeType::LEVEL;
                if (match(TokenType::TOKEN_POSEDGE)) edge = EdgeType::POSEDGE;
                else if (match(TokenType::TOKEN_NEGEDGE)) edge = EdgeType::NEGEDGE;
                std::string clk_sig = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected clock signal identifier");
                while (match(TokenType::TOKEN_DOT)) {
                    clk_sig += "." + current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in clocking signal");
                }
                expect(TokenType::TOKEN_RPAREN, "Expected ')' closing clocking event");
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after clocking header");

                auto cb_node = std::make_unique<ClockingBlockNode>(cb_name, is_def_cb);
                cb_node->clock_edge = edge;
                cb_node->clock_signal = clk_sig;

                while (current.type != TokenType::TOKEN_ENDCLOCKING && current.type != TokenType::TOKEN_EOF) {
                    if (match(TokenType::TOKEN_DEFAULT)) {
                        while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_EOF) {
                            if (match(TokenType::TOKEN_INPUT)) {
                                cb_node->default_input_skew = parse_skew_spec();
                                if (cb_node->default_input_skew == "1step") {
                                    cb_node->default_input_skew_type = SkewType::STEP;
                                } else {
                                    cb_node->default_input_skew_type = SkewType::TIME;
                                }
                            } else if (match(TokenType::TOKEN_OUTPUT)) {
                                cb_node->default_output_skew = parse_skew_spec();
                                cb_node->default_output_skew_type = SkewType::TIME;
                                try {
                                    cb_node->default_output_skew_val = std::stoull(cb_node->default_output_skew);
                                } catch (...) {}
                            } else {
                                advance();
                            }
                        }
                        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after default clocking skew");
                        continue;
                    }

                    if (current.type == TokenType::TOKEN_INPUT || 
                        current.type == TokenType::TOKEN_OUTPUT || 
                        current.type == TokenType::TOKEN_INOUT) {
                        std::string pdir = current.text;
                        advance();
                        std::string skew = parse_skew_spec();
                        while (true) {
                            std::string sig = current.text;
                            expect(TokenType::TOKEN_IDENTIFIER, "Expected signal name in clocking item");
                            cb_node->add_item(pdir, sig, skew);
                            if (match(TokenType::TOKEN_COMMA)) continue;
                            break;
                        }
                        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after clocking signal list");
                        continue;
                    }

                    advance();
                }
                expect(TokenType::TOKEN_ENDCLOCKING, "Expected 'endclocking'");
                if (match(TokenType::TOKEN_COLON)) {
                    match(TokenType::TOKEN_IDENTIFIER);
                }
                if (is_def_cb || mod->default_clocking_name.empty()) {
                    if (is_def_cb) mod->default_clocking_name = cb_name;
                }
                mod->clocking_block_map[cb_name] = cb_node.get();
                mod->clocking_blocks.push_back(std::move(cb_node));
                continue;
            }
            if (current.type == TokenType::TOKEN_PARAMETER || current.type == TokenType::TOKEN_LOCALPARAM) {
                bool is_local = (current.type == TokenType::TOKEN_LOCALPARAM);
                advance();
                bool is_type_param = false;
                if (current.type == TokenType::TOKEN_TYPE || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "type")) {
                    is_type_param = true;
                    advance();
                }
                if (is_type_param) {
                    while (true) {
                        std::string pname = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter identifier");
                        expect(TokenType::TOKEN_EQUAL, "Expected '=' in type parameter assignment");
                        std::string def_type = current.text;
                        advance();
                        mod->simple_typedefs[pname] = {pname, def_type, 1, false};
                        mod->parameters.push_back(ParameterDef(pname, std::make_unique<IdentifierNode>(def_type), is_local, "type"));
                        if (match(TokenType::TOKEN_COMMA)) continue;
                        break;
                    }
                    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after type parameter declaration");
                    continue;
                }
                bool has_type = false;
                std::string ptype = "";
                bool is_signed = false;
                std::unique_ptr<ExpressionNode> p_msb;
                std::unique_ptr<ExpressionNode> p_lsb;
                bool has_range = false;
                while (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
                       current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_INTEGER ||
                       current.type == TokenType::TOKEN_SIGNED || current.type == TokenType::TOKEN_UNSIGNED || 
                       current.type == TokenType::TOKEN_BIT ||
                       (!has_type && is_type_identifier(current.text)) ||
                       (!has_type && current.type == TokenType::TOKEN_IDENTIFIER && (peek_token().type == TokenType::TOKEN_COLON_COLON || peek_token().type == TokenType::TOKEN_IDENTIFIER))) {
                    has_type = true;
                    if (current.type == TokenType::TOKEN_SIGNED) is_signed = true;
                    if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON_COLON) {
                        ptype = current.text;
                        advance();
                        ptype += current.text;
                        advance();
                        ptype += current.text;
                        advance();
                    } else {
                        if (ptype.empty()) ptype = current.text;
                        advance();
                    }
                    while (current.type == TokenType::TOKEN_LBRACKET) {
                        advance();
                        p_msb = parse_expression();
                        if (match(TokenType::TOKEN_COLON)) {
                            p_lsb = parse_expression();
                        }
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
                        has_range = true;
                    }
                }
                while (match(TokenType::TOKEN_LBRACKET)) {
                    p_msb = parse_expression();
                    if (match(TokenType::TOKEN_COLON)) {
                        p_lsb = parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
                    has_range = true;
                }
                while (true) {
                    std::string pname = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter identifier");
                    while (match(TokenType::TOKEN_LBRACKET)) {
                        parse_expression();
                        if (match(TokenType::TOKEN_COLON)) {
                            parse_expression();
                        }
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
                    }
                    expect(TokenType::TOKEN_EQUAL, "Expected '=' in parameter assignment");
                    auto val_expr = parse_expression();
                    ParameterDef pdef(pname, std::move(val_expr), is_local, ptype);
                    if (has_range && p_msb) {
                        pdef.has_range = true;
                        pdef.msb_expr = p_msb->clone();
                        if (p_lsb) pdef.lsb_expr = p_lsb->clone();
                    }
                    pdef.is_signed = is_signed;
                    if (mod && mod->referenced_wildcard_symbols.count(pname)) {
                        throw std::runtime_error("Declaration of '" + pname + "' conflicts with previous wildcard import reference per IEEE 1800-2023 section 26.3");
                    }
                    mod->parameters.push_back(std::move(pdef));
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after parameter declaration");
                continue;
            }

            if (current.type == TokenType::TOKEN_INPUT || current.type == TokenType::TOKEN_OUTPUT || current.type == TokenType::TOKEN_INOUT) {
                std::string dir = current.text;
                advance();
                bool is_signed_port = false;
                bool is_int_port = false;
                std::string int_port_type = "";
                bool is_real_port = false;
                bool is_shortreal_port = false;
                if (current.type == TokenType::TOKEN_SIGNED) {
                    is_signed_port = true;
                    advance();
                }
                std::string port_user_type = "";
                if (current.type == TokenType::TOKEN_IDENTIFIER) {
                    Token next_tok = peek_token();
                    if (next_tok.type == TokenType::TOKEN_COLON_COLON) {
                        std::string pkg = current.text;
                        advance();
                        advance();
                        port_user_type = pkg + "::" + current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected type identifier after '::'");
                    } else if (next_tok.type == TokenType::TOKEN_IDENTIFIER ||
                               (next_tok.type == TokenType::TOKEN_LBRACKET && is_type_identifier(current.text))) {
                        if (current.text != "input" && current.text != "output" && current.text != "inout" &&
                            current.text != "int" && current.text != "byte" && current.text != "shortint" && current.text != "longint") {
                            port_user_type = current.text;
                            auto enum_info = get_enum_typedef(current.text);
                            if (enum_info && enum_info->is_signed) is_signed_port = true;
                            advance();
                        }
                    }
                }
                bool is_chandle_port = (current.type == TokenType::TOKEN_CHANDLE || 
                                       (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "chandle") ||
                                       port_user_type == "chandle");
                bool is_semaphore_port = (current.type == TokenType::TOKEN_SEMAPHORE || 
                                         (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "semaphore") ||
                                         port_user_type == "semaphore");
                bool is_mailbox_port = (current.type == TokenType::TOKEN_MAILBOX || 
                                       (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "mailbox") ||
                                       port_user_type == "mailbox");
                bool is_string_port = (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "string") ||
                                      port_user_type == "string";
                NetKind port_kind = (dir == "input" || dir == "inout") ? NetKind::WIRE : NetKind::WIRE;
                if (!port_user_type.empty()) {
                    if (is_class(port_user_type) || is_chandle_port || is_semaphore_port || is_mailbox_port || is_string_port) {
                        port_kind = NetKind::VAR;
                    }
                }
                if (is_chandle_port || is_semaphore_port || is_mailbox_port || is_string_port) {
                    port_kind = NetKind::VAR;
                    if (current.type == TokenType::TOKEN_CHANDLE || current.type == TokenType::TOKEN_SEMAPHORE ||
                        current.type == TokenType::TOKEN_MAILBOX ||
                        (current.type == TokenType::TOKEN_IDENTIFIER &&
                         (current.text == "chandle" || current.text == "semaphore" || current.text == "mailbox" || current.text == "string"))) {
                        advance();
                    }
                    if (is_mailbox_port && match(TokenType::TOKEN_HASH)) {
                        expect(TokenType::TOKEN_LPAREN, "Expected '(' in mailbox parameter");
                        while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) advance();
                        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing mailbox parameter");
                    }
                } else if (current.type == TokenType::TOKEN_REAL || (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "real" || current.text == "realtime"))) {
                    is_real_port = true;
                    port_user_type = "real";
                    port_kind = NetKind::VAR;
                    advance();
                } else if (current.type == TokenType::TOKEN_SHORTREAL || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "shortreal")) {
                    is_shortreal_port = true;
                    port_user_type = "shortreal";
                    port_kind = NetKind::VAR;
                    advance();
                } else if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
                    current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_BIT) {
                    port_kind = NetKind::VAR;
                    if (current.type == TokenType::TOKEN_INTEGER) is_signed_port = true;
                    if (current.type == TokenType::TOKEN_BIT) port_user_type = "bit";
                    advance();
                } else if (current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_WAND ||
                           current.type == TokenType::TOKEN_WOR || current.type == TokenType::TOKEN_TRI ||
                           (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "uwire" || current.text == "trireg" || current.text == "tri0" || current.text == "tri1"))) {
                    port_kind = (current.type == TokenType::TOKEN_WAND) ? NetKind::WAND :
                                ((current.type == TokenType::TOKEN_WOR) ? NetKind::WOR :
                                 ((current.type == TokenType::TOKEN_TRI) ? NetKind::TRI : NetKind::WIRE));
                    advance();
                    if (current.type == TokenType::TOKEN_SIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "signed")) {
                        is_signed_port = true; advance();
                    } else if (current.type == TokenType::TOKEN_UNSIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned")) {
                        is_signed_port = false; advance();
                    }
                    if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_BIT ||
                        current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_REAL ||
                        current.type == TokenType::TOKEN_SHORTREAL || current.type == TokenType::TOKEN_INTEGER ||
                        (current.type == TokenType::TOKEN_IDENTIFIER &&
                         (current.text == "int" || current.text == "byte" || current.text == "shortint" ||
                          current.text == "longint" || is_type_identifier(current.text)))) {
                        if (current.type == TokenType::TOKEN_INTEGER) is_signed_port = true;
                        if (current.type == TokenType::TOKEN_REAL || (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "real" || current.text == "realtime"))) {
                            is_real_port = true;
                            port_user_type = "real";
                        } else if (current.type == TokenType::TOKEN_SHORTREAL || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "shortreal")) {
                            is_shortreal_port = true;
                            port_user_type = "shortreal";
                        } else if (current.type == TokenType::TOKEN_BIT) {
                            port_user_type = "bit";
                        } else if (current.type == TokenType::TOKEN_IDENTIFIER) {
                            if (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint") {
                                is_int_port = true;
                                int_port_type = current.text;
                                is_signed_port = true;
                            } else {
                                port_user_type = current.text;
                                auto enum_info = get_enum_typedef(current.text);
                                if (enum_info && enum_info->is_signed) is_signed_port = true;
                            }
                        }
                        advance();
                    }
                }
                if (current.type == TokenType::TOKEN_SIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "signed")) {
                    is_signed_port = true;
                    advance();
                } else if (current.type == TokenType::TOKEN_UNSIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned")) {
                    is_signed_port = false;
                    advance();
                }

                std::unique_ptr<ExpressionNode> msb = nullptr;
                std::unique_ptr<ExpressionNode> lsb = nullptr;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    msb = parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in range");
                    lsb = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in range");
                } else if (is_chandle_port || is_semaphore_port || is_mailbox_port || is_string_port || is_real_port) {
                    msb = std::make_unique<NumberNode>(63, 32);
                    lsb = std::make_unique<NumberNode>(0, 32);
                } else if (is_shortreal_port) {
                    msb = std::make_unique<NumberNode>(31, 32);
                    lsb = std::make_unique<NumberNode>(0, 32);
                } else if (is_int_port) {
                    uint32_t iw = 32;
                    if (int_port_type == "byte") iw = 8;
                    else if (int_port_type == "shortint") iw = 16;
                    else if (int_port_type == "longint") iw = 64;
                    msb = std::make_unique<NumberNode>(iw - 1, 32);
                    lsb = std::make_unique<NumberNode>(0, 32);
                }
                while (true) {
                    std::string pname = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier");
                    bool found = false;
                    for (auto& p : mod->ports) {
                        if (p.name == pname) {
                            p.direction = dir;
                            p.is_signed = is_signed_port;
                            p.net_kind = port_kind;
                            if (msb) p.msb_expr = msb->clone();
                            if (lsb) p.lsb_expr = lsb->clone();
                            if (is_chandle_port) {
                                p.is_chandle = true;
                                p.type_name = "chandle";
                            }
                            if (is_semaphore_port) {
                                p.is_semaphore = true;
                                p.type_name = "semaphore";
                            }
                            if (is_mailbox_port) {
                                p.is_mailbox = true;
                                p.type_name = "mailbox";
                            }
                            if (is_string_port) {
                                p.type_name = "string";
                            }
                            if (is_real_port) {
                                p.type_name = "real";
                            }
                            if (is_shortreal_port) {
                                p.type_name = "shortreal";
                            }
                            if (port_user_type == "bit") {
                                p.is_2state = true;
                                p.type_name = "bit";
                            }
                            if (p.type_name.empty() && !port_user_type.empty()) {
                                p.type_name = port_user_type;
                            }
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        PortNode port_decl(pname, dir, msb ? msb->clone() : nullptr, lsb ? lsb->clone() : nullptr, is_signed_port, port_kind);
                        if (is_chandle_port) {
                            port_decl.is_chandle = true;
                            port_decl.type_name = "chandle";
                        }
                        if (is_semaphore_port) {
                            port_decl.is_semaphore = true;
                            port_decl.type_name = "semaphore";
                        }
                        if (is_mailbox_port) {
                            port_decl.is_mailbox = true;
                            port_decl.type_name = "mailbox";
                        }
                        if (is_string_port) {
                            port_decl.type_name = "string";
                        }
                        if (is_real_port) {
                            port_decl.type_name = "real";
                        }
                        if (is_shortreal_port) {
                            port_decl.type_name = "shortreal";
                        }
                        if (port_user_type == "bit") {
                            port_decl.is_2state = true;
                            port_decl.type_name = "bit";
                        }
                        if (port_decl.type_name.empty() && !port_user_type.empty()) {
                            port_decl.type_name = port_user_type;
                        }
                        mod->ports.push_back(std::move(port_decl));
                    }
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after port declaration");
                continue;
            }

            if (current.type == TokenType::TOKEN_EVENT) {
                advance();
                while (true) {
                    std::string ev_name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected event identifier");
                    mod->events.push_back(ev_name);
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after event declaration");
                continue;
            }

            if (current.type == TokenType::TOKEN_TYPEDEF) {
                auto td = parse_typedef();
                if (td && td->enum_type) {
                    for (const auto& m : td->enum_type->members) {
                        ParameterDef pdef;
                        pdef.name = m.first;
                        pdef.default_expr = std::make_unique<NumberNode>(m.second.aval, m.second.bval, td->enum_type->base_width);
                        if (mod && mod->referenced_wildcard_symbols.count(pdef.name)) {
                            throw std::runtime_error("Declaration of '" + pdef.name + "' conflicts with previous wildcard import reference per IEEE 1800-2023 section 26.3");
                        }
                    mod->parameters.push_back(std::move(pdef));
                    }
                }
                if (td && !td->base_type.empty()) {
                    mod->simple_typedefs[td->type_name] = {td->type_name, td->base_type, td->width, td->is_signed};
                }
                if (td) mod->typedefs.push_back(std::move(td));
                continue;
            }


        if (current.type == TokenType::TOKEN_COVERGROUP) {
                auto cg = parse_covergroup();
                if (cg) {
                    std::string cg_name = cg->name;
                    std::shared_ptr<CovergroupNode> scg = std::move(cg);
                    covergroup_map[cg_name] = scg;
                    mod->covergroup_map[cg_name] = scg.get();
                    mod->covergroups.push_back(scg);
                }
                continue;
            }

            if (current.type == TokenType::TOKEN_PROPERTY) {
                auto prop = parse_property();
                if (prop) {
                    std::string pname = prop->name;
                    std::shared_ptr<PropertyNode> sprop = std::move(prop);
                    property_map[pname] = sprop;
                    mod->property_map[pname] = sprop.get();
                    mod->properties.push_back(sprop);
                }
                continue;
            }

            if (current.type == TokenType::TOKEN_SEQUENCE) {
                auto seq = parse_sequence();
                if (seq) {
                    std::string sname = seq->name;
                    std::shared_ptr<SequenceNode> sseq = std::move(seq);
                    sequence_map[sname] = sseq;
                    mod->sequence_map[sname] = sseq.get();
                    mod->sequences.push_back(sseq);
                }
                continue;
            }

            if (current.type == TokenType::TOKEN_VIRTUAL && peek_token().type == TokenType::TOKEN_CLASS) {
                advance(); // consume 'virtual'
                auto cls = parse_class(false, true);
                if (cls) {
                    std::shared_ptr<ClassNode> scls = std::move(cls);
                    class_map[scls->name] = scls;
                    mod->class_map[scls->name] = scls.get();
                    mod->classes.push_back(scls);
                }
                continue;
            }

            if (current.type == TokenType::TOKEN_INTERFACE && peek_token().type == TokenType::TOKEN_CLASS) {
                advance(); // consume 'interface'
                auto cls = parse_class(true);
                if (cls) {
                    std::shared_ptr<ClassNode> scls = std::move(cls);
                    class_map[scls->name] = scls;
                    mod->class_map[scls->name] = scls.get();
                    mod->classes.push_back(scls);
                }
                continue;
            }

            if (current.type == TokenType::TOKEN_CLASS) {
                auto cls = parse_class();
                if (cls) {
                    std::shared_ptr<ClassNode> scls = std::move(cls);
                    class_map[scls->name] = scls;
                    mod->class_map[scls->name] = scls.get();
                    mod->classes.push_back(scls);
                }
                continue;
            }

            if (current.type == TokenType::TOKEN_CONSTRAINT) {
                advance();
                if (match(TokenType::TOKEN_STATIC)) {}
                std::string class_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected class name in out-of-block constraint");
                expect(TokenType::TOKEN_COLON_COLON, "Expected '::' after class name");
                std::string cname = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected constraint name");
                expect(TokenType::TOKEN_LBRACE, "Expected '{' after constraint name");
                int cdepth = 1;
                while (cdepth > 0 && current.type != TokenType::TOKEN_EOF) {
                    if (current.type == TokenType::TOKEN_LBRACE) cdepth++;
                    else if (current.type == TokenType::TOKEN_RBRACE) cdepth--;
                    advance();
                }
                continue;
            }

            if (current.type == TokenType::TOKEN_FATAL || current.type == TokenType::TOKEN_ERROR ||
                current.type == TokenType::TOKEN_WARNING || current.type == TokenType::TOKEN_INFO) {
                throw std::runtime_error(current.filename + ":" + std::to_string(current.line) + ": [CAS-DTINPCIL] Dynamic type in non-procedural context: severity system task '" + current.text + "' is not permitted outside procedural or action-block context per IEEE 1800-2023 section 20.10");
            }

            if (current.type == TokenType::TOKEN_IDENTIFIER && is_covergroup(current.text)) {
                std::string cg_type = current.text;
                advance();
                while (true) {
                    std::string signame = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected covergroup handle identifier");
                    auto msb = std::make_unique<NumberNode>(63, 32);
                    auto lsb = std::make_unique<NumberNode>(0, 32);
                    PortNode sig(signame, "internal", std::move(msb), std::move(lsb), false, NetKind::VAR);
                    sig.is_covergroup_handle = true;
                    sig.covergroup_type_name = cg_type;
                    sig.type_name = cg_type;
                    mod->signals.push_back(std::move(sig));
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after covergroup handle declaration");
                continue;
            }

            if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "const") {
                advance(); // consume optional const modifier
            }

            if (current.type == TokenType::TOKEN_IDENTIFIER && is_class(current.text)) {
                std::string class_type = parse_class_type_and_specialize();
                while (true) {
                    std::string signame = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected class handle identifier");
                    std::vector<AstDimension> unpacked_dims;
                    bool is_arr = false;
                    while (match(TokenType::TOKEN_LBRACKET)) {
                        is_arr = true;
                        auto l = parse_expression();
                        std::unique_ptr<ExpressionNode> r = nullptr;
                        bool is_single = true;
                        if (match(TokenType::TOKEN_COLON)) {
                            r = parse_expression();
                            is_single = false;
                        }
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                        AstDimension ad;
                        ad.left_expr = std::move(l);
                        ad.right_expr = std::move(r);
                        ad.is_single = is_single;
                        unpacked_dims.push_back(std::move(ad));
                    }
                    if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                        parse_expression();
                    }
                    auto msb = std::make_unique<NumberNode>(63, 32);
                    auto lsb = std::make_unique<NumberNode>(0, 32);
                    PortNode sig(signame, "internal", std::move(msb), std::move(lsb), false, NetKind::VAR);
                    sig.is_class_handle = true;
                    sig.class_type_name = class_type;
                    sig.type_name = class_type;
                    sig.is_array = is_arr;
                    sig.unpacked_dimensions = std::move(unpacked_dims);
                    mod->signals.push_back(std::move(sig));
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after class handle declaration");
                continue;
            }

            if (match(TokenType::TOKEN_VIRTUAL)) {
                std::string iface_name = "";
                std::string modport_name = "";
                if (current.type == TokenType::TOKEN_IDENTIFIER || is_interface_type(current.text)) {
                    iface_name = current.text;
                    advance();
                    if (match(TokenType::TOKEN_DOT)) {
                        modport_name = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected modport name after '.'");
                    }
                }
                while (true) {
                    std::string signame = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected virtual interface variable name");
                    auto msb = std::make_unique<NumberNode>(63, 32);
                    auto lsb = std::make_unique<NumberNode>(0, 32);
                    PortNode sig(signame, "internal", std::move(msb), std::move(lsb), false, NetKind::VAR);
                    sig.is_virtual_interface = true;
                    sig.interface_name = iface_name;
                    sig.modport_name = modport_name;
                    sig.type_name = iface_name;
                    mod->signals.push_back(std::move(sig));
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after virtual interface declaration");
                continue;
            }

            if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "const") {
                advance(); // consume optional const modifier
            }

            if (match(TokenType::TOKEN_NETTYPE)) {
                std::string base_t = current.text;
                advance();
                std::string net_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected nettype name");
                if (match(TokenType::TOKEN_WITH)) {
                    std::string res_fn = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected resolution function name");
                } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "with") {
                    advance();
                    std::string res_fn = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected resolution function name");
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after nettype declaration");
                continue;
            }

            if (match(TokenType::TOKEN_SPECPARAM)) {
                while (true) {
                    std::string sname = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected specparam identifier");
                    expect(TokenType::TOKEN_EQUAL, "Expected '=' in specparam");
                    auto val = parse_expression();
                    mod->parameters.push_back(ParameterDef(sname, std::move(val), true));
                    mod->specparam_names.insert(sname);
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after specparam");
                continue;
            }

            bool has_var_kw = (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "var");
            if (has_var_kw) advance();
            if (has_var_kw || current.type == TokenType::TOKEN_TYPE) {
                if (current.type == TokenType::TOKEN_TYPE) {
                    TypeInferenceResult inf = parse_type_spec_from_type_op();
                    auto msb = inf.msb ? inf.msb->clone() : std::make_unique<NumberNode>(inf.width > 0 ? inf.width - 1 : 0, 32);
                    auto lsb = inf.lsb ? inf.lsb->clone() : std::make_unique<NumberNode>(0, 32);

                    while (true) {
                        std::string signame = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier");
                        PortNode sig(signame, "internal", msb ? msb->clone() : nullptr, lsb ? lsb->clone() : nullptr, inf.is_signed, NetKind::VAR);
                        sig.type_name = inf.type_name;
                        sig.is_class_handle = inf.is_class_handle;
                        sig.class_type_name = inf.class_type_name;
                        sig.is_covergroup_handle = inf.is_covergroup_handle;
                        sig.covergroup_type_name = inf.covergroup_type_name;
                        sig.is_virtual_interface = inf.is_virtual_interface;
                        sig.interface_name = inf.interface_name;
                        sig.modport_name = inf.modport_name;

                        if (match(TokenType::TOKEN_EQUAL)) {
                            auto init_expr = parse_expression();
                            if (dynamic_cast<NewArrayNode*>(init_expr.get()) && !sig.is_dynamic_array) {
                                throw std::runtime_error("The new[] operator can only be applied to dynamic arrays per IEEE 1800-2023 section 7.5");
                            }
                            sig.default_value = init_expr->clone();
                            auto init_proc = std::make_unique<ProcessNode>(ProcessType::INITIAL);
                            init_proc->is_decl_init = true;
                            init_proc->body.push_back(std::make_unique<BlockingAssignNode>(signame, std::move(init_expr)));
                            mod->processes.push_back(std::move(init_proc));
                        }

                        mod->signals.push_back(std::move(sig));
                        if (match(TokenType::TOKEN_COMMA)) continue;
                        break;
                    }
                    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after signal declaration");
                    continue;
                }
            }

            if (current.type == TokenType::TOKEN_STRUCT || current.type == TokenType::TOKEN_UNION) {
                auto struct_node = parse_struct_or_union_body();
                std::string anon_type = "__anon_struct_or_union_" + std::to_string(anon_struct_counter++);
                struct_typedef_map[anon_type] = struct_node;
                mod->struct_typedefs[anon_type] = struct_node;
                mod->typedefs.push_back(std::make_unique<TypedefNode>(anon_type, struct_node));
                if (struct_node->is_packed && struct_node->total_width > 0) {
                    VariableDimMetadata svdm;
                    svdm.name = anon_type;
                    svdm.dimensions.push_back(ArrayDimInfo{(int64_t)struct_node->total_width - 1, 0, false});
                    svdm.unpacked_dims = 0;
                    svdm.is_vector_or_array = true;
                    global_type_dimensions[anon_type] = svdm;
                    mod->type_dimensions[anon_type] = svdm;
                }

                while (true) {
                    std::string signame = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier");
                    auto msb = std::make_unique<NumberNode>(struct_node->total_width > 0 ? struct_node->total_width - 1 : 0, 32);
                    auto lsb = std::make_unique<NumberNode>(0, 32);
                    PortNode sig(signame, "internal", std::move(msb), std::move(lsb), struct_node->is_signed, NetKind::VAR);
                    sig.type_name = anon_type;
                    if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                        auto init_expr = parse_expression();
                        sig.default_value = init_expr->clone();
                        if (sig.net_kind == NetKind::VAR) {
                            auto init_proc = std::make_unique<ProcessNode>(ProcessType::INITIAL);
                            init_proc->is_decl_init = true;
                            init_proc->body.push_back(std::make_unique<BlockingAssignNode>(signame, std::move(init_expr)));
                            mod->processes.push_back(std::move(init_proc));
                        } else {
                            auto ca = std::make_unique<ContinuousAssignNode>(signame, std::move(init_expr));
                            mod->continuous_assigns.push_back(std::move(ca));
                        }
                    }
                    mod->signals.push_back(std::move(sig));
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after signal declaration");
                continue;
            }

            if (current.type == TokenType::TOKEN_ENUM) {
                advance(); // consume enum
                uint32_t e_width = 32;
                bool e_signed = true;
                std::string e_base = "int";
                // Optional signedness prefix
                if (current.type == TokenType::TOKEN_SIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "signed")) {
                    e_signed = true; advance();
                } else if (current.type == TokenType::TOKEN_UNSIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned")) {
                    e_signed = false; advance();
                }

                if (current.type == TokenType::TOKEN_INTEGER) {
                    e_width = 32; e_base = "integer"; advance();
                } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "int") {
                    e_width = 32; e_base = "int"; advance();
                } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "byte") {
                    e_width = 8; e_base = "byte"; advance();
                } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "shortint") {
                    e_width = 16; e_base = "shortint"; advance();
                } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "longint") {
                    e_width = 64; e_base = "longint"; advance();
                } else if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_BIT || current.type == TokenType::TOKEN_REG) {
                    e_base = current.text; e_width = 1; e_signed = false; advance();
                }

                // Optional signedness suffix
                if (current.type == TokenType::TOKEN_SIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "signed")) {
                    e_signed = true; advance();
                } else if (current.type == TokenType::TOKEN_UNSIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned")) {
                    e_signed = false; advance();
                }

                if (match(TokenType::TOKEN_LBRACKET)) {
                    auto msb_e = parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in range");
                    auto lsb_e = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in range");
                    try {
                        uint64_t m = eval_const_expr(msb_e.get());
                        uint64_t l = eval_const_expr(lsb_e.get());
                        e_width = (m >= l) ? (m - l + 1) : (l - m + 1);
                    } catch (...) {}
                }
                expect(TokenType::TOKEN_LBRACE, "Expected '{' in enum declaration");
                auto enum_node = std::make_shared<EnumTypeNode>();
                enum_node->base_width = e_width;
                enum_node->is_signed = e_signed;
                enum_node->base_type_name = e_base;
                uint64_t current_val = 0;
                bool prev_had_xz = false;
                std::unordered_set<uint64_t> seen_enum_vals;
                while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                    std::string mname = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected enum member identifier");
                    bool is_seq = false;
                    int64_t seq_start = 0, seq_end = 0;
                    if (match(TokenType::TOKEN_LBRACKET)) {
                        is_seq = true;
                        auto l = parse_expression();
                        if (match(TokenType::TOKEN_COLON)) {
                            auto r = parse_expression();
                            if (expr_has_xz(l.get()) || expr_has_xz(r.get())) {
                                throw std::runtime_error("Undefined sequence count in enum is an error per IEEE 1800-2023 section 6.19");
                            }
                            try {
                                seq_start = eval_const_expr(l.get());
                                seq_end = eval_const_expr(r.get());
                            } catch (...) {}
                        } else {
                            if (expr_has_xz(l.get())) {
                                throw std::runtime_error("Undefined sequence count in enum is an error per IEEE 1800-2023 section 6.19");
                            }
                            int64_t cnt = 0;
                            try {
                                cnt = eval_const_expr(l.get());
                            } catch (...) {}
                            if (cnt <= 0) {
                                throw std::runtime_error("A zero or negative sequence count in enum is an error per IEEE 1800-2023 section 6.19");
                            }
                            seq_start = 0; seq_end = cnt - 1;
                        }
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in enum sequence");
                    }
                    bool has_eq = false;
                    bool curr_had_xz = false;
                    uint64_t current_bval = 0;
                    if (match(TokenType::TOKEN_EQUAL)) {
                        has_eq = true;
                        auto val_expr = parse_expression();
                        if (auto num = dynamic_cast<NumberNode*>(val_expr.get())) {
                            current_val = num->aval;
                            current_bval = num->bval;
                            if (num->explicit_size && num->width != e_width) {
                                throw std::runtime_error("If integer value expression is a sized literal constant, it shall be an error if the size is different from the enum base type per IEEE 1800-2023 6.19");
                            }
                            if (!num->explicit_size && num->width > e_width && e_width < 64) {
                                uint64_t msb_a = (num->aval >> (e_width - 1)) & 1ULL;
                                uint64_t msb_b = (num->bval >> (e_width - 1)) & 1ULL;
                                for (uint32_t b = e_width; b < num->width && b < 64; ++b) {
                                    uint64_t cur_a = (num->aval >> b) & 1ULL;
                                    uint64_t cur_b = (num->bval >> b) & 1ULL;
                                    if (cur_a != msb_a || cur_b != msb_b) {
                                        throw std::runtime_error("If integer value expression is an unsized literal constant, it shall be an error if trimmed bits do not match MSB per IEEE 1800-2023 6.19");
                                    }
                                }
                            }
                            if (num->bval != 0) {
                                curr_had_xz = true;
                                if (e_base == "bit" || e_base == "byte" || e_base == "int" || e_base == "shortint" || e_base == "longint") {
                                    throw std::runtime_error("An enumerated name with x or z assignments assigned to an enum with 2-state declaration shall be a syntax error per IEEE 1800-2023 6.19");
                                }
                            }
                        } else {
                            if (expr_has_xz(val_expr.get())) {
                                curr_had_xz = true;
                                if (e_base == "bit" || e_base == "byte" || e_base == "int" || e_base == "shortint" || e_base == "longint") {
                                    throw std::runtime_error("An enumerated name with x or z assignments assigned to an enum with 2-state declaration shall be a syntax error per IEEE 1800-2023 6.19");
                                }
                            }
                            try {
                                current_val = eval_const_expr(val_expr.get());
                                current_bval = 0;
                            } catch (...) {
                                throw std::runtime_error("Enum value expression must be an elaboration-time constant expression per IEEE 1800-2023 section 6.19");
                            }
                        }
                    } else {
                        current_bval = 0;
                        if (prev_had_xz) {
                            throw std::runtime_error("An unassigned enumerated name that follows an enum name with x or z assignments shall be a syntax error per IEEE 1800-2023 6.19");
                        }
                    }
                    if (!e_signed && e_width < 64) {
                        uint64_t max_val = (1ULL << e_width) - 1;
                        if (current_val > max_val) {
                            throw std::runtime_error("Enum value exceeds range of base type per IEEE 1800-2023 section 6.19");
                        }
                    }
                    if (!curr_had_xz) {
                        if (seen_enum_vals.count(current_val)) {
                            throw std::runtime_error("Duplicate enum value in enum declaration per IEEE 1800-2023 section 6.19");
                        }
                        seen_enum_vals.insert(current_val);
                    }
                    prev_had_xz = curr_had_xz;
                    if (is_seq) {
                        int64_t step = (seq_start <= seq_end) ? 1 : -1;
                        for (int64_t idx = seq_start; ; idx += step) {
                            std::string s_name = mname + std::to_string(idx);
                            LogicVal mval(e_width, current_val, current_bval);
                            enum_node->members.push_back({s_name, mval});
                            ParameterDef pdef;
                            pdef.name = s_name;
                            pdef.is_signed = e_signed;
                            pdef.default_expr = std::make_unique<NumberNode>(mval.aval, mval.bval, e_width, e_signed);
                            if (mod && mod->referenced_wildcard_symbols.count(pdef.name)) {
                                throw std::runtime_error("Declaration of '" + pdef.name + "' conflicts with previous wildcard import reference per IEEE 1800-2023 section 26.3");
                            }
                            mod->parameters.push_back(std::move(pdef));
                            current_val++;
                            current_bval = 0;
                            if (idx == seq_end) break;
                        }
                    } else {
                        LogicVal mval(e_width, current_val, current_bval);
                        enum_node->members.push_back({mname, mval});
                        ParameterDef pdef;
                        pdef.name = mname;
                        pdef.is_signed = e_signed;
                        pdef.default_expr = std::make_unique<NumberNode>(mval.aval, mval.bval, e_width, e_signed);
                        if (mod && mod->referenced_wildcard_symbols.count(pdef.name)) {
                            throw std::runtime_error("Declaration of '" + pdef.name + "' conflicts with previous wildcard import reference per IEEE 1800-2023 section 26.3");
                        }
                    mod->parameters.push_back(std::move(pdef));
                        current_val++;
                        current_bval = 0;
                    }
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_RBRACE, "Expected '}' closing enum member list");

                std::string anon_enum_name = "__anon_enum_" + std::to_string(anon_struct_counter++);
                typedef_map[anon_enum_name] = enum_node;
                mod->enum_typedefs[anon_enum_name] = enum_node;
                mod->typedefs.push_back(std::make_unique<TypedefNode>(anon_enum_name, enum_node));

                while (true) {
                    std::string signame = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier");
                    auto msb = std::make_unique<NumberNode>(e_width > 0 ? e_width - 1 : 0, 32);
                    auto lsb = std::make_unique<NumberNode>(0, 32);
                    PortNode sig(signame, "internal", std::move(msb), std::move(lsb), e_signed, NetKind::VAR);
                    sig.type_name = anon_enum_name;
                    sig.is_2state = (e_base == "bit" || e_base == "int" || e_base == "byte" || e_base == "shortint" || e_base == "longint");
                    if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                        auto init_expr = parse_expression();
                        sig.default_value = init_expr->clone();
                        if (sig.net_kind == NetKind::VAR) {
                            auto init_proc = std::make_unique<ProcessNode>(ProcessType::INITIAL);
                            init_proc->is_decl_init = true;
                            init_proc->body.push_back(std::make_unique<BlockingAssignNode>(signame, std::move(init_expr)));
                            mod->processes.push_back(std::move(init_proc));
                        } else {
                            auto ca = std::make_unique<ContinuousAssignNode>(signame, std::move(init_expr));
                            mod->continuous_assigns.push_back(std::move(ca));
                        }
                    }
                    mod->signals.push_back(std::move(sig));
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after enum signal declaration");
                continue;
            }

            std::string mod_sig_type = "";
            if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON_COLON) {
                std::string p_name = current.text;
                advance(); // consume package identifier
                advance(); // consume '::'
                std::string t_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected type identifier after '::'");
                mod_sig_type = p_name + "::" + t_name;
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && (is_typedef(current.text) || is_simple_typedef(current.text) || is_unpacked_array_typedef(current.text) || find_type_dim_metadata(current.text) != nullptr)) {
                mod_sig_type = current.text;
                advance();
            }

            if (!mod_sig_type.empty()) {
                std::string type_name = resolve_imported_type_name(mod_sig_type);
                auto enum_info = get_enum_typedef(type_name);
                auto struct_info = get_struct_typedef(type_name);
                const UnpackedArrayTypedef* uarr_info = get_unpacked_array_typedef(type_name);
                const VariableDimMetadata* dim_meta = find_type_dim_metadata(type_name);
                SimpleTypedef smp_info;
                bool has_smp = get_simple_typedef(type_name, smp_info);

                uint32_t width = uarr_info ? uarr_info->elem_width : (struct_info ? struct_info->total_width : (enum_info ? enum_info->base_width : (has_smp ? smp_info.width : 32)));
                if (dim_meta) {
                    uint32_t pw = 1;
                    for (const auto& d : dim_meta->dimensions) {
                        if (!d.is_unpacked) pw *= (uint32_t)d.size();
                    }
                    if (pw > 1) width = pw;
                }
                bool is_signed_sig = uarr_info ? uarr_info->elem_signed : (struct_info ? struct_info->is_signed : (enum_info ? enum_info->is_signed : (has_smp ? smp_info.is_signed : false)));

                std::vector<AstDimension> packed_dims;
                while (match(TokenType::TOKEN_LBRACKET)) {
                    if (match(TokenType::TOKEN_DOLLAR)) {
                        throw std::runtime_error("A packed dimension cannot be a queue '$' per IEEE 1800-2023 §7.4");
                    }
                    auto l = parse_expression();
                    std::unique_ptr<ExpressionNode> r = nullptr;
                    if (match(TokenType::TOKEN_COLON)) {
                        r = parse_expression();
                    } else {
                        if (auto num = dynamic_cast<NumberNode*>(l.get())) {
                            if (!num->is_real && !num->is_unknown() && num->aval == 0) {
                                throw std::runtime_error("[CAS-DIM] Packed dimension size must be greater than 0 per IEEE 1800-2023 §7.4.2");
                            }
                        }
                        r = std::make_unique<NumberNode>(0, 32);
                        l = std::make_unique<BinaryOpNode>("-", std::move(l), std::make_unique<NumberNode>(1, 32));
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in range");
                    AstDimension ad;
                    ad.left_expr = l->clone();
                    ad.right_expr = r->clone();
                    ad.is_single = false;
                    packed_dims.push_back(std::move(ad));
                }

                auto msb = std::make_unique<NumberNode>(width > 0 ? width - 1 : 0, 32);
                auto lsb = std::make_unique<NumberNode>(0, 32);

                while (true) {
                    std::string signame = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier");
                    PortNode sig(signame, "internal", msb ? msb->clone() : nullptr, lsb ? lsb->clone() : nullptr, is_signed_sig, NetKind::WIRE);
                    sig.type_name = type_name;
                    sig.packed_dimensions = packed_dims;
                    if (uarr_info) {
                        if (uarr_info->is_dynamic_array) {
                            sig.is_dynamic_array = true;
                        } else {
                            sig.is_array = true;
                            AstDimension ad;
                            ad.left_expr = std::make_unique<NumberNode>(uarr_info->array_size, 32);
                            ad.right_expr = nullptr;
                            ad.is_single = true;
                            sig.unpacked_dimensions.push_back(std::move(ad));
                            sig.bound1_expr = std::make_unique<NumberNode>(uarr_info->start_idx, 32);
                            sig.bound2_expr = std::make_unique<NumberNode>(uarr_info->end_idx, 32);
                        }
                    } else if (dim_meta && dim_meta->unpacked_dims > 0) {
                        sig.is_array = true;
                        for (const auto& d : dim_meta->dimensions) {
                            if (d.is_unpacked) {
                                AstDimension ad;
                                ad.left_expr = std::make_unique<NumberNode>(d.size(), 32);
                                ad.right_expr = nullptr;
                                ad.is_single = true;
                                sig.unpacked_dimensions.push_back(std::move(ad));
                            }
                        }
                    }

                    while (match(TokenType::TOKEN_LBRACKET)) {
                        if (match(TokenType::TOKEN_DOLLAR)) {
                            sig.is_queue = true;
                            if (match(TokenType::TOKEN_COLON)) {
                                sig.queue_bound_expr = parse_expression();
                                if (auto un = dynamic_cast<UnaryOpNode*>(sig.queue_bound_expr.get())) {
                                    if (un->op == "-") throw std::runtime_error("[CAS-QUEUE] Queue maximum bound must be a non-negative integral constant expression per IEEE 1800-2023 §7.10.1");
                                }
                                if (auto num = dynamic_cast<NumberNode*>(sig.queue_bound_expr.get())) {
                                    if (num->is_unknown() || num->is_real) throw std::runtime_error("[CAS-QUEUE] Queue maximum bound must be a non-negative integral constant expression per IEEE 1800-2023 §7.10.1");
                                }
                                if (dynamic_cast<IdentifierNode*>(sig.queue_bound_expr.get())) {
                                    throw std::runtime_error("[CAS-QUEUE] Queue maximum bound must be a constant expression per IEEE 1800-2023 §7.10.1");
                                }
                            }
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                            break;
                        } else if (match(TokenType::TOKEN_RBRACKET)) {
                            sig.is_dynamic_array = true;
                            break;
                        } else if (is_assoc_key_token(current)) {
                            sig.is_associative_array = true;
                            parse_assoc_key_type(sig.assoc_key_type, sig.assoc_key_width);
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in associative array dimension");
                            break;
                        } else {
                            sig.is_array = true;
                            auto l = parse_expression();
                            std::unique_ptr<ExpressionNode> r = nullptr;
                            bool is_single = true;
                            if (match(TokenType::TOKEN_COLON)) {
                                r = parse_expression();
                                is_single = false;
                            }
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                            if (is_single) {
                                if (auto un = dynamic_cast<UnaryOpNode*>(l.get())) {
                                    if (un->op == "-") throw std::runtime_error("[CAS-DIM] Array dimension size must be greater than 0 per IEEE 1800-2023 §7.4.2");
                                }
                                if (auto num = dynamic_cast<NumberNode*>(l.get())) {
                                    if (!num->is_real && !num->is_unknown() && num->aval == 0) throw std::runtime_error("[CAS-DIM] Array dimension size must be greater than 0 per IEEE 1800-2023 §7.4.2");
                                }
                            }
                            AstDimension ad;
                            ad.left_expr = std::move(l);
                            ad.right_expr = std::move(r);
                            ad.is_single = is_single;
                            sig.unpacked_dimensions.push_back(std::move(ad));
                        }
                    }
                    if (!sig.unpacked_dimensions.empty()) {
                        sig.bound1_expr = sig.unpacked_dimensions[0].left_expr->clone();
                        if (sig.unpacked_dimensions[0].right_expr) {
                            sig.bound2_expr = sig.unpacked_dimensions[0].right_expr->clone();
                        }
                    } else if (match(TokenType::TOKEN_REP_CONSEC)) {
                        sig.is_associative_array = true;
                        sig.assoc_key_type = "*";
                        sig.assoc_key_width = 64;
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in associative array dimension");
                    }

                    if (match(TokenType::TOKEN_EQUAL)) {
                        auto init_expr = parse_expression();
                        if (dynamic_cast<NewArrayNode*>(init_expr.get()) && !sig.is_dynamic_array) {
                            throw std::runtime_error("The new[] operator can only be applied to dynamic arrays per IEEE 1800-2023 section 7.5");
                        }
                        sig.default_value = init_expr->clone();
                        if (sig.net_kind == NetKind::VAR) {
                            auto init_proc = std::make_unique<ProcessNode>(ProcessType::INITIAL);
                            init_proc->is_decl_init = true;
                            init_proc->body.push_back(std::make_unique<BlockingAssignNode>(signame, std::move(init_expr)));
                            mod->processes.push_back(std::move(init_proc));
                        } else {
                            auto ca = std::make_unique<ContinuousAssignNode>(signame, std::move(init_expr));
                            mod->continuous_assigns.push_back(std::move(ca));
                        }
                    }

                    bool is_port = false;
                    for (auto& p : mod->ports) {
                        if (p.name == signame) {
                            p.is_signed = is_signed_sig;
                            p.net_kind = NetKind::VAR;
                            p.type_name = type_name;
                            if (msb) p.msb_expr = msb->clone();
                            if (lsb) p.lsb_expr = lsb->clone();
                            is_port = true;
                            break;
                        }
                    }
                    if (!is_port) {
                        for (const auto& existing : mod->signals) {
                            if (existing.name == signame) {
                                throw std::runtime_error("Identifier '" + signame + "' is already declared in module '" + mod->name + "' per IEEE 1800-2023 6.5");
                            }
                        }
                        mod->signals.push_back(std::move(sig));
                    }
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after signal declaration");
                continue;
            }

            bool is_ident_int = (current.type == TokenType::TOKEN_IDENTIFIER && 
                                 (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint" || current.text == "time" || current.text == "realtime"));
            bool is_ident_unsigned = (current.type == TokenType::TOKEN_UNSIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned"));
            bool is_chandle_mod = (current.type == TokenType::TOKEN_CHANDLE || 
                                   (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "chandle"));
            bool is_semaphore_mod = (current.type == TokenType::TOKEN_SEMAPHORE || 
                                     (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "semaphore"));
            bool is_mailbox_mod = (current.type == TokenType::TOKEN_MAILBOX || 
                                   (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "mailbox"));
            bool is_ident_string_sig = (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "string");
            bool is_shortreal_sig = (current.type == TokenType::TOKEN_SHORTREAL || 
                                     (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "shortreal"));
            bool is_real_sig = (current.type == TokenType::TOKEN_REAL || is_shortreal_sig || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "realtime"));
            bool is_time_sig = (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "time");
            bool is_extra_net = (current.type == TokenType::TOKEN_INTERCONNECT ||
                                 (current.type == TokenType::TOKEN_IDENTIFIER && 
                                  (current.text == "interconnect" || current.text == "uwire" || 
                                   current.text == "tri0" || current.text == "tri1" || 
                                   current.text == "triand" || current.text == "trior" || 
                                   current.text == "supply0" || current.text == "supply1")));

            if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE || 
                current.type == TokenType::TOKEN_WAND  || current.type == TokenType::TOKEN_WOR  || 
                current.type == TokenType::TOKEN_TRI   || is_extra_net ||
                current.type == TokenType::TOKEN_REG   || current.type == TokenType::TOKEN_INTEGER || 
                current.type == TokenType::TOKEN_BIT   || current.type == TokenType::TOKEN_REAL ||
                current.type == TokenType::TOKEN_SHORTREAL || is_shortreal_sig || is_real_sig || is_time_sig ||
                current.type == TokenType::TOKEN_SIGNED || is_ident_int || is_ident_string_sig || is_ident_unsigned || is_chandle_mod ||
                is_semaphore_mod || is_mailbox_mod) {
                
                bool explicit_signed = false;
                bool explicit_unsigned = false;
                if (current.type == TokenType::TOKEN_SIGNED) {
                    explicit_signed = true;
                    advance();
                } else if (current.type == TokenType::TOKEN_UNSIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned")) {
                    explicit_unsigned = true;
                    advance();
                }
                std::string int_type_name = "";
                bool is_int = (current.type == TokenType::TOKEN_INTEGER);
                if (current.type == TokenType::TOKEN_IDENTIFIER && 
                    (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint")) {
                    int_type_name = current.text;
                    is_int = true;
                }

                bool is_bit_type = (current.type == TokenType::TOKEN_BIT);
                bool is_explicit_net = (current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_WAND ||
                                        current.type == TokenType::TOKEN_WOR || current.type == TokenType::TOKEN_TRI || is_extra_net);
                NetKind sig_kind = NetKind::WIRE;
                if (is_chandle_mod || is_semaphore_mod || is_mailbox_mod || is_ident_string_sig) {
                    sig_kind = NetKind::VAR;
                } else if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_BIT || is_int || is_real_sig || is_time_sig) {
                    sig_kind = NetKind::VAR;
                } else if (current.type == TokenType::TOKEN_WAND) {
                    sig_kind = NetKind::WAND;
                } else if (current.type == TokenType::TOKEN_WOR) {
                    sig_kind = NetKind::WOR;
                } else if (current.type == TokenType::TOKEN_TRI) {
                    sig_kind = NetKind::TRI;
                } else if (current.type == TokenType::TOKEN_WIRE || is_extra_net) {
                    sig_kind = NetKind::WIRE;
                }
                advance();
                if (current.type == TokenType::TOKEN_STRUCT || current.type == TokenType::TOKEN_UNION) {
                    auto struct_node = parse_struct_or_union_body();
                    std::string anon_type = "__anon_struct_or_union_" + std::to_string(anon_struct_counter++);
                    struct_typedef_map[anon_type] = struct_node;
                    mod->struct_typedefs[anon_type] = struct_node;
                    mod->typedefs.push_back(std::make_unique<TypedefNode>(anon_type, struct_node));
                    if (struct_node->is_packed && struct_node->total_width > 0) {
                        VariableDimMetadata svdm;
                        svdm.name = anon_type;
                        svdm.dimensions.push_back(ArrayDimInfo{(int64_t)struct_node->total_width - 1, 0, false});
                        svdm.unpacked_dims = 0;
                        svdm.is_vector_or_array = true;
                        global_type_dimensions[anon_type] = svdm;
                        mod->type_dimensions[anon_type] = svdm;
                    }

                    while (true) {
                        std::string signame = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier");
                        auto msb = std::make_unique<NumberNode>(struct_node->total_width > 0 ? struct_node->total_width - 1 : 0, 32);
                        auto lsb = std::make_unique<NumberNode>(0, 32);
                        PortNode sig(signame, "internal", std::move(msb), std::move(lsb), struct_node->is_signed, sig_kind);
                        sig.type_name = anon_type;
                        if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                            auto init_expr = parse_expression();
                            sig.default_value = init_expr->clone();
                            auto init_proc = std::make_unique<ProcessNode>(ProcessType::INITIAL);
                            init_proc->is_decl_init = true;
                            init_proc->body.push_back(std::make_unique<BlockingAssignNode>(signame, std::move(init_expr)));
                            mod->processes.push_back(std::move(init_proc));
                        }
                        mod->signals.push_back(std::move(sig));
                        if (match(TokenType::TOKEN_COMMA)) continue;
                        break;
                    }
                    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after signal declaration");
                    continue;
                }
                Token peek_t = peek_token();
                bool is_followed_by_name = (peek_t.type == TokenType::TOKEN_IDENTIFIER ||
                                            peek_t.type == TokenType::TOKEN_SIGNED ||
                                            peek_t.type == TokenType::TOKEN_UNSIGNED ||
                                            peek_t.type == TokenType::TOKEN_LBRACKET);
                std::string udt_type_name = "";
                if (is_followed_by_name &&
                    (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_BIT ||
                     current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_INTEGER ||
                     current.type == TokenType::TOKEN_REAL || current.type == TokenType::TOKEN_SHORTREAL ||
                     (current.type == TokenType::TOKEN_IDENTIFIER &&
                      (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint" ||
                       is_typedef(current.text) || is_simple_typedef(current.text) || is_unpacked_array_typedef(current.text) ||
                       find_type_dim_metadata(current.text) != nullptr || is_type_identifier(current.text))))) {
                    if (current.type == TokenType::TOKEN_REAL) is_real_sig = true;
                    if (current.type == TokenType::TOKEN_SHORTREAL) is_shortreal_sig = true;
                    if (current.type == TokenType::TOKEN_BIT) is_bit_type = true;
                    if (current.type == TokenType::TOKEN_INTEGER || current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint") {
                        is_int = true;
                        int_type_name = current.text;
                    } else if (current.type == TokenType::TOKEN_IDENTIFIER &&
                               (is_typedef(current.text) || is_simple_typedef(current.text) || is_unpacked_array_typedef(current.text) ||
                                find_type_dim_metadata(current.text) != nullptr || is_type_identifier(current.text))) {
                        udt_type_name = current.text;
                    }
                    advance();
                }
                bool is_vectored_decl = false;
                if (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "scalared" || current.text == "vectored")) {
                    if (current.text == "vectored") is_vectored_decl = true;
                    advance();
                }
                if (match(TokenType::TOKEN_HASH)) {
                    if (match(TokenType::TOKEN_LPAREN)) {
                        while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) advance();
                        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing net delay");
                    } else {
                        parse_expression();
                    }
                }
                if (is_mailbox_mod && match(TokenType::TOKEN_HASH)) {
                    expect(TokenType::TOKEN_LPAREN, "Expected '(' in mailbox parameter");
                    while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) advance();
                    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing mailbox parameter");
                }

                if (current.type == TokenType::TOKEN_SIGNED) {
                    explicit_signed = true;
                    advance();
                } else if (current.type == TokenType::TOKEN_UNSIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned")) {
                    explicit_unsigned = true;
                    advance();
                }

                auto enum_info = !udt_type_name.empty() ? get_enum_typedef(udt_type_name) : nullptr;
                auto struct_info = !udt_type_name.empty() ? get_struct_typedef(udt_type_name) : nullptr;
                const UnpackedArrayTypedef* uarr_info = !udt_type_name.empty() ? get_unpacked_array_typedef(udt_type_name) : nullptr;
                const VariableDimMetadata* dim_meta = !udt_type_name.empty() ? find_type_dim_metadata(udt_type_name) : nullptr;
                SimpleTypedef smp_info;
                bool has_smp = !udt_type_name.empty() ? get_simple_typedef(udt_type_name, smp_info) : false;

                uint32_t udt_width = uarr_info ? uarr_info->elem_width : (struct_info ? struct_info->total_width : (enum_info ? enum_info->base_width : (has_smp ? smp_info.width : 32)));
                if (dim_meta) {
                    uint32_t pw = 1;
                    for (const auto& d : dim_meta->dimensions) {
                        if (!d.is_unpacked) pw *= (uint32_t)d.size();
                    }
                    if (pw > 1) udt_width = pw;
                }

                bool is_signed_sig = false;
                if (explicit_signed) is_signed_sig = true;
                else if (explicit_unsigned) is_signed_sig = false;
                else if (is_int) is_signed_sig = true;
                else if (!udt_type_name.empty()) {
                    is_signed_sig = uarr_info ? uarr_info->elem_signed : (struct_info ? struct_info->is_signed : (enum_info ? enum_info->is_signed : (has_smp ? smp_info.is_signed : false)));
                }

                std::vector<AstDimension> packed_dims;
                std::unique_ptr<ExpressionNode> msb = nullptr;
                std::unique_ptr<ExpressionNode> lsb = nullptr;
                while (match(TokenType::TOKEN_LBRACKET)) {
                    if (match(TokenType::TOKEN_DOLLAR)) {
                        throw std::runtime_error("A packed dimension cannot be a queue '$' per IEEE 1800-2023 §7.4");
                    }
                    auto l = parse_expression();
                    std::unique_ptr<ExpressionNode> r = nullptr;
                    if (match(TokenType::TOKEN_COLON)) {
                        r = parse_expression();
                    } else {
                        if (auto num = dynamic_cast<NumberNode*>(l.get())) {
                            if (!num->is_real && !num->is_unknown() && num->aval == 0) {
                                throw std::runtime_error("[CAS-DIM] Packed dimension size must be greater than 0 per IEEE 1800-2023 §7.4.2");
                            }
                        }
                        r = std::make_unique<NumberNode>(0, 32);
                        l = std::make_unique<BinaryOpNode>("-", std::move(l), std::make_unique<NumberNode>(1, 32));
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in range");
                    AstDimension ad;
                    ad.left_expr = l->clone();
                    ad.right_expr = r->clone();
                    ad.is_single = false;
                    packed_dims.push_back(std::move(ad));
                    if (!msb) {
                        msb = std::move(l);
                        lsb = std::move(r);
                    }
                }
                if (packed_dims.empty()) {
                    if (!udt_type_name.empty()) {
                        msb = std::make_unique<NumberNode>(udt_width > 0 ? udt_width - 1 : 0, 32);
                        lsb = std::make_unique<NumberNode>(0, 32);
                    } else if (is_chandle_mod || is_semaphore_mod || is_mailbox_mod || is_ident_string_sig) {
                        msb = std::make_unique<NumberNode>(63, 32);
                        lsb = std::make_unique<NumberNode>(0, 32);
                    } else if (is_int) {
                        uint32_t iw = 32;
                        if (int_type_name == "byte") iw = 8;
                        else if (int_type_name == "shortint") iw = 16;
                        else if (int_type_name == "longint") iw = 64;
                        msb = std::make_unique<NumberNode>(iw - 1, 32);
                        lsb = std::make_unique<NumberNode>(0, 32);
                    } else if (is_real_sig || is_time_sig) {
                        uint32_t rw = is_shortreal_sig ? 32 : 64;
                        msb = std::make_unique<NumberNode>(rw - 1, 32);
                        lsb = std::make_unique<NumberNode>(0, 32);
                    }
                }
                while (true) {
                    std::string signame = current.text;
                    if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_IDENTIFIER) {
                        advance();
                    } else {
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier");
                    }
                    if (!is_explicit_net && !udt_type_name.empty()) {
                        if (is_class(udt_type_name) || is_chandle_mod || is_semaphore_mod || is_mailbox_mod || is_ident_string_sig) {
                            sig_kind = NetKind::VAR;
                        }
                    }
                    PortNode sig(signame, "internal", msb ? msb->clone() : nullptr, lsb ? lsb->clone() : nullptr, is_signed_sig, sig_kind);
                    sig.packed_dimensions = packed_dims;
                    if (is_chandle_mod) {
                        sig.is_chandle = true;
                        sig.type_name = "chandle";
                    }
                    if (is_semaphore_mod) {
                        sig.is_semaphore = true;
                        sig.type_name = "semaphore";
                    }
                    if (is_mailbox_mod) {
                        sig.is_mailbox = true;
                        sig.type_name = "mailbox";
                    }
                    if (is_ident_string_sig) {
                        sig.type_name = "string";
                    }
                    if (is_bit_type) sig.type_name = "bit";
                    if (is_shortreal_sig) sig.type_name = "shortreal";
                    else if (is_real_sig) sig.type_name = "real";
                    if (!int_type_name.empty()) sig.type_name = int_type_name;
                    if (!udt_type_name.empty()) sig.type_name = udt_type_name;

                    while (match(TokenType::TOKEN_LBRACKET)) {
                        if (match(TokenType::TOKEN_DOLLAR)) {
                            sig.is_queue = true;
                            if (match(TokenType::TOKEN_COLON)) {
                                sig.queue_bound_expr = parse_expression();
                                if (auto un = dynamic_cast<UnaryOpNode*>(sig.queue_bound_expr.get())) {
                                    if (un->op == "-") throw std::runtime_error("[CAS-QUEUE] Queue maximum bound must be a non-negative integral constant expression per IEEE 1800-2023 §7.10.1");
                                }
                                if (auto num = dynamic_cast<NumberNode*>(sig.queue_bound_expr.get())) {
                                    if (num->is_unknown() || num->is_real) throw std::runtime_error("[CAS-QUEUE] Queue maximum bound must be a non-negative integral constant expression per IEEE 1800-2023 §7.10.1");
                                }
                                if (dynamic_cast<IdentifierNode*>(sig.queue_bound_expr.get())) {
                                    throw std::runtime_error("[CAS-QUEUE] Queue maximum bound must be a constant expression per IEEE 1800-2023 §7.10.1");
                                }
                            }
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                            break;
                        } else if (match(TokenType::TOKEN_RBRACKET)) {
                            sig.is_dynamic_array = true;
                            break;
                        } else if (is_assoc_key_token(current)) {
                            sig.is_associative_array = true;
                            parse_assoc_key_type(sig.assoc_key_type, sig.assoc_key_width);
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in associative array dimension");
                            break;
                        } else {
                            sig.is_array = true;
                            auto l = parse_expression();
                            std::unique_ptr<ExpressionNode> r = nullptr;
                            bool is_single = true;
                            if (match(TokenType::TOKEN_COLON)) {
                                r = parse_expression();
                                is_single = false;
                            }
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                            if (is_single) {
                                if (auto un = dynamic_cast<UnaryOpNode*>(l.get())) {
                                    if (un->op == "-") throw std::runtime_error("[CAS-DIM] Array dimension size must be greater than 0 per IEEE 1800-2023 §7.4.2");
                                }
                                if (auto num = dynamic_cast<NumberNode*>(l.get())) {
                                    if (!num->is_real && !num->is_unknown() && num->aval == 0) throw std::runtime_error("[CAS-DIM] Array dimension size must be greater than 0 per IEEE 1800-2023 §7.4.2");
                                }
                            }
                            AstDimension ad;
                            ad.left_expr = std::move(l);
                            ad.right_expr = std::move(r);
                            ad.is_single = is_single;
                            sig.unpacked_dimensions.push_back(std::move(ad));
                        }
                    }
                    if (!sig.unpacked_dimensions.empty()) {
                        sig.bound1_expr = sig.unpacked_dimensions[0].left_expr->clone();
                        if (sig.unpacked_dimensions[0].right_expr) {
                            sig.bound2_expr = sig.unpacked_dimensions[0].right_expr->clone();
                        }
                    } else if (match(TokenType::TOKEN_REP_CONSEC)) {
                        sig.is_associative_array = true;
                        sig.assoc_key_type = "*";
                        sig.assoc_key_width = 64;
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in associative array dimension");
                    }

                    if (match(TokenType::TOKEN_EQUAL)) {
                        auto init_expr = parse_expression();
                        if (dynamic_cast<NewArrayNode*>(init_expr.get()) && !sig.is_dynamic_array) {
                            throw std::runtime_error("The new[] operator can only be applied to dynamic arrays per IEEE 1800-2023 section 7.5");
                        }
                        sig.default_value = init_expr->clone();
                        if (sig.net_kind == NetKind::VAR) {
                            auto init_proc = std::make_unique<ProcessNode>(ProcessType::INITIAL);
                            init_proc->is_decl_init = true;
                            init_proc->body.push_back(std::make_unique<BlockingAssignNode>(signame, std::move(init_expr)));
                            mod->processes.push_back(std::move(init_proc));
                        } else {
                            auto ca = std::make_unique<ContinuousAssignNode>(signame, std::move(init_expr));
                            mod->continuous_assigns.push_back(std::move(ca));
                        }
                    }

                    bool is_port = false;
                    for (auto& p : mod->ports) {
                        if (p.name == signame) {
                            if (explicit_signed) p.is_signed = true;
                            else if (explicit_unsigned && !p.is_signed) p.is_signed = false;
                            else if (is_int) p.is_signed = true;
                            p.net_kind = sig_kind;
                            if (msb) p.msb_expr = msb->clone();
                            if (lsb) p.lsb_expr = lsb->clone();
                            if (is_chandle_mod) {
                                p.is_chandle = true;
                                p.type_name = "chandle";
                            }
                            if (is_semaphore_mod) {
                                p.is_semaphore = true;
                                p.type_name = "semaphore";
                            }
                            if (is_mailbox_mod) {
                                p.is_mailbox = true;
                                p.type_name = "mailbox";
                            }
                            if (is_shortreal_sig) p.type_name = "shortreal";
                            else if (is_real_sig) p.type_name = "real";
                            if (is_bit_type) { p.is_2state = true; p.type_name = "bit"; }
                            if (!int_type_name.empty()) p.type_name = int_type_name;
                            if (p.type_name.empty() && !udt_type_name.empty()) p.type_name = udt_type_name;
                            is_port = true;
                            break;
                        }
                    }
                    if (!is_port) {
                        for (const auto& existing : mod->signals) {
                            if (existing.name == signame) {
                                throw std::runtime_error("Identifier '" + signame + "' is already declared in module '" + mod->name + "' per IEEE 1800-2023 6.5");
                            }
                        }
                        sig.is_vectored = is_vectored_decl;
                        if (is_vectored_decl) mod->vectored_signals.insert(signame);
                        mod->signals.push_back(std::move(sig));
                    }
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after signal declaration");
                continue;
            }

            if (match(TokenType::TOKEN_ASSIGN)) {
                if (match(TokenType::TOKEN_LPAREN)) {
                    while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                        advance();
                    }
                    expect(TokenType::TOKEN_RPAREN, "Expected ')' after drive strength in assign");
                }
                if (match(TokenType::TOKEN_HASH)) {
                    if (match(TokenType::TOKEN_LPAREN)) {
                        parse_expression();
                        expect(TokenType::TOKEN_RPAREN, "Expected ')' after delay in assign");
                    } else {
                        advance();
                    }
                }
                if (match(TokenType::TOKEN_LBRACE)) {
                    if (current.type == TokenType::TOKEN_SHR || current.type == TokenType::TOKEN_SHL) {
                        bool is_shr = (current.type == TokenType::TOKEN_SHR);
                        advance();
                        StreamDirection dir = is_shr ? StreamDirection::LEFT_TO_RIGHT : StreamDirection::RIGHT_TO_LEFT;
                        std::unique_ptr<ExpressionNode> slice_sz = nullptr;
                        if (current.type != TokenType::TOKEN_LBRACE) {
                            if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "byte") {
                                advance();
                                slice_sz = std::make_unique<NumberNode>(8ULL, 32);
                            } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "shortint") {
                                advance();
                                slice_sz = std::make_unique<NumberNode>(16ULL, 32);
                            } else if (current.type == TokenType::TOKEN_INTEGER || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "int")) {
                                advance();
                                slice_sz = std::make_unique<NumberNode>(32ULL, 32);
                            } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "longint") {
                                advance();
                                slice_sz = std::make_unique<NumberNode>(64ULL, 32);
                            } else if (current.type == TokenType::TOKEN_BIT || current.type == TokenType::TOKEN_LOGIC) {
                                advance();
                                slice_sz = std::make_unique<NumberNode>(1ULL, 32);
                            } else {
                                slice_sz = parse_expression();
                            }
                        } else {
                            slice_sz = std::make_unique<NumberNode>(1ULL, 32);
                        }
                        expect(TokenType::TOKEN_LBRACE, "Expected '{' in streaming concatenation");
                        std::vector<std::unique_ptr<ExpressionNode>> items;
                        while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                            items.push_back(parse_expression());
                            if (match(TokenType::TOKEN_COMMA)) continue;
                            break;
                        }
                        expect(TokenType::TOKEN_RBRACE, "Expected '}' closing streaming concatenation items");
                        expect(TokenType::TOKEN_RBRACE, "Expected '}' closing streaming concatenation");
                        expect(TokenType::TOKEN_EQUAL, "Expected '=' in continuous assign statement");
                        auto rhs = parse_expression();
                        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assign");
                        auto ca = std::make_unique<ContinuousAssignNode>("", std::move(rhs));
                        ca->stream_target = std::make_unique<StreamingConcatNode>(dir, std::move(slice_sz), std::move(items));
                        mod->continuous_assigns.push_back(std::move(ca));
                        continue;
                    }

                    // Multi-target concatenation assignment: assign { a, b, ... } = rhs;
                    std::vector<std::string> concat_targets;
                    std::vector<std::unique_ptr<ExpressionNode>> concat_target_exprs;
                    while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                        std::string t = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier in concatenation target list");
                        while (match(TokenType::TOKEN_DOT)) {
                            t += "." + current.text;
                            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in concat target");
                        }
                        std::unique_ptr<ExpressionNode> target_expr = nullptr;
                        if (match(TokenType::TOKEN_LBRACKET)) {
                            auto idx_expr = parse_expression();
                            if (match(TokenType::TOKEN_PLUS_COLON)) {
                                auto w_expr = parse_expression();
                                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                                auto inode = std::make_unique<IndexSelectNode>(t, std::move(idx_expr), std::move(w_expr));
                                inode->is_plus_colon = true;
                                target_expr = std::move(inode);
                            } else if (match(TokenType::TOKEN_MINUS_COLON)) {
                                auto w_expr = parse_expression();
                                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                                auto inode = std::make_unique<IndexSelectNode>(t, std::move(idx_expr), std::move(w_expr));
                                inode->is_minus_colon = true;
                                target_expr = std::move(inode);
                            } else if (match(TokenType::TOKEN_COLON)) {
                                auto lsb_expr = parse_expression();
                                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                                auto inode = std::make_unique<IndexSelectNode>(t, std::move(idx_expr), std::move(lsb_expr));
                                inode->is_part_select = true;
                                target_expr = std::move(inode);
                            } else {
                                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                                target_expr = std::make_unique<IndexSelectNode>(t, std::move(idx_expr));
                            }
                        } else {
                            target_expr = std::make_unique<IdentifierNode>(t);
                        }
                        concat_targets.push_back(t);
                        concat_target_exprs.push_back(std::move(target_expr));
                        if (match(TokenType::TOKEN_COMMA)) continue;
                        break;
                    }
                    expect(TokenType::TOKEN_RBRACE, "Expected '}' closing concatenation target list");
                    expect(TokenType::TOKEN_EQUAL, "Expected '=' in continuous assign statement");
                    auto rhs = parse_expression();
                    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assign");
                    auto ca = std::make_unique<ContinuousAssignNode>("", std::move(rhs));
                    ca->concat_targets = std::move(concat_targets);
                    ca->concat_target_exprs = std::move(concat_target_exprs);
                    mod->continuous_assigns.push_back(std::move(ca));
                    continue;
                }
                std::string target = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected target identifier in assign");
                while (match(TokenType::TOKEN_DOT)) {
                    target += "." + current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in assign target");
                }
                std::unique_ptr<ExpressionNode> target_idx = nullptr;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    auto idx_expr = parse_expression();
                    if (match(TokenType::TOKEN_PLUS_COLON)) {
                        auto w_expr = parse_expression();
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                        auto inode = std::make_unique<IndexSelectNode>(target, std::move(idx_expr), std::move(w_expr));
                        inode->is_plus_colon = true;
                        target_idx = std::move(inode);
                    } else if (match(TokenType::TOKEN_MINUS_COLON)) {
                        auto w_expr = parse_expression();
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                        auto inode = std::make_unique<IndexSelectNode>(target, std::move(idx_expr), std::move(w_expr));
                        inode->is_minus_colon = true;
                        target_idx = std::move(inode);
                    } else if (match(TokenType::TOKEN_COLON)) {
                        auto lsb_expr = parse_expression();
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                        auto inode = std::make_unique<IndexSelectNode>(target, std::move(idx_expr), std::move(lsb_expr));
                        inode->is_part_select = true;
                        target_idx = std::move(inode);
                    } else {
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                        target_idx = std::make_unique<IndexSelectNode>(target, std::move(idx_expr));
                    }
                    if (match(TokenType::TOKEN_LBRACKET)) {
                        if (auto inode = dynamic_cast<IndexSelectNode*>(target_idx.get())) {
                            inode->has_bit_select = true;
                            inode->bit_msb_expr = parse_expression();
                            if (match(TokenType::TOKEN_PLUS_COLON)) {
                                inode->bit_lsb_expr = parse_expression();
                                inode->is_plus_colon = true;
                            } else if (match(TokenType::TOKEN_MINUS_COLON)) {
                                inode->bit_lsb_expr = parse_expression();
                                inode->is_minus_colon = true;
                            } else if (match(TokenType::TOKEN_COLON)) {
                                inode->bit_lsb_expr = parse_expression();
                            }
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing bit-select in assign target");
                            while (match(TokenType::TOKEN_LBRACKET)) {
                                auto extra = parse_expression();
                                if (match(TokenType::TOKEN_COLON)) {
                                    auto lsb = parse_expression();
                                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing part-select in assign target");
                                    auto part_node = std::make_unique<IndexSelectNode>("", std::move(extra), std::move(lsb));
                                    inode->extra_indices.push_back(std::move(part_node));
                                } else if (match(TokenType::TOKEN_PLUS_COLON)) {
                                    auto w = parse_expression();
                                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing part-select in assign target");
                                    auto part_node = std::make_unique<IndexSelectNode>("", std::move(extra), std::move(w));
                                    part_node->is_plus_colon = true;
                                    inode->extra_indices.push_back(std::move(part_node));
                                } else if (match(TokenType::TOKEN_MINUS_COLON)) {
                                    auto w = parse_expression();
                                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing part-select in assign target");
                                    auto part_node = std::make_unique<IndexSelectNode>("", std::move(extra), std::move(w));
                                    part_node->is_minus_colon = true;
                                    inode->extra_indices.push_back(std::move(part_node));
                                } else {
                                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing index in assign target");
                                    inode->extra_indices.push_back(std::move(extra));
                                }
                            }
                        }
                    }
                }
                std::string member_name = "";
                std::unique_ptr<ExpressionNode> member_msb = nullptr;
                std::unique_ptr<ExpressionNode> member_lsb = nullptr;
                bool has_mem_sel = false;
                bool is_part_sel = false;
                while (match(TokenType::TOKEN_DOT)) {
                    if (!member_name.empty()) member_name += ".";
                    member_name += current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in assign target");
                    if (match(TokenType::TOKEN_LBRACKET)) {
                        has_mem_sel = true;
                        member_msb = parse_expression();
                        if (match(TokenType::TOKEN_COLON)) {
                            is_part_sel = true;
                            member_lsb = parse_expression();
                        }
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in member select in assign target");
                    }
                }
                expect(TokenType::TOKEN_EQUAL, "Expected '=' in assign statement");
                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assign");
                std::string var_type = "";
                for (const auto& s : mod->signals) {
                    if (s.name == target) {
                        var_type = s.type_name;
                        break;
                    }
                }
                if (!var_type.empty() && get_enum_typedef(var_type) != nullptr) {
                    if (dynamic_cast<NumberNode*>(rhs.get()) || dynamic_cast<BinaryOpNode*>(rhs.get())) {
                        throw std::runtime_error("Assigning numerical expression to enum variable '" + target + "' without explicit cast is illegal per IEEE 1800-2023 6.19.3/6.19.4");
                    }
                }
                auto ca = std::make_unique<ContinuousAssignNode>(target, std::move(rhs));
                ca->target_index_expr = std::move(target_idx);
                ca->member_name = member_name;
                ca->has_member_select = has_mem_sel;
                ca->is_part_select = is_part_sel;
                ca->member_msb_expr = std::move(member_msb);
                ca->member_lsb_expr = std::move(member_lsb);
                mod->continuous_assigns.push_back(std::move(ca));
                continue;
            }

            if (match(TokenType::TOKEN_GENVAR)) {
                while (true) {
                    std::string gname = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected genvar identifier");
                    mod->genvars.insert(gname);
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after genvar declaration");
                continue;
            }

            if (match(TokenType::TOKEN_GENERATE)) {
                while (current.type != TokenType::TOKEN_ENDGENERATE && current.type != TokenType::TOKEN_EOF) {
                    parse_generate_item(mod.get(), nullptr);
                }
                expect(TokenType::TOKEN_ENDGENERATE, "Expected 'endgenerate'");
                continue;
            }

            if (current.type == TokenType::TOKEN_FOR) {
                mod->generate_nodes.push_back(parse_generate_for(mod.get()));
                continue;
            }

            if (current.type == TokenType::TOKEN_IF) {
                mod->generate_nodes.push_back(parse_generate_if(mod.get()));
                continue;
            }

            if (current.type == TokenType::TOKEN_CASE) {
                mod->generate_nodes.push_back(parse_generate_case(mod.get()));
                continue;
            }

            if (current.type == TokenType::TOKEN_UNIQUE || current.type == TokenType::TOKEN_UNIQUE0 || current.type == TokenType::TOKEN_PRIORITY) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: '" + current.text + "' modifier is not permitted outside procedural or generate block statements per IEEE 1800-2023 section 12.5");
            }

            if (match(TokenType::TOKEN_DEFPARAM)) {
                if (is_prog) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: defparam statement is illegal in program scope per IEEE 1800-2023 section 24.3");
                }
                while (true) {
                    std::string hier_path = current.text;
                    std::string cur_file = current.filename;
                    size_t cur_line = current.line;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected hierarchical identifier in defparam");
                    while (match(TokenType::TOKEN_DOT)) {
                        hier_path += "." + current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in defparam");
                    }
                    expect(TokenType::TOKEN_EQUAL, "Expected '=' in defparam assignment");
                    auto expr = parse_expression();
                    mod->defparams.push_back(DefparamItemNode(hier_path, std::move(expr), cur_file, cur_line));
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after defparam statement");
                continue;
            }

            if (match(TokenType::TOKEN_ALIAS)) {
                if (is_prog) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: 'alias' construct is not permitted in program scope per IEEE 1800-2023 section 24.3");
                }
                std::string cur_file = current.filename;
                size_t cur_line = current.line;
                std::vector<std::unique_ptr<ExpressionNode>> aliased_exprs;
                aliased_exprs.push_back(parse_net_lvalue());
                expect(TokenType::TOKEN_EQUAL, "Expected '=' in alias statement");
                aliased_exprs.push_back(parse_net_lvalue());
                while (match(TokenType::TOKEN_EQUAL)) {
                    aliased_exprs.push_back(parse_net_lvalue());
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after alias statement");
                mod->aliases.push_back(std::make_unique<AliasStatementNode>(std::move(aliased_exprs), cur_file, cur_line));
                continue;
            }

            bool is_builtin_gate = (current.type == TokenType::TOKEN_GATE_AND  || current.type == TokenType::TOKEN_GATE_OR   ||
                                    current.type == TokenType::TOKEN_GATE_XOR  || current.type == TokenType::TOKEN_GATE_NAND ||
                                    current.type == TokenType::TOKEN_GATE_NOR  || current.type == TokenType::TOKEN_GATE_XNOR ||
                                    current.type == TokenType::TOKEN_GATE_NOT  || current.type == TokenType::TOKEN_GATE_BUF   ||
                                    current.type == TokenType::TOKEN_GATE_BUFIF0 || current.type == TokenType::TOKEN_GATE_BUFIF1 ||
                                    current.type == TokenType::TOKEN_GATE_NOTIF0 || current.type == TokenType::TOKEN_GATE_NOTIF1 ||
                                    current.type == TokenType::TOKEN_OR);
            bool is_udp_inst = (current.type == TokenType::TOKEN_IDENTIFIER && Parser::global_udp_defs.find(current.text) != Parser::global_udp_defs.end());

            if (is_builtin_gate || is_udp_inst) {
                GateType gt = GateType::AND;
                std::string udp_type = "";
                if (is_udp_inst) {
                    gt = GateType::UDP;
                    udp_type = current.text;
                } else if (current.type == TokenType::TOKEN_GATE_OR || current.type == TokenType::TOKEN_OR) gt = GateType::OR;
                else if (current.type == TokenType::TOKEN_GATE_XOR) gt = GateType::XOR;
                else if (current.type == TokenType::TOKEN_GATE_NAND) gt = GateType::NAND;
                else if (current.type == TokenType::TOKEN_GATE_NOR) gt = GateType::NOR;
                else if (current.type == TokenType::TOKEN_GATE_XNOR) gt = GateType::XNOR;
                else if (current.type == TokenType::TOKEN_GATE_NOT) gt = GateType::NOT;
                else if (current.type == TokenType::TOKEN_GATE_BUF) gt = GateType::BUF;
                else if (current.type == TokenType::TOKEN_GATE_BUFIF0) gt = GateType::BUFIF0;
                else if (current.type == TokenType::TOKEN_GATE_BUFIF1) gt = GateType::BUFIF1;
                else if (current.type == TokenType::TOKEN_GATE_NOTIF0) gt = GateType::NOTIF0;
                else if (current.type == TokenType::TOKEN_GATE_NOTIF1) gt = GateType::NOTIF1;
                advance();

                // Optional gate delay: #delay or #(delay) or #(d1, d2)
                if (match(TokenType::TOKEN_HASH)) {
                    if (match(TokenType::TOKEN_LPAREN)) {
                        while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) advance();
                        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing gate delay");
                    } else {
                        parse_expression();
                    }
                }

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

                auto gate = std::make_unique<GatePrimitiveNode>(gt, std::move(out_expr), std::move(in_exprs), inst_name, udp_type);
                mod->gates.push_back(std::move(gate));
                continue;
            }

            if (current.type == TokenType::TOKEN_SPECIFY) {
                if (mod->is_interface) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: 'specify' block is not permitted inside interface per IEEE 1800-2023 section 32");
                }
                if (mod->is_program) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: 'specify' block is not permitted inside program per IEEE 1800-2023 section 32");
                }
                advance(); // consume 'specify'
                while (current.type != TokenType::TOKEN_ENDSPECIFY && current.type != TokenType::TOKEN_EOF) {
                    std::unique_ptr<ExpressionNode> cond_expr = nullptr;
                    if (match(TokenType::TOKEN_IF)) {
                        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'if' in specify block");
                        cond_expr = parse_expression();
                        expect(TokenType::TOKEN_RPAREN, "Expected ')' after condition in specify block");
                    } else if (match(TokenType::TOKEN_IFNONE)) {
                        // ifnone path
                    }

                    if (match(TokenType::TOKEN_LPAREN)) {
                        std::vector<std::string> sources;
                        while (true) {
                            sources.push_back(current.text);
                            expect(TokenType::TOKEN_IDENTIFIER, "Expected source terminal in path declaration");
                            if (match(TokenType::TOKEN_COMMA)) continue;
                            break;
                        }

                        bool is_full_conn = false;
                        if (match(TokenType::TOKEN_FAT_ARROW)) {
                            is_full_conn = false;
                        } else if (match(TokenType::TOKEN_STAR_ARROW)) {
                            is_full_conn = true;
                        } else {
                            expect(TokenType::TOKEN_FAT_ARROW, "Expected '=>' or '*>' in path declaration");
                        }

                        std::vector<std::string> dests;
                        while (true) {
                            dests.push_back(current.text);
                            expect(TokenType::TOKEN_IDENTIFIER, "Expected destination terminal in path declaration");
                            if (match(TokenType::TOKEN_COMMA)) continue;
                            break;
                        }
                        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing path terminals");
                        expect(TokenType::TOKEN_EQUAL, "Expected '=' in path delay assignment");

                        std::unique_ptr<ExpressionNode> delay_expr = nullptr;
                        if (match(TokenType::TOKEN_LPAREN)) {
                            delay_expr = parse_expression();
                            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing delay value");
                        } else {
                            delay_expr = parse_expression();
                        }
                        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after path delay declaration");

                        auto path_node = std::make_shared<PathDelayNode>();
                        path_node->sources = std::move(sources);
                        path_node->destinations = std::move(dests);
                        path_node->is_full_connection = is_full_conn;
                        path_node->cond_expr = std::move(cond_expr);
                        path_node->delay_expr = std::move(delay_expr);
                        mod->path_delays.push_back(path_node);
                    } else if (current.type == TokenType::TOKEN_SYS_SETUP || current.type == TokenType::TOKEN_SYS_HOLD ||
                               current.type == TokenType::TOKEN_SYS_WIDTH || current.type == TokenType::TOKEN_SYS_RECOVERY) {
                        auto parse_tc_event = [&]() -> TimingCheckEvent {
                            TimingCheckEvent ev;
                            if (match(TokenType::TOKEN_POSEDGE)) {
                                ev.edge = TimingCheckEdge::POSEDGE;
                            } else if (match(TokenType::TOKEN_NEGEDGE)) {
                                ev.edge = TimingCheckEdge::NEGEDGE;
                            } else {
                                ev.edge = TimingCheckEdge::ANY;
                            }
                            std::string sig = current.text;
                            expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier in timing check event");
                            while (match(TokenType::TOKEN_DOT)) {
                                sig += "." + current.text;
                                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in timing check event");
                            }
                            ev.signal_name = sig;
                            return ev;
                        };

                        if (match(TokenType::TOKEN_SYS_SETUP)) {
                            auto tc = std::make_shared<TimingCheckNode>();
                            tc->type = TimingCheckType::SETUP;
                            tc->file = current.filename;
                            tc->line = current.line;
                            expect(TokenType::TOKEN_LPAREN, "Expected '(' after $setup");
                            tc->data_event = parse_tc_event();
                            expect(TokenType::TOKEN_COMMA, "Expected ',' after data event in $setup");
                            tc->ref_event = parse_tc_event();
                            expect(TokenType::TOKEN_COMMA, "Expected ',' after reference event in $setup");
                            tc->limit_expr = parse_expression();
                            if (match(TokenType::TOKEN_COMMA)) {
                                tc->notifier_name = current.text;
                                expect(TokenType::TOKEN_IDENTIFIER, "Expected notifier identifier in $setup");
                            }
                            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $setup");
                            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $setup");
                            mod->timing_checks.push_back(tc);
                        } else if (match(TokenType::TOKEN_SYS_HOLD)) {
                            auto tc = std::make_shared<TimingCheckNode>();
                            tc->type = TimingCheckType::HOLD;
                            tc->file = current.filename;
                            tc->line = current.line;
                            expect(TokenType::TOKEN_LPAREN, "Expected '(' after $hold");
                            tc->ref_event = parse_tc_event();
                            expect(TokenType::TOKEN_COMMA, "Expected ',' after reference event in $hold");
                            tc->data_event = parse_tc_event();
                            expect(TokenType::TOKEN_COMMA, "Expected ',' after data event in $hold");
                            tc->limit_expr = parse_expression();
                            if (match(TokenType::TOKEN_COMMA)) {
                                tc->notifier_name = current.text;
                                expect(TokenType::TOKEN_IDENTIFIER, "Expected notifier identifier in $hold");
                            }
                            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $hold");
                            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $hold");
                            mod->timing_checks.push_back(tc);
                        } else if (match(TokenType::TOKEN_SYS_WIDTH)) {
                            auto tc = std::make_shared<TimingCheckNode>();
                            tc->type = TimingCheckType::WIDTH;
                            tc->file = current.filename;
                            tc->line = current.line;
                            expect(TokenType::TOKEN_LPAREN, "Expected '(' after $width");
                            tc->ref_event = parse_tc_event();
                            expect(TokenType::TOKEN_COMMA, "Expected ',' after reference event in $width");
                            tc->limit_expr = parse_expression();
                            if (match(TokenType::TOKEN_COMMA)) {
                                tc->threshold_expr = parse_expression();
                                if (match(TokenType::TOKEN_COMMA)) {
                                    tc->notifier_name = current.text;
                                    expect(TokenType::TOKEN_IDENTIFIER, "Expected notifier identifier in $width");
                                }
                            }
                            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $width");
                            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $width");
                            mod->timing_checks.push_back(tc);
                        } else if (match(TokenType::TOKEN_SYS_RECOVERY)) {
                            auto tc = std::make_shared<TimingCheckNode>();
                            tc->type = TimingCheckType::RECOVERY;
                            tc->file = current.filename;
                            tc->line = current.line;
                            expect(TokenType::TOKEN_LPAREN, "Expected '(' after $recovery");
                            tc->ref_event = parse_tc_event();
                            expect(TokenType::TOKEN_COMMA, "Expected ',' after reference event in $recovery");
                            tc->data_event = parse_tc_event();
                            expect(TokenType::TOKEN_COMMA, "Expected ',' after data event in $recovery");
                            tc->limit_expr = parse_expression();
                            if (match(TokenType::TOKEN_COMMA)) {
                                tc->notifier_name = current.text;
                                expect(TokenType::TOKEN_IDENTIFIER, "Expected notifier identifier in $recovery");
                            }
                            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing $recovery");
                            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after $recovery");
                            mod->timing_checks.push_back(tc);
                        }
                    } else {
                        while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_ENDSPECIFY && current.type != TokenType::TOKEN_EOF) {
                            advance();
                        }
                        match(TokenType::TOKEN_SEMICOLON);
                    }
                }
                expect(TokenType::TOKEN_ENDSPECIFY, "Expected 'endspecify'");
                continue;
            }

            if (current.type == TokenType::TOKEN_SYS_SETUP || current.type == TokenType::TOKEN_SYS_HOLD ||
                current.type == TokenType::TOKEN_SYS_WIDTH || current.type == TokenType::TOKEN_SYS_RECOVERY) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: timing check system task '" + current.text + "' is not permitted outside specify block per IEEE 1800-2023 section 31.8 & 32.7");
            }

            bool is_virt = false;
            bool is_stat = false;
            bool is_ext = false;
            if (match(TokenType::TOKEN_VIRTUAL)) is_virt = true;
            if (match(TokenType::TOKEN_STATIC)) is_stat = true;
            if (match(TokenType::TOKEN_EXTERN)) is_ext = true;
            // Function declaration parsed by parser_function.cpp
            if (match(TokenType::TOKEN_FUNCTION)) {
                auto fn = parse_function();
                if (fn) {
                    if (fn->name.find("::") != std::string::npos) {
                        if (is_virt || is_stat || is_ext || fn->is_static) {
                            throw std::runtime_error("[CAS-DTINPCIL] Out-of-body method implementation cannot specify qualifiers ('virtual', 'static', 'extern') per IEEE 1800-2023 section 8.24: '" + fn->name + "'");
                        }
                        bind_out_of_body_function(std::move(fn));
                    } else {
                        fn->is_virtual = is_virt;
                        if (is_stat) fn->is_static = true;
                        mod->functions.push_back(std::move(fn));
                    }
                }
                continue;
            }

            if (match(TokenType::TOKEN_TASK)) {
                auto t = parse_task();
                if (t) {
                    if (t->name.find("::") != std::string::npos) {
                        if (is_virt || is_stat || is_ext || t->is_static) {
                            throw std::runtime_error("[CAS-DTINPCIL] Out-of-body task implementation cannot specify qualifiers ('virtual', 'static', 'extern') per IEEE 1800-2023 section 8.24: '" + t->name + "'");
                        }
                        bind_out_of_body_task(std::move(t));
                    } else {
                        t->is_virtual = is_virt;
                        if (is_stat) t->is_static = true;
                        mod->tasks.push_back(std::move(t));
                    }
                }
                continue;
            }

            if (current.type == TokenType::TOKEN_IMPORT) {
                if (peek_token().type == TokenType::TOKEN_STRING) {
                    auto dpi = parse_dpi_import();
                    if (dpi) {
                        std::shared_ptr<DpiImportNode> sdpi = std::move(dpi);
                        mod->dpi_imports.push_back(sdpi);
                        mod->dpi_import_map[sdpi->sv_name] = sdpi.get();
                    }
                } else {
                    advance(); // consume 'import'
                    while (true) {
                        std::string pkg_name = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected package name in import");
                        expect(TokenType::TOKEN_COLON_COLON, "Expected '::' after package name in import");
                        if (match(TokenType::TOKEN_STAR)) {
                            if (std::find(mod->wildcard_imports.begin(), mod->wildcard_imports.end(), pkg_name) == mod->wildcard_imports.end()) {
                                mod->wildcard_imports.push_back(pkg_name);
                            }
                        } else {
                            std::string item_name = current.text;
                            expect(TokenType::TOKEN_IDENTIFIER, "Expected item name or '*' in import");
                            mod->explicit_imports[item_name] = {pkg_name, item_name};
                        }
                        if (match(TokenType::TOKEN_COMMA)) continue;
                        break;
                    }
                    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after import declaration");
                }
                continue;
            }

            if (current.type == TokenType::TOKEN_EXPORT) {
                auto exp = parse_dpi_export();
                if (exp) {
                    std::shared_ptr<DpiExportNode> sexp = std::move(exp);
                    mod->dpi_exports.push_back(sexp);
                    mod->dpi_export_map[sexp->c_name] = sexp.get();
                    mod->dpi_export_map[sexp->sv_name] = sexp.get();
                    dpi_export_map[sexp->c_name] = sexp;
                    dpi_export_map[sexp->sv_name] = sexp;
                }
                continue;
            }

            if (match(TokenType::TOKEN_INITIAL)) {
                auto proc = std::make_unique<ProcessNode>(ProcessType::INITIAL);
                proc->body = parse_statement_or_block();
                mod->processes.push_back(std::move(proc));
                continue;
            }

            if (match(TokenType::TOKEN_FINAL)) {
                in_final_procedure = true;
                auto proc = std::make_unique<ProcessNode>(ProcessType::FINAL);
                proc->body = parse_statement_or_block();
                in_final_procedure = false;
                mod->processes.push_back(std::move(proc));
                continue;
            }

            if (match(TokenType::TOKEN_ALWAYS_COMB)) {
                if (is_prog) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: always blocks are prohibited inside program blocks per IEEE 1800-2023 section 24.3");
                }
                auto proc = std::make_unique<ProcessNode>(ProcessType::COMBINATIONAL);
                proc->is_always_comb = true;
                proc->body = parse_statement_or_block();
                mod->processes.push_back(std::move(proc));
                continue;
            }

            if (match(TokenType::TOKEN_ALWAYS_LATCH)) {
                if (is_prog) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: always blocks are prohibited inside program blocks per IEEE 1800-2023 section 24.3");
                }
                auto proc = std::make_unique<ProcessNode>(ProcessType::COMBINATIONAL);
                proc->is_always_latch = true;
                proc->body = parse_statement_or_block();
                mod->processes.push_back(std::move(proc));
                continue;
            }

            if (match(TokenType::TOKEN_ALWAYS_FF)) {
                if (is_prog) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: always blocks are prohibited inside program blocks per IEEE 1800-2023 section 24.3");
                }
                auto proc = parse_always_ff();
                proc->is_always_ff = true;
                mod->processes.push_back(std::move(proc));
                continue;
            }

            if (match(TokenType::TOKEN_ALWAYS)) {
                if (is_prog) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: always blocks are prohibited inside program blocks per IEEE 1800-2023 section 24.3");
                }
                if (current.type == TokenType::TOKEN_AT) {
                    advance();
                    if (match(TokenType::TOKEN_STAR)) {
                        auto proc = std::make_unique<ProcessNode>(ProcessType::COMBINATIONAL);
                        proc->body = parse_statement_or_block();
                        mod->processes.push_back(std::move(proc));
                        continue;
                    }
                    if (match(TokenType::TOKEN_LPAREN)) {
                        if (match(TokenType::TOKEN_STAR)) {
                            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing sensitivity list");
                            auto proc = std::make_unique<ProcessNode>(ProcessType::COMBINATIONAL);
                            proc->body = parse_statement_or_block();
                            mod->processes.push_back(std::move(proc));
                            continue;
                        }
                        auto proc = std::make_unique<ProcessNode>(ProcessType::SEQUENTIAL);
                        while (true) {
                            EdgeType edge = EdgeType::LEVEL;
                            bool is_edge_kw = false;
                            if (match(TokenType::TOKEN_POSEDGE)) edge = EdgeType::POSEDGE;
                            else if (match(TokenType::TOKEN_NEGEDGE)) edge = EdgeType::NEGEDGE;
                            else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "edge") {
                                advance();
                                is_edge_kw = true;
                            }

                            if (current.type == TokenType::TOKEN_NOT || current.type == TokenType::TOKEN_TILDE ||
                                current.type == TokenType::TOKEN_AND || current.type == TokenType::TOKEN_OR_BIT) {
                                advance();
                            }
                            std::string sig = current.text;
                            expect(TokenType::TOKEN_IDENTIFIER, "Expected signal name in sensitivity list");
                            while (match(TokenType::TOKEN_DOT)) {
                                sig += "." + current.text;
                                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in sensitivity list");
                            }
                            while (match(TokenType::TOKEN_LBRACKET)) {
                                parse_expression();
                                if (match(TokenType::TOKEN_COLON)) parse_expression();
                                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in sensitivity list select");
                            }
                            if (match(TokenType::TOKEN_IFF)) {
                                parse_expression();
                            } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "iff") {
                                advance();
                                parse_expression();
                            }
                            if (is_edge_kw) {
                                proc->sensitivity_list.push_back({EdgeType::POSEDGE, sig});
                                proc->sensitivity_list.push_back({EdgeType::NEGEDGE, sig});
                            } else {
                                proc->sensitivity_list.push_back({edge, sig});
                            }

                            if (match(TokenType::TOKEN_OR) || match(TokenType::TOKEN_GATE_OR) || match(TokenType::TOKEN_COMMA)) continue;
                            break;
                        }
                        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing sensitivity list");
                        proc->body = parse_statement_or_block();
                        mod->processes.push_back(std::move(proc));
                    } else if (current.type == TokenType::TOKEN_IDENTIFIER) {
                        auto proc = std::make_unique<ProcessNode>(ProcessType::SEQUENTIAL);
                        std::string sig = current.text;
                        advance();
                        while (match(TokenType::TOKEN_DOT)) {
                            sig += "." + current.text;
                            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in sensitivity list");
                        }
                        proc->sensitivity_list.push_back({EdgeType::LEVEL, sig});
                        proc->body = parse_statement_or_block();
                        mod->processes.push_back(std::move(proc));
                    }
                } else {
                    auto proc = std::make_unique<ProcessNode>(ProcessType::ALWAYS);
                    proc->body = parse_statement_or_block();
                    mod->processes.push_back(std::move(proc));
                }
                continue;
            }

            if (match(TokenType::TOKEN_SEMICOLON)) continue;

            if (current.type == TokenType::TOKEN_ASSERT || current.type == TokenType::TOKEN_ASSUME || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "cover")) {
                auto asrt = parse_assert_property("");
                if (asrt) {
                    mod->assertions.push_back(std::move(asrt));
                }
                continue;
            }

            if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON) {
                std::string label = current.text;
                advance(); // ident
                advance(); // colon
                if (current.type == TokenType::TOKEN_ASSERT || current.type == TokenType::TOKEN_ASSUME || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "cover")) {
                    auto asrt = parse_assert_property(label);
                    if (asrt) {
                        mod->assertions.push_back(std::move(asrt));
                    }
                    continue;
                }
            }

            if (current.type == TokenType::TOKEN_IDENTIFIER) {
                std::string mtype = current.text;
                advance();

                std::vector<ParamOverride> param_overrides;
                if (match(TokenType::TOKEN_HASH)) {
                    expect(TokenType::TOKEN_LPAREN, "Expected '(' in parameter override list");
                    while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                        if (match(TokenType::TOKEN_DOT)) {
                            std::string pname = current.text;
                            expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter name after '.'");
                            if (match(TokenType::TOKEN_LPAREN)) {
                                std::unique_ptr<ExpressionNode> pval = nullptr;
                                if (current.type != TokenType::TOKEN_RPAREN) {
                                    pval = parse_expression();
                                }
                                expect(TokenType::TOKEN_RPAREN, "Expected ')' in named parameter override");
                                param_overrides.push_back({pname, std::move(pval)});
                            } else {
                                param_overrides.push_back({pname, std::make_unique<IdentifierNode>(pname)});
                            }
                        } else {
                            param_overrides.push_back({"", parse_expression()});
                        }
                        if (match(TokenType::TOKEN_COMMA)) continue;
                        break;
                    }
                    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing parameter override list");
                }

                std::string iname = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected instance identifier");
                bool is_instance_array = false;
                int64_t arr_msb = 0, arr_lsb = 0;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    is_instance_array = true;
                    auto r_msb = parse_expression();
                    if (auto num = dynamic_cast<NumberNode*>(r_msb.get())) arr_msb = num->aval;
                    if (match(TokenType::TOKEN_COLON)) {
                        auto r_lsb = parse_expression();
                        if (auto num = dynamic_cast<NumberNode*>(r_lsb.get())) arr_lsb = num->aval;
                    } else {
                        arr_lsb = 0;
                        arr_msb = arr_msb > 0 ? (arr_msb - 1) : 0;
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in instance range");
                }
                if (match(TokenType::TOKEN_SEMICOLON)) {
                    if (is_instance_array) {
                        int64_t low = std::min(arr_msb, arr_lsb);
                        int64_t high = std::max(arr_msb, arr_lsb);
                        for (int64_t idx = low; idx <= high; ++idx) {
                            std::string elem_name = iname + "[" + std::to_string(idx) + "]";
                            auto inst = std::make_unique<InstantiationNode>(mtype, elem_name, false);
                            for (const auto& po : param_overrides) {
                                inst->param_overrides.push_back({po.param_name, po.expr ? po.expr->clone() : nullptr});
                            }
                            mod->instantiations.push_back(std::move(inst));
                        }
                    } else {
                        auto inst = std::make_unique<InstantiationNode>(mtype, iname, false);
                        inst->param_overrides = std::move(param_overrides);
                        mod->instantiations.push_back(std::move(inst));
                    }
                    continue;
                }
                expect(TokenType::TOKEN_LPAREN, "Expected '(' in instance port connections");

                bool positional = false;
                std::vector<PortConnection> conns;
                while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                    if (match(TokenType::TOKEN_COMMA)) {
                        positional = true;
                        conns.push_back({"", nullptr});
                        continue;
                    }
                    if (match(TokenType::TOKEN_DOT)) {
                        if (match(TokenType::TOKEN_STAR)) {
                            conns.push_back({"*", nullptr});
                        } else {
                            std::string port_name = current.text;
                            expect(TokenType::TOKEN_IDENTIFIER, "Expected port name after '.'");
                            if (match(TokenType::TOKEN_LPAREN)) {
                                std::unique_ptr<ExpressionNode> expr = nullptr;
                                if (current.type != TokenType::TOKEN_RPAREN) {
                                    expr = parse_expression();
                                }
                                expect(TokenType::TOKEN_RPAREN, "Expected ')' closing port connection");
                                conns.push_back({port_name, std::move(expr)});
                            } else {
                                PortConnection pc;
                                pc.port_name = port_name;
                                pc.expr = std::make_unique<IdentifierNode>(port_name);
                                pc.is_implicit_name_only = true;
                                conns.push_back(std::move(pc));
                            }
                        }
                    } else {
                        positional = true;
                        auto expr = parse_expression();
                        conns.push_back({"", std::move(expr)});
                    }
                    if (match(TokenType::TOKEN_COMMA)) {
                        if (current.type == TokenType::TOKEN_COMMA) {
                            positional = true;
                            conns.push_back({"", nullptr});
                            advance();
                        }
                        continue;
                    }
                    break;
                }
                expect(TokenType::TOKEN_RPAREN, "Expected ')' closing instance port connections");
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after instance");

                if (is_prog) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: module, interface, or program instantiation '" + iname + "' is illegal in program scope per IEEE 1800-2023 section 24.3");
                }

                if (is_instance_array) {
                    int64_t low = std::min(arr_msb, arr_lsb);
                    int64_t high = std::max(arr_msb, arr_lsb);
                    for (int64_t idx = low; idx <= high; ++idx) {
                        std::string elem_name = iname + "[" + std::to_string(idx) + "]";
                        auto inst = std::make_unique<InstantiationNode>(mtype, elem_name, positional);
                        for (const auto& po : param_overrides) {
                            inst->param_overrides.push_back({po.param_name, po.expr ? po.expr->clone() : nullptr});
                        }
                        for (const auto& pc : conns) {
                            inst->connections.push_back({pc.port_name, pc.expr ? pc.expr->clone() : nullptr, pc.is_implicit_name_only});
                        }
                        mod->instantiations.push_back(std::move(inst));
                    }
                } else {
                    auto inst = std::make_unique<InstantiationNode>(mtype, iname, positional);
                    inst->param_overrides = std::move(param_overrides);
                    inst->connections = std::move(conns);
                    mod->instantiations.push_back(std::move(inst));
                }
                continue;
            }

            advance();
        }
        if (is_iface) {
            expect(TokenType::TOKEN_ENDINTERFACE, "Expected 'endinterface'");
        } else if (is_prog) {
            expect(TokenType::TOKEN_ENDPROGRAM, "Expected 'endprogram'");
        } else {
            expect(TokenType::TOKEN_ENDMODULE, "Expected 'endmodule'");
        }
        if (match(TokenType::TOKEN_COLON)) {
            if (current.type == TokenType::TOKEN_IDENTIFIER) {
                if (!mod->name.empty() && current.text != mod->name) {
                    throw std::runtime_error("End label '" + current.text + "' does not match definition name '" + mod->name + "'");
                }
                advance();
            }
        }
        match(TokenType::TOKEN_SEMICOLON);

        for (const auto& exp : mod->dpi_exports) {
            bool found = false;
            if (!exp->is_task) {
                for (const auto& f : mod->functions) {
                    if (f->name == exp->sv_name) {
                        found = true;
                        break;
                    }
                }
            } else {
                for (const auto& t : mod->tasks) {
                    if (t->name == exp->sv_name) {
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                throw std::runtime_error("[DPI-FNF] Exported DPI-C routine '" + exp->sv_name + "' was not found in module scope '" + mod->name + "'");
            }
        }

        current_parsing_module = nullptr;
        modules.push_back(std::move(mod));
    }

    for (const auto& ft : forward_typedefs) {
        if (!resolved_typedefs.count(ft)) {
            throw std::runtime_error("missing forward typedef declaration, type_identifier '" + ft + "' does not resolve to a data type per IEEE 1800-2023 6.18");
        }
    }

    return modules;
}
std::string Parser::parse_skew_spec() {
    if (match(TokenType::TOKEN_HASH)) {
        if (current.type == TokenType::TOKEN_1STEP) {
            advance();
            return "1step";
        }
        if (current.type == TokenType::TOKEN_NUMBER) {
            std::string num = current.text;
            advance();
            if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "step") {
                advance();
                return num + "step";
            }
            return num;
        }
        if (current.type == TokenType::TOKEN_IDENTIFIER) {
            std::string id = current.text;
            advance();
            if (id == "1step") return "1step";
            return id;
        }
    }
    return "";
}

std::shared_ptr<StructTypeNode> Parser::parse_struct_or_union_body() {
    bool is_union = (current.type == TokenType::TOKEN_UNION);
    advance(); // consume struct or union

    bool is_tagged = false;
    bool is_packed = false;
    bool is_signed = false;

    while (current.type == TokenType::TOKEN_TAGGED || current.type == TokenType::TOKEN_PACKED ||
           current.type == TokenType::TOKEN_SIGNED || current.type == TokenType::TOKEN_UNSIGNED) {
        if (match(TokenType::TOKEN_TAGGED)) is_tagged = true;
        else if (match(TokenType::TOKEN_PACKED)) is_packed = true;
        else if (match(TokenType::TOKEN_SIGNED)) is_signed = true;
        else if (match(TokenType::TOKEN_UNSIGNED)) is_signed = false;
    }

    expect(TokenType::TOKEN_LBRACE, "Expected '{' in struct or union declaration");
    auto struct_node = std::make_shared<StructTypeNode>();
    struct_node->is_packed = is_packed;
    struct_node->is_signed = is_signed;
    struct_node->is_union = is_union;
    struct_node->is_tagged = is_tagged;

    while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
        if (match(TokenType::TOKEN_RAND) || match(TokenType::TOKEN_RANDC)) {
            // consume rand / randc qualifier inside struct
        }
        bool mem_signed = false;
        std::string mem_type = "logic";
        uint32_t field_width = 1;
        std::vector<std::pair<int64_t, int64_t>> member_packed_dims;

        std::shared_ptr<StructTypeNode> nested_st = nullptr;
        if (current.type == TokenType::TOKEN_STRUCT || current.type == TokenType::TOKEN_UNION) {
            nested_st = parse_struct_or_union_body();
            std::string anon_st_name = "__anon_nested_struct_" + std::to_string(anon_struct_counter++);
            struct_typedef_map[anon_st_name] = nested_st;
            if (current_parsing_module) current_parsing_module->struct_typedefs[anon_st_name] = nested_st;
            else if (current_parsing_package) current_parsing_package->struct_typedefs[anon_st_name] = nested_st;
            else global_struct_typedefs[anon_st_name] = nested_st;
            mem_type = anon_st_name;
            field_width = nested_st->total_width;
            mem_signed = nested_st->is_signed;
        } else if (match(TokenType::TOKEN_VOID)) {
            mem_type = "void";
            field_width = 0;
        } else {
            if (match(TokenType::TOKEN_SIGNED)) mem_signed = true;
            if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG ||
                current.type == TokenType::TOKEN_WIRE  || current.type == TokenType::TOKEN_INTEGER ||
                current.type == TokenType::TOKEN_BIT   ||
                (current.type == TokenType::TOKEN_IDENTIFIER &&
                 (current.text == "int" || current.text == "byte" || current.text == "bit" ||
                  current.text == "shortint" || current.text == "longint" || current.text == "time"))) {
                mem_type = current.text;
                if (current.type == TokenType::TOKEN_INTEGER || current.text == "int" ||
                    current.text == "byte" || current.text == "shortint" || current.text == "longint") {
                    mem_signed = true;
                }
                advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && (is_type_identifier(current.text) || is_typedef(current.text))) {
                mem_type = current.text;
                advance();
                while (match(TokenType::TOKEN_COLON_COLON)) {
                    mem_type += "::" + current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '::'");
                }
            }
            if (match(TokenType::TOKEN_SIGNED)) mem_signed = true;

            auto en_def = get_enum_typedef(mem_type);
            auto st_def = get_struct_typedef(mem_type);
            SimpleTypedef smp_t;
            if (mem_type == "int" || mem_type == "integer") field_width = 32;
            else if (mem_type == "byte") field_width = 8;
            else if (mem_type == "shortint") field_width = 16;
            else if (mem_type == "longint" || mem_type == "time") field_width = 64;
            else if (get_simple_typedef(mem_type, smp_t)) { field_width = smp_t.width; mem_signed = smp_t.is_signed; }
            else if (en_def) { field_width = en_def->base_width; mem_signed = en_def->is_signed; }
            else if (st_def) { field_width = st_def->total_width; mem_signed = st_def->is_signed; }

            uint32_t total_member_width = (field_width > 0) ? field_width : 1;
            bool has_dim = false;
            uint64_t msb_v = 0, lsb_v = 0;
            while (match(TokenType::TOKEN_LBRACKET)) {
                has_dim = true;
                auto msb_e = parse_expression();
                expect(TokenType::TOKEN_COLON, "Expected ':' in member range");
                auto lsb_e = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in member range");
                auto check_ident_defined = [&](ExpressionNode* e) {
                    std::function<void(ExpressionNode*)> walk = [&](ExpressionNode* node) {
                        if (!node) return;
                        if (auto id = dynamic_cast<IdentifierNode*>(node)) {
                            bool found = false;
                            if (current_parsing_module) {
                                for (const auto& p : current_parsing_module->parameters) {
                                    if (p.name == id->name) { found = true; break; }
                                }
                            }
                            if (!found) {
                                for (const auto& kv : global_packages) {
                                    if (kv.second->param_constants.count(id->name)) { found = true; break; }
                                }
                            }
                            if (!found) {
                                for (const auto& gp : global_parameters) {
                                    if (gp.name == id->name) { found = true; break; }
                                }
                            }
                            if (!found) {
                                throw std::runtime_error("Undefined parameter or identifier '" + id->name + "' in struct/union member range per IEEE 1800-2023 6.18");
                            }
                        } else if (auto bin = dynamic_cast<BinaryOpNode*>(node)) {
                            walk(bin->lhs.get()); walk(bin->rhs.get());
                        } else if (auto un = dynamic_cast<UnaryOpNode*>(node)) {
                            walk(un->operand.get());
                        }
                    };
                    walk(e);
                };
                check_ident_defined(msb_e.get());
                check_ident_defined(lsb_e.get());
                bool ok = false;
                try {
                    msb_v = eval_const_expr(msb_e.get());
                    lsb_v = eval_const_expr(lsb_e.get());
                    ok = true;
                } catch (...) {
                    if (auto nm = dynamic_cast<NumberNode*>(msb_e.get())) {
                        if (auto nl = dynamic_cast<NumberNode*>(lsb_e.get())) {
                            msb_v = nm->aval;
                            lsb_v = nl->aval;
                            ok = true;
                        }
                    }
                }
                if (!ok) {
                    throw std::runtime_error("Undefined parameter or expression in struct/union member range per IEEE 1800-2023 6.18");
                }
                uint32_t dim_w = 1;
                if (ok) {
                    dim_w = (msb_v >= lsb_v) ? (uint32_t)(msb_v - lsb_v + 1) : (uint32_t)(lsb_v - msb_v + 1);
                    member_packed_dims.push_back({(int64_t)msb_v, (int64_t)lsb_v});
                }
                total_member_width *= dim_w;
            }
            if (has_dim) {
                field_width = total_member_width;
            }
        }

        while (true) {
            std::string mem_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected member identifier");
            uint32_t unpacked_mult = 1;
            while (match(TokenType::TOKEN_LBRACKET)) {
                if (match(TokenType::TOKEN_DOLLAR)) {
                    if (match(TokenType::TOKEN_COLON)) {
                        parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in member queue dimension");
                    field_width = 64;
                    break;
                } else if (match(TokenType::TOKEN_RBRACKET)) {
                    field_width = 64;
                    break;
                } else if (match(TokenType::TOKEN_STAR)) {
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in member associative array dimension");
                    field_width = 64;
                    break;
                } else if (is_assoc_key_token(current)) {
                    std::string k_type;
                    uint32_t k_width = 0;
                    parse_assoc_key_type(k_type, k_width);
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in member associative array dimension");
                    field_width = 64;
                    break;
                } else {
                    auto u_msb = parse_expression();
                    uint64_t u_dim = 1;
                    if (match(TokenType::TOKEN_COLON)) {
                        auto u_lsb = parse_expression();
                        try {
                            uint64_t u_m = eval_const_expr(u_msb.get());
                            uint64_t u_l = eval_const_expr(u_lsb.get());
                            u_dim = (u_m >= u_l) ? (u_m - u_l + 1) : (u_l - u_m + 1);
                        } catch (...) {}
                    } else {
                        try {
                            u_dim = eval_const_expr(u_msb.get());
                        } catch (...) {}
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in member unpacked range");
                    unpacked_mult *= (uint32_t)u_dim;
                }
            }
            if (match(TokenType::TOKEN_REP_CONSEC)) {
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in member associative array dimension");
                field_width = 64;
            }
            StructMemberNode sm;
            sm.name = mem_name;
            sm.width = field_width * unpacked_mult;
            sm.is_signed = mem_signed;
            sm.type_name = mem_type;
            if (!member_packed_dims.empty()) {
                sm.msb = member_packed_dims[0].first;
                sm.lsb = member_packed_dims[0].second;
                if (member_packed_dims.size() > 1) {
                    int64_t outer_size = std::abs(member_packed_dims[0].first - member_packed_dims[0].second) + 1;
                    sm.elem_width = (outer_size > 0) ? (field_width / (uint32_t)outer_size) : 1;
                    sm.outer_low = std::min(member_packed_dims[0].first, member_packed_dims[0].second);
                } else {
                    sm.elem_width = 1;
                    sm.outer_low = std::min(member_packed_dims[0].first, member_packed_dims[0].second);
                }
            }
            if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                if (is_packed) {
                    throw std::runtime_error("Packed struct member declarations cannot have default values per IEEE 1800-2023 7.2.1");
                }
                sm.default_value = parse_expression();
            }
            struct_node->members.push_back(sm);
            if (!is_union) {
                struct_node->total_width += sm.width;
            }

            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after member declaration");
    }
    expect(TokenType::TOKEN_RBRACE, "Expected '}' closing struct or union declaration");

    if (is_union) {
        uint32_t max_payload = 0;
        for (const auto& sm : struct_node->members) {
            if (sm.width > max_payload) max_payload = sm.width;
        }
        struct_node->payload_width = max_payload;
        if (is_tagged) {
            size_t N = struct_node->members.size();
            uint32_t tag_bits = 1;
            while ((1ULL << tag_bits) < N) {
                tag_bits++;
            }
            struct_node->tag_bits = tag_bits;
            struct_node->total_width = tag_bits + max_payload;
        } else {
            struct_node->total_width = max_payload;
        }
    }

    return struct_node;
}

std::unique_ptr<TypedefNode> Parser::parse_typedef() {
    expect(TokenType::TOKEN_TYPEDEF, "Expected 'typedef'");

    // Forward typedef struct: typedef struct <name>;
    if (current.type == TokenType::TOKEN_STRUCT && peek_token_n(1).type == TokenType::TOKEN_IDENTIFIER && peek_token_n(2).type == TokenType::TOKEN_SEMICOLON) {
        advance(); // consume 'struct'
        std::string s_name = current.text;
        advance(); // consume s_name
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';'");
        register_forward_type(s_name);
        return std::make_unique<TypedefNode>(s_name, true, false);
    }
    // Forward typedef union: typedef union <name>;
    if (current.type == TokenType::TOKEN_UNION && peek_token_n(1).type == TokenType::TOKEN_IDENTIFIER && peek_token_n(2).type == TokenType::TOKEN_SEMICOLON) {
        advance(); // consume 'union'
        std::string u_name = current.text;
        advance(); // consume u_name
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';'");
        register_forward_type(u_name);
        return std::make_unique<TypedefNode>(u_name, true, false);
    }
    // Forward typedef enum: typedef enum <name>;
    if (current.type == TokenType::TOKEN_ENUM && peek_token_n(1).type == TokenType::TOKEN_IDENTIFIER && peek_token_n(2).type == TokenType::TOKEN_SEMICOLON) {
        advance(); // consume 'enum'
        std::string e_name = current.text;
        advance(); // consume e_name
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';'");
        register_forward_type(e_name);
        return std::make_unique<TypedefNode>(e_name, true, false);
    }
    // Forward typedef: typedef <name>;
    if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token_n(1).type == TokenType::TOKEN_SEMICOLON) {
        std::string t_name = current.text;
        advance(); // consume t_name
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';'");
        register_forward_type(t_name);
        return std::make_unique<TypedefNode>(t_name, true, false);
    }

    if (current.type == TokenType::TOKEN_CLASS ||
        (current.type == TokenType::TOKEN_INTERFACE && peek_token().type == TokenType::TOKEN_CLASS)) {
        bool is_iface = false;
        if (current.type == TokenType::TOKEN_INTERFACE) {
            is_iface = true;
            advance(); // consume 'interface'
        }
        advance(); // consume 'class'
        std::string cls_name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected class name after 'typedef class'");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after forward class declaration");

        if (current_parsing_package) {
            if (is_iface) current_parsing_package->forward_interface_classes.insert(cls_name);
            else current_parsing_package->forward_classes.insert(cls_name);
        } else if (current_parsing_module) {
            if (is_iface) current_parsing_module->forward_interface_classes.insert(cls_name);
            else current_parsing_module->forward_classes.insert(cls_name);
        } else {
            if (is_iface) global_forward_interface_classes.insert(cls_name);
            else global_forward_classes.insert(cls_name);
        }

        return std::make_unique<TypedefNode>(cls_name, true, is_iface);
    }

    if (current.type == TokenType::TOKEN_IDENTIFIER || current.type == TokenType::TOKEN_INTEGER ||
        current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_BIT ||
        current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_WIRE ||
        current.type == TokenType::TOKEN_REAL || current.type == TokenType::TOKEN_STRING ||
        current.type == TokenType::TOKEN_SIGNED || current.type == TokenType::TOKEN_UNSIGNED) {
        if (current.type != TokenType::TOKEN_STRUCT && current.type != TokenType::TOKEN_ENUM) {
            std::string base_t = current.text;
            advance();
            while (match(TokenType::TOKEN_COLON_COLON)) {
                base_t += "::" + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '::' in typedef");
            }
            if (match(TokenType::TOKEN_HASH)) {
                expect(TokenType::TOKEN_LPAREN, "Expected '(' after '#' in typedef");
                int pdepth = 1;
                while (pdepth > 0 && current.type != TokenType::TOKEN_EOF) {
                    if (current.type == TokenType::TOKEN_LPAREN) pdepth++;
                    else if (current.type == TokenType::TOKEN_RPAREN) pdepth--;
                    advance();
                }
            }
            bool is_sgn = (base_t == "integer" || base_t == "int" || base_t == "byte" || base_t == "shortint" || base_t == "longint" || base_t == "signed");
            if (match(TokenType::TOKEN_SIGNED)) {
                is_sgn = true;
            } else if (match(TokenType::TOKEN_UNSIGNED)) {
                is_sgn = false;
            }
            uint32_t width = 1;
            if (base_t == "integer" || base_t == "int") width = 32;
            else if (base_t == "byte") width = 8;
            else if (base_t == "shortint") width = 16;
            else if (base_t == "longint") width = 64;
            else if (base_t == "real") width = 64;
            else if (base_t == "shortreal") width = 32;
            else if (base_t == "string") width = 64;

            auto struct_def = get_struct_typedef(base_t);
            auto enum_def = get_enum_typedef(base_t);
            SimpleTypedef sm_def;
            bool has_sm = get_simple_typedef(base_t, sm_def);
            bool is_builtin = (base_t == "logic" || base_t == "bit" || base_t == "reg" || base_t == "wire" ||
                               base_t == "tri" || base_t == "wand" || base_t == "wor" ||
                               base_t == "integer" || base_t == "int" || base_t == "byte" ||
                               base_t == "shortint" || base_t == "longint" || base_t == "string" ||
                               base_t == "chandle" || base_t == "real" || base_t == "shortreal" ||
                               base_t == "void" || base_t == "signed" || base_t == "unsigned");
            bool is_type_param = false;
            if (current_parsing_class) {
                for (const auto& p : current_parsing_class->parameters) {
                    if (p.name == base_t) { is_type_param = true; break; }
                }
            }
            if (!is_builtin && !struct_def && !enum_def && !has_sm && !is_class(base_t) && !is_forward_class(base_t) && !is_type_param) {
                throw std::runtime_error("Defining a type using undefined type '" + base_t + "' per IEEE 1800-2023 6.18");
            }
            if (struct_def) {
                width = struct_def->total_width;
                is_sgn = struct_def->is_signed;
            } else if (enum_def) {
                width = enum_def->base_width;
                is_sgn = enum_def->is_signed;
            } else if (has_sm) {
                width = sm_def.width;
                is_sgn = sm_def.is_signed;
            }

            auto eval_dim_expr = [&](ExpressionNode* e) -> int64_t {
                if (!e) return 0;
                try {
                    const auto& pmap = current_parsing_package ? current_parsing_package->param_constants : std::unordered_map<std::string, uint64_t>{};
                    return (int64_t)eval_const_expr_with_params(e, pmap);
                } catch (...) {}
                if (auto num = dynamic_cast<NumberNode*>(e)) {
                    return (int64_t)num->aval;
                }
                return 0;
            };

            std::vector<AstDimension> packed_dims;
            while (match(TokenType::TOKEN_LBRACKET)) {
                auto msb_e = parse_expression();
                std::unique_ptr<ExpressionNode> lsb_e = nullptr;
                if (match(TokenType::TOKEN_COLON)) {
                    lsb_e = parse_expression();
                } else {
                    lsb_e = std::make_unique<NumberNode>(0, 32);
                    msb_e = std::make_unique<BinaryOpNode>("-", std::move(msb_e), std::make_unique<NumberNode>(1, 32));
                }
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in typedef range");
                int64_t msb_v = eval_dim_expr(msb_e.get());
                int64_t lsb_v = eval_dim_expr(lsb_e.get());
                uint32_t dim_w = (msb_v >= lsb_v) ? (uint32_t)(msb_v - lsb_v + 1) : (uint32_t)(lsb_v - msb_v + 1);
                if (dim_w == 0) dim_w = 1;
                width *= dim_w;
                AstDimension ad;
                ad.left_expr = std::move(msb_e);
                ad.right_expr = std::move(lsb_e);
                ad.is_single = false;
                packed_dims.push_back(std::move(ad));
            }
            if (packed_dims.empty()) {
                const auto* base_meta = find_type_dim_metadata(base_t);
                if (base_meta) {
                    uint32_t bw = 1;
                    for (const auto& d : base_meta->dimensions) {
                        if (!d.is_unpacked) bw *= (uint32_t)d.size();
                    }
                    if (bw > 1) width = bw;
                }
            }
            if (current.type == TokenType::TOKEN_IDENTIFIER) {
                std::string tname = current.text;
                advance();
                std::vector<AstDimension> unpacked_dims;
                bool is_unpacked_arr = false;
                bool is_dynamic_arr = false;
                uint32_t total_elements = 1;
                int64_t arr_start = 0;
                int64_t arr_end = 0;
                while (match(TokenType::TOKEN_LBRACKET) || match(TokenType::TOKEN_REP_CONSEC)) {
                    is_unpacked_arr = true;
                    if (current.type == TokenType::TOKEN_RBRACKET) {
                        advance();
                        is_dynamic_arr = true;
                        total_elements = 0;
                        continue;
                    }
                    if (match(TokenType::TOKEN_STAR)) {
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in associative array dimension");
                        is_dynamic_arr = true;
                        total_elements = 0;
                        continue;
                    }
                    if (match(TokenType::TOKEN_RBRACKET)) {
                        is_dynamic_arr = true;
                        total_elements = 0;
                        break;
                    }
                    // Associative array indexed by data type: e.g. [bit], [bit[31:0]], [string]
                    if (current.type == TokenType::TOKEN_BIT || current.type == TokenType::TOKEN_LOGIC ||
                        current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_REG ||
                        current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_STRING ||
                        (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "string" || is_type_identifier(current.text)))) {
                        advance();
                        while (match(TokenType::TOKEN_LBRACKET)) {
                            while (current.type != TokenType::TOKEN_RBRACKET && current.type != TokenType::TOKEN_EOF) advance();
                            match(TokenType::TOKEN_RBRACKET);
                        }
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in associative array dimension");
                        is_dynamic_arr = true;
                        total_elements = 0;
                        continue;
                    }
                    auto l = parse_expression();
                    std::unique_ptr<ExpressionNode> r = nullptr;
                    bool is_single = true;
                    if (match(TokenType::TOKEN_COLON)) {
                        r = parse_expression();
                        is_single = false;
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in typedef dimension");
                    int64_t b1 = eval_dim_expr(l.get());
                    int64_t b2 = r ? eval_dim_expr(r.get()) : 0;
                    if (is_single) {
                        arr_start = 0;
                        arr_end = b1 > 0 ? (b1 - 1) : 0;
                        total_elements *= (b1 > 0 ? (uint32_t)b1 : 1);
                    } else {
                        arr_start = b1;
                        arr_end = b2;
                        int64_t cnt = (b2 >= b1) ? (b2 - b1 + 1) : (b1 - b2 + 1);
                        total_elements *= (cnt > 0 ? (uint32_t)cnt : 1);
                    }
                    AstDimension ad;
                    ad.left_expr = std::move(l);
                    ad.right_expr = std::move(r);
                    ad.is_single = is_single;
                    unpacked_dims.push_back(std::move(ad));
                }
                match(TokenType::TOKEN_SEMICOLON);
                if (is_forward_class(tname)) {
                    throw std::runtime_error("[CAS-DTINPCIL] Identifier '" + tname + "' was declared as 'typedef class' but subsequently defined as a non-class type per IEEE 1800-2023 §6.18");
                }
                auto td = std::make_unique<TypedefNode>(tname, false, false);
                td->base_type = base_t;
                td->width = width;
                td->is_signed = is_sgn;
                td->packed_dims = packed_dims;

                if (is_dynamic_arr) {
                    td->is_unpacked_array = true;
                    td->is_dynamic_array = true;
                    td->elem_type = base_t;
                    td->elem_width = width;
                    td->elem_signed = is_sgn;
                    td->array_size = 0;

                    VariableDimMetadata vdm;
                    vdm.name = tname;
                    vdm.unpacked_dims = 1;
                    vdm.is_vector_or_array = true;
                    vdm.is_dynamic = true;
                    ArrayDimInfo dinfo;
                    dinfo.left = 0;
                    dinfo.right = -1;
                    dinfo.is_unpacked = true;
                    dinfo.is_dynamic = true;
                    vdm.dimensions.push_back(dinfo);

                    global_type_dimensions[tname] = vdm;
                    if (current_parsing_package) {
                        global_type_dimensions[current_parsing_package->name + "::" + tname] = vdm;
                        current_parsing_package->type_dimensions[tname] = vdm;
                    } else if (current_parsing_module) {
                        current_parsing_module->type_dimensions[tname] = vdm;
                    }

                    UnpackedArrayTypedef uinfo{tname, base_t, width, is_sgn, 0, 0, 0, true};
                    unpacked_array_typedef_map[tname] = uinfo;
                    global_unpacked_array_typedefs[tname] = uinfo;
                    if (current_parsing_package) {
                        current_parsing_package->unpacked_array_typedefs[tname] = uinfo;
                    } else if (current_parsing_module) {
                        current_parsing_module->unpacked_array_typedefs[tname] = uinfo;
                    }
                    return td;
                }

                VariableDimMetadata vdm;
                vdm.name = tname;
                for (const auto& udim : unpacked_dims) {
                    int64_t b1 = eval_dim_expr(udim.left_expr.get());
                    int64_t b2 = udim.right_expr ? eval_dim_expr(udim.right_expr.get()) : 0;
                    if (udim.is_single) {
                        b2 = b1 > 0 ? (b1 - 1) : 0;
                        b1 = 0;
                    }
                    ArrayDimInfo info;
                    info.left = b1;
                    info.right = b2;
                    info.is_unpacked = true;
                    vdm.dimensions.push_back(info);
                }
                vdm.unpacked_dims = (uint32_t)unpacked_dims.size();

                if (!packed_dims.empty()) {
                    for (const auto& pdim : packed_dims) {
                        int64_t b1 = eval_dim_expr(pdim.left_expr.get());
                        int64_t b2 = pdim.right_expr ? eval_dim_expr(pdim.right_expr.get()) : 0;
                        ArrayDimInfo info;
                        info.left = b1;
                        info.right = b2;
                        info.is_unpacked = false;
                        vdm.dimensions.push_back(info);
                    }
                    const VariableDimMetadata* base_vdm = find_type_dim_metadata(base_t);
                    if (base_vdm) {
                        for (const auto& d : base_vdm->dimensions) {
                            if (!d.is_unpacked) {
                                vdm.dimensions.push_back(d);
                            }
                        }
                    } else if (struct_def && struct_def->is_packed && struct_def->total_width > 0) {
                        vdm.dimensions.push_back(ArrayDimInfo{(int64_t)struct_def->total_width - 1, 0, false});
                    } else if (base_t == "int" || base_t == "integer") {
                        vdm.dimensions.push_back(ArrayDimInfo{31, 0, false});
                    } else if (base_t == "byte") {
                        vdm.dimensions.push_back(ArrayDimInfo{7, 0, false});
                    } else if (base_t == "shortint") {
                        vdm.dimensions.push_back(ArrayDimInfo{15, 0, false});
                    } else if (base_t == "longint") {
                        vdm.dimensions.push_back(ArrayDimInfo{63, 0, false});
                    }
                } else {
                    const VariableDimMetadata* base_vdm = find_type_dim_metadata(base_t);
                    if (base_vdm) {
                        for (const auto& d : base_vdm->dimensions) {
                            if (!d.is_unpacked) {
                                vdm.dimensions.push_back(d);
                            }
                        }
                    } else if (base_t == "int" || base_t == "integer") {
                        vdm.dimensions.push_back(ArrayDimInfo{31, 0, false});
                    } else if (base_t == "byte") {
                        vdm.dimensions.push_back(ArrayDimInfo{7, 0, false});
                    } else if (base_t == "shortint") {
                        vdm.dimensions.push_back(ArrayDimInfo{15, 0, false});
                    } else if (base_t == "longint") {
                        vdm.dimensions.push_back(ArrayDimInfo{63, 0, false});
                    }
                }
                if (!vdm.dimensions.empty()) {
                    vdm.is_vector_or_array = true;
                }

                global_type_dimensions[tname] = vdm;
                if (current_parsing_package) {
                    global_type_dimensions[current_parsing_package->name + "::" + tname] = vdm;
                    current_parsing_package->type_dimensions[tname] = vdm;
                } else if (current_parsing_module) {
                    current_parsing_module->type_dimensions[tname] = vdm;
                }
                if (is_unpacked_arr) {
                    td->is_unpacked_array = true;
                    td->elem_type = base_t;
                    td->elem_width = width;
                    td->elem_signed = is_sgn;
                    td->array_size = total_elements;
                    td->arr_start_idx = arr_start;
                    td->arr_end_idx = arr_end;
                    td->unpacked_dims = std::move(unpacked_dims);

                    UnpackedArrayTypedef uinfo{tname, base_t, width, is_sgn, total_elements, arr_start, arr_end};
                    unpacked_array_typedef_map[tname] = uinfo;
                    global_unpacked_array_typedefs[tname] = uinfo;
                    if (current_parsing_package) {
                        current_parsing_package->unpacked_array_typedefs[tname] = uinfo;
                    } else if (current_parsing_module) {
                        current_parsing_module->unpacked_array_typedefs[tname] = uinfo;
                    }
                } else {
                    simple_typedef_map[tname] = {tname, base_t, width, is_sgn};
                    if (current_parsing_package) {
                        current_parsing_package->simple_typedefs[tname] = {tname, base_t, width, is_sgn};
                    } else if (current_parsing_module) {
                        current_parsing_module->simple_typedefs[tname] = {tname, base_t, width, is_sgn};
                    }
                    if (packed_dims.empty()) {
                        if (struct_def) {
                            struct_typedef_map[tname] = struct_def;
                            if (current_parsing_package) current_parsing_package->struct_typedefs[tname] = struct_def;
                            else if (current_parsing_module) current_parsing_module->struct_typedefs[tname] = struct_def;
                        }
                        if (enum_def) {
                            typedef_map[tname] = enum_def;
                            if (current_parsing_package) current_parsing_package->enum_typedefs[tname] = enum_def;
                            else if (current_parsing_module) current_parsing_module->enum_typedefs[tname] = enum_def;
                        }
                    }
                    resolved_typedefs.insert(tname);
                }
                return td;
            }
        }
    }

    if (current.type == TokenType::TOKEN_STRUCT || current.type == TokenType::TOKEN_UNION) {
        auto struct_node = parse_struct_or_union_body();
        std::string type_name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected typedef type identifier");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after typedef declaration");
        resolved_typedefs.insert(type_name);

        if (is_forward_class(type_name)) {
            throw std::runtime_error("[CAS-DTINPCIL] Identifier '" + type_name + "' was declared as 'typedef class' but subsequently defined as a non-class type per IEEE 1800-2023 §6.18");
        }

        struct_typedef_map[type_name] = struct_node;
        if (struct_node->is_packed && struct_node->total_width > 0) {
            VariableDimMetadata svdm;
            svdm.name = type_name;
            svdm.dimensions.push_back(ArrayDimInfo{(int64_t)struct_node->total_width - 1, 0, false});
            svdm.unpacked_dims = 0;
            svdm.is_vector_or_array = true;
            global_type_dimensions[type_name] = svdm;
            if (current_parsing_package) {
                global_type_dimensions[current_parsing_package->name + "::" + type_name] = svdm;
                current_parsing_package->type_dimensions[type_name] = svdm;
            } else if (current_parsing_module) {
                current_parsing_module->type_dimensions[type_name] = svdm;
            }
        }
        return std::make_unique<TypedefNode>(type_name, struct_node);
    }

    expect(TokenType::TOKEN_ENUM, "Expected 'enum' or 'struct' in typedef");

    uint32_t width = 32;
    bool is_signed = true;
    std::string base_type_name = "int";

    // Optional base type
    if (current.type == TokenType::TOKEN_IDENTIFIER && (is_typedef(current.text) || is_simple_typedef(current.text) || find_type_dim_metadata(current.text) != nullptr)) {
        base_type_name = current.text;
        advance();
        while (match(TokenType::TOKEN_COLON_COLON)) {
            base_type_name += "::" + current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '::' in enum base type");
        }
        SimpleTypedef sm_def;
        if (get_simple_typedef(base_type_name, sm_def)) {
            width = sm_def.width;
            is_signed = sm_def.is_signed;
        } else if (auto st = get_struct_typedef(base_type_name)) {
            width = st->total_width;
            is_signed = st->is_signed;
        } else if (auto et = get_enum_typedef(base_type_name)) {
            width = et->base_width;
            is_signed = et->is_signed;
        } else if (const auto* vdm = find_type_dim_metadata(base_type_name)) {
            uint32_t bw = 1;
            for (const auto& d : vdm->dimensions) {
                if (!d.is_unpacked) bw *= (uint32_t)d.size();
            }
            if (bw > 1) width = bw;
            is_signed = false;
        }
    } else if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG ||
        current.type == TokenType::TOKEN_WIRE  || current.type == TokenType::TOKEN_INTEGER ||
        current.type == TokenType::TOKEN_BIT   || current.type == TokenType::TOKEN_SIGNED ||
        current.type == TokenType::TOKEN_UNSIGNED ||
        (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "bit" || current.text == "signed" || current.text == "unsigned"))) {

        if (current.type == TokenType::TOKEN_SIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "signed")) {
            is_signed = true;
            advance();
        } else if (current.type == TokenType::TOKEN_UNSIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned")) {
            is_signed = false;
            advance();
        } else {
            is_signed = false;
        }

        if (current.type == TokenType::TOKEN_INTEGER) {
            width = 32;
            is_signed = true;
            base_type_name = "integer";
            advance();
        } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "int") {
            width = 32;
            is_signed = true;
            base_type_name = "int";
            advance();
        } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "byte") {
            width = 8;
            is_signed = true;
            base_type_name = "byte";
            advance();
        } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "shortint") {
            width = 16;
            is_signed = true;
            base_type_name = "shortint";
            advance();
        } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "longint") {
            width = 64;
            is_signed = true;
            base_type_name = "longint";
            advance();
        } else if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_BIT || current.type == TokenType::TOKEN_REG) {
            base_type_name = current.text;
            width = 1;
            advance();
        }

        if (current.type == TokenType::TOKEN_SIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "signed")) {
            is_signed = true;
            advance();
        } else if (current.type == TokenType::TOKEN_UNSIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned")) {
            is_signed = false;
            advance();
        }

        if (match(TokenType::TOKEN_LBRACKET)) {
            auto msb_e = parse_expression();
            expect(TokenType::TOKEN_COLON, "Expected ':' in base type range");
            auto lsb_e = parse_expression();
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in base type range");
            if (auto nm = dynamic_cast<NumberNode*>(msb_e.get())) {
                if (auto nl = dynamic_cast<NumberNode*>(lsb_e.get())) {
                    width = (nm->aval >= nl->aval) ? (uint32_t)(nm->aval - nl->aval + 1) : (uint32_t)(nl->aval - nm->aval + 1);
                }
            }
        }
    } else if (match(TokenType::TOKEN_LBRACKET)) {
        auto msb_e = parse_expression();
        expect(TokenType::TOKEN_COLON, "Expected ':' in base type range");
        auto lsb_e = parse_expression();
        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in base type range");
        if (auto nm = dynamic_cast<NumberNode*>(msb_e.get())) {
            if (auto nl = dynamic_cast<NumberNode*>(lsb_e.get())) {
                width = (nm->aval >= nl->aval) ? (uint32_t)(nm->aval - nl->aval + 1) : (uint32_t)(nl->aval - nm->aval + 1);
            }
        }
        is_signed = false;
        base_type_name = "logic";
    }

    expect(TokenType::TOKEN_LBRACE, "Expected '{' in enum declaration");
    auto enum_node = std::make_shared<EnumTypeNode>();
    enum_node->base_width = width;
    enum_node->is_signed = is_signed;
    enum_node->base_type_name = base_type_name;

    uint64_t current_val = 0;
    uint64_t current_bval = 0;
    bool prev_had_xz = false;
    while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
        std::string mname = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected enum member identifier");

        bool curr_had_xz = false;
        if (match(TokenType::TOKEN_EQUAL)) {
            auto val_expr = parse_expression();
            if (auto num = dynamic_cast<NumberNode*>(val_expr.get())) {
                current_val = num->aval;
                current_bval = num->bval;
                if (num->explicit_size && num->width != width) {
                    throw std::runtime_error("If integer value expression is a sized literal constant, it shall be an error if the size is different from the enum base type per IEEE 1800-2023 6.19");
                }
                if (num->bval != 0) {
                    curr_had_xz = true;
                    if (base_type_name == "bit" || base_type_name == "byte" || base_type_name == "int" || base_type_name == "shortint" || base_type_name == "longint") {
                        throw std::runtime_error("An enumerated name with x or z assignments assigned to an enum with 2-state declaration shall be a syntax error per IEEE 1800-2023 6.19");
                    }
                }
            } else {
                if (expr_has_xz(val_expr.get())) {
                    curr_had_xz = true;
                    if (base_type_name == "bit" || base_type_name == "byte" || base_type_name == "int" || base_type_name == "shortint" || base_type_name == "longint") {
                        throw std::runtime_error("An enumerated name with x or z assignments assigned to an enum with 2-state declaration shall be a syntax error per IEEE 1800-2023 6.19");
                    }
                }
                try {
                    current_val = eval_const_expr(val_expr.get());
                    current_bval = 0;
                } catch (...) {}
            }
        } else {
            current_bval = 0;
            if (prev_had_xz) {
                throw std::runtime_error("An unassigned enumerated name that follows an enum name with x or z assignments shall be a syntax error per IEEE 1800-2023 6.19");
            }
        }
        prev_had_xz = curr_had_xz;

        LogicVal mval(width, current_val, current_bval);
        enum_node->members.push_back({mname, mval});
        current_val++;
        current_bval = 0;

        if (match(TokenType::TOKEN_COMMA)) continue;
        break;
    }
    expect(TokenType::TOKEN_RBRACE, "Expected '}' closing enum member list");

    std::string type_name = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected typedef type identifier");
    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after typedef declaration");
    resolved_typedefs.insert(type_name);

    if (is_forward_class(type_name)) {
        throw std::runtime_error("[CAS-DTINPCIL] Identifier '" + type_name + "' was declared as 'typedef class' but subsequently defined as a non-class type per IEEE 1800-2023 §6.18");
    }

    typedef_map[type_name] = enum_node;
    if (current_parsing_package) {
        current_parsing_package->enum_typedefs[type_name] = enum_node;
        for (const auto& m : enum_node->members) {
            current_parsing_package->enum_constant_map[m.first] = m.second;
        }
    } else if (current_parsing_module) {
        current_parsing_module->enum_typedefs[type_name] = enum_node;
    }

    VariableDimMetadata edim;
    edim.name = type_name;
    edim.dimensions.push_back({(int64_t)width - 1, 0, false, false, false});
    edim.is_vector_or_array = true;
    edim.unpacked_dims = 0;
    global_type_dimensions[type_name] = edim;
    if (current_parsing_package) {
        current_parsing_package->type_dimensions[type_name] = edim;
        global_type_dimensions[current_parsing_package->name + "::" + type_name] = edim;
    }
    return std::make_unique<TypedefNode>(type_name, enum_node);
}

std::unique_ptr<ClassNode> Parser::parse_class(bool is_interface_class, bool is_abstract) {
    expect(TokenType::TOKEN_CLASS, "Expected 'class'");
    std::string class_name = current.text;
    if (current.type == TokenType::TOKEN_IDENTIFIER || current.type == TokenType::TOKEN_SEMAPHORE || current.type == TokenType::TOKEN_MAILBOX) {
        advance();
    } else {
        expect(TokenType::TOKEN_IDENTIFIER, "Expected class name");
    }

    auto cls = std::make_unique<ClassNode>(class_name);
    cls->defined_in_file = current.filename;
    cls->compilation_unit_id = current_compilation_unit_id;
    cls->is_interface_class = is_interface_class;
    cls->is_abstract = is_abstract;
    cls->time_unit = !global_timeunit_val.empty() ? global_timeunit_val : Lexer::global_time_unit_str;
    cls->time_precision = !global_timeprec_val.empty() ? global_timeprec_val : Lexer::global_time_precision_str;
    std::vector<std::string> local_class_typedef_names;
    ClassNode* prev_cls = current_parsing_class;
    current_parsing_class = cls.get();

    if (match(TokenType::TOKEN_HASH)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after '#' in class parameter list");
        cls->is_parameterized = true;
        while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
            if (current.type == TokenType::TOKEN_PARAMETER || current.type == TokenType::TOKEN_LOCALPARAM) {
                advance();
            }
            bool is_type_param = false;
            if (current.type == TokenType::TOKEN_TYPE || current.text == "type") {
                is_type_param = true;
                advance();
            }
            if (current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_LOGIC ||
                current.type == TokenType::TOKEN_BIT || current.type == TokenType::TOKEN_REG ||
                current.type == TokenType::TOKEN_WIRE ||
                (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint" || is_type_identifier(current.text)))) {
                advance();
            }
            std::string pname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter identifier");
            std::unique_ptr<ExpressionNode> pexpr = nullptr;
            if (match(TokenType::TOKEN_EQUAL)) {
                if (is_type_param) {
                    if (current.type == TokenType::TOKEN_VIRTUAL) {
                        advance(); // consume 'virtual'
                        if (current.type == TokenType::TOKEN_INTERFACE) advance();
                        std::string virt_type = current.text;
                        if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
                        while (match(TokenType::TOKEN_COLON_COLON)) {
                            virt_type += "::" + current.text;
                            if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
                        }
                        pexpr = std::make_unique<IdentifierNode>("virtual " + virt_type);
                    } else if (current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_LOGIC ||
                        current.type == TokenType::TOKEN_BIT || current.type == TokenType::TOKEN_REG ||
                        current.type == TokenType::TOKEN_WIRE ||
                        (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint" || is_type_identifier(current.text)))) {
                        advance();
                    } else {
                        pexpr = parse_expression();
                    }
                } else {
                    pexpr = parse_expression();
                }
            }
            cls->parameters.push_back(ParameterDef(pname, std::move(pexpr)));
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing class parameter list");
    }

    if (is_interface_class) {
        if (match(TokenType::TOKEN_EXTENDS)) {
            while (true) {
                std::string parent_iface = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected interface class name after 'extends'");
                while (match(TokenType::TOKEN_COLON_COLON)) {
                    parent_iface += "::" + current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected interface class name after '::'");
                }
                if (match(TokenType::TOKEN_HASH)) {
                    parent_iface += "#(";
                    expect(TokenType::TOKEN_LPAREN, "Expected '(' after '#' in extended interface parameter list");
                    int pdepth = 1;
                    while (pdepth > 0 && current.type != TokenType::TOKEN_EOF) {
                        if (current.type == TokenType::TOKEN_LPAREN) pdepth++;
                        else if (current.type == TokenType::TOKEN_RPAREN) {
                            pdepth--;
                            if (pdepth == 0) {
                                advance();
                                break;
                            }
                        }
                        parent_iface += current.text;
                        advance();
                    }
                    parent_iface += ")";
                }
                cls->extended_interfaces.push_back(parent_iface);
                std::string base_parent = parent_iface;
                auto hash_pos = base_parent.find('#');
                if (hash_pos != std::string::npos) base_parent = base_parent.substr(0, hash_pos);
                ClassNode* p_cls = find_class(base_parent);
                if (p_cls) {
                    for (const auto& kv : p_cls->simple_typedefs) {
                        cls->simple_typedefs[kv.first] = kv.second;
                        simple_typedef_map[kv.first] = kv.second;
                        local_class_typedef_names.push_back(kv.first);
                    }
                }
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
        }
        if (match(TokenType::TOKEN_IMPLEMENTS)) {
            throw std::runtime_error("[CAS-DTINPCIL] Interface class '" + class_name + "' cannot use 'implements' clause per IEEE 1800-2023 section 8.26");
        }
    } else {
        std::string base_class_name = "";
        if (match(TokenType::TOKEN_EXTENDS)) {
            base_class_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected base class name after 'extends'");
            while (match(TokenType::TOKEN_COLON_COLON)) {
                base_class_name += "::" + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected class name after '::'");
            }
            cls->base_class_name = base_class_name;
            ClassNode* b_cls = find_class(base_class_name);
            if (b_cls) {
                for (const auto& kv : b_cls->simple_typedefs) {
                    cls->simple_typedefs[kv.first] = kv.second;
                    simple_typedef_map[kv.first] = kv.second;
                    local_class_typedef_names.push_back(kv.first);
                }
            }
            if (match(TokenType::TOKEN_HASH)) {
                expect(TokenType::TOKEN_LPAREN, "Expected '(' after '#' in base class parameter list");
                int pdepth = 1;
                while (pdepth > 0 && current.type != TokenType::TOKEN_EOF) {
                    if (current.type == TokenType::TOKEN_LPAREN) pdepth++;
                    else if (current.type == TokenType::TOKEN_RPAREN) pdepth--;
                    advance();
                }
            }
            if (match(TokenType::TOKEN_LPAREN)) {
                if (current.type != TokenType::TOKEN_RPAREN) {
                    while (true) {
                        cls->base_constructor_args.push_back(parse_expression());
                        if (match(TokenType::TOKEN_COMMA)) continue;
                        break;
                    }
                }
                expect(TokenType::TOKEN_RPAREN, "Expected ')' closing base class constructor arguments");
            }
        }
        if (match(TokenType::TOKEN_IMPLEMENTS)) {
            while (true) {
                std::string iface_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected interface class name after 'implements'");
                while (match(TokenType::TOKEN_COLON_COLON)) {
                    iface_name += "::" + current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected interface class name after '::'");
                }
                if (is_forward_interface_class(iface_name) && !find_class(iface_name)) {
                    throw std::runtime_error("Implementing forward-declared interface class '" + iface_name + "' before its definition is illegal per IEEE 1800-2023 8.26.4");
                }
                if (match(TokenType::TOKEN_HASH)) {
                    expect(TokenType::TOKEN_LPAREN, "Expected '(' after '#' in implemented interface parameter list");
                    int pdepth = 1;
                    while (pdepth > 0 && current.type != TokenType::TOKEN_EOF) {
                        if (current.type == TokenType::TOKEN_LPAREN) pdepth++;
                        else if (current.type == TokenType::TOKEN_RPAREN) pdepth--;
                        advance();
                    }
                }
                cls->implemented_interfaces.push_back(iface_name);
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
        }
    }
    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after class name");

    while (current.type != TokenType::TOKEN_ENDCLASS && current.type != TokenType::TOKEN_EOF) {
        if (current.type == TokenType::TOKEN_CLASS || (current.type == TokenType::TOKEN_INTERFACE && peek_token().type == TokenType::TOKEN_CLASS)) {
            bool is_nested_iface = false;
            if (current.type == TokenType::TOKEN_INTERFACE) {
                is_nested_iface = true;
                advance();
            }
            auto nested_cls = parse_class(is_nested_iface);
            if (nested_cls) {
                std::shared_ptr<ClassNode> snested = std::move(nested_cls);
                class_map[snested->name] = snested;
                class_map[class_name + "::" + snested->name] = snested;
                global_classes.push_back(snested);
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_TYPEDEF) {
            auto td = parse_typedef();
            if (td) {
                cls->simple_typedefs[td->type_name] = {td->type_name, td->base_type, td->width, td->is_signed};
                local_class_typedef_names.push_back(td->type_name);
                simple_typedef_map[class_name + "::" + td->type_name] = {td->type_name, td->base_type, td->width, td->is_signed};
                if (current_parsing_module) {
                    current_parsing_module->simple_typedefs[class_name + "::" + td->type_name] = {td->type_name, td->base_type, td->width, td->is_signed};
                    current_parsing_module->typedefs.push_back(std::move(td));
                } else {
                    global_typedefs.push_back(std::move(td));
                }
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_PARAMETER || current.type == TokenType::TOKEN_LOCALPARAM) {
            bool is_local = (current.type == TokenType::TOKEN_LOCALPARAM);
            advance();
            bool is_type_param = false;
            if (current.type == TokenType::TOKEN_TYPE || current.text == "type") {
                is_type_param = true;
                advance();
            }
            if (is_type_param) {
                while (true) {
                    std::string pname = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter identifier");
                    expect(TokenType::TOKEN_EQUAL, "Expected '=' in type parameter assignment");
                    std::string def_type = current.text;
                    if (current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_LOGIC ||
                        current.type == TokenType::TOKEN_BIT || current.type == TokenType::TOKEN_REG ||
                        current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_IDENTIFIER) {
                        advance();
                        while (match(TokenType::TOKEN_COLON_COLON)) {
                            def_type += "::" + current.text;
                            if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
                        }
                        if (match(TokenType::TOKEN_HASH)) {
                            expect(TokenType::TOKEN_LPAREN, "Expected '('");
                            int pdepth = 1;
                            while (pdepth > 0 && current.type != TokenType::TOKEN_EOF) {
                                if (current.type == TokenType::TOKEN_LPAREN) pdepth++;
                                else if (current.type == TokenType::TOKEN_RPAREN) pdepth--;
                                advance();
                            }
                        }
                    }
                    cls->parameters.push_back(ParameterDef(pname, std::make_unique<IdentifierNode>(def_type), is_local, "type"));
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after type parameter declaration");
                continue;
            }
            bool has_type = false;
            std::string ptype = "";
            while (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
                   current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_INTEGER ||
                   current.type == TokenType::TOKEN_SIGNED || current.type == TokenType::TOKEN_UNSIGNED || 
                   current.type == TokenType::TOKEN_BIT ||
                   (!has_type && is_type_identifier(current.text)) ||
                   (!has_type && current.type == TokenType::TOKEN_IDENTIFIER && (peek_token().type == TokenType::TOKEN_COLON_COLON || peek_token().type == TokenType::TOKEN_IDENTIFIER))) {
                has_type = true;
                if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON_COLON) {
                    ptype = current.text;
                    advance();
                    ptype += current.text;
                    advance();
                    ptype += current.text;
                    advance();
                } else {
                    if (ptype.empty()) ptype = current.text;
                    advance();
                }
                while (current.type == TokenType::TOKEN_LBRACKET) {
                    advance();
                    parse_expression();
                    if (match(TokenType::TOKEN_COLON)) {
                        parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
                }
            }
            while (match(TokenType::TOKEN_LBRACKET)) {
                parse_expression();
                if (match(TokenType::TOKEN_COLON)) {
                    parse_expression();
                }
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
            }
            while (true) {
                std::string pname = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter identifier");
                while (match(TokenType::TOKEN_LBRACKET)) {
                    parse_expression();
                    if (match(TokenType::TOKEN_COLON)) {
                        parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
                }
                expect(TokenType::TOKEN_EQUAL, "Expected '=' in parameter assignment");
                auto val_expr = parse_expression();
                cls->parameters.push_back(ParameterDef(pname, std::move(val_expr), is_local, ptype));
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after parameter declaration");
            continue;
        }
        if (current.type == TokenType::TOKEN_SPECIFY) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: 'specify' block is not permitted inside class per IEEE 1800-2023 section 32");
        }
        if (current.type == TokenType::TOKEN_SYS_SETUP || current.type == TokenType::TOKEN_SYS_HOLD ||
            current.type == TokenType::TOKEN_SYS_WIDTH || current.type == TokenType::TOKEN_SYS_RECOVERY) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: timing check system task '" + current.text + "' is not permitted inside class per IEEE 1800-2023 section 31.8 & 32.7");
        }
        if (current.type == TokenType::TOKEN_FATAL || current.type == TokenType::TOKEN_ERROR ||
            current.type == TokenType::TOKEN_WARNING || current.type == TokenType::TOKEN_INFO) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: severity system task '" + current.text + "' is not permitted directly in class body per IEEE 1800-2023 section 20.10");
        }
        bool is_pure = false;
        bool is_virt = false;
        bool is_stat = false;
        bool is_ext = false;

        while (true) {
            if (match(TokenType::TOKEN_EXTERN)) {
                is_ext = true;
            } else if (match(TokenType::TOKEN_PURE)) {
                is_pure = true;
            } else if (match(TokenType::TOKEN_VIRTUAL)) {
                is_virt = true;
            } else if (match(TokenType::TOKEN_STATIC)) {
                is_stat = true;
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "protected" || current.text == "local" || current.text == "const")) {
                advance();
            } else {
                break;
            }
        }

        if (current.type != TokenType::TOKEN_CONSTRAINT && is_pure && !is_virt) {
            throw std::runtime_error("[CAS-DTINPCIL] Expected 'virtual' after 'pure' per IEEE 1800-2023 section 8.26");
        }
        if (current.type != TokenType::TOKEN_CONSTRAINT && is_ext && is_pure) {
            throw std::runtime_error("[CAS-DTINPCIL] Method cannot be declared both 'pure' and 'extern' per IEEE 1800-2023 section 8.24");
        }

        if (current.type == TokenType::TOKEN_FUNCTION) {
            advance();
            if (is_interface_class && (!is_pure || !is_virt)) {
                throw std::runtime_error("[CAS-DTINPCIL] Interface class '" + class_name + "' can only contain pure virtual method declarations per IEEE 1800-2023 section 8.26");
            }
            if (is_pure && !is_interface_class && !cls->is_abstract) {
                throw std::runtime_error("[CAS-DTINPCIL] Pure virtual method can only be declared inside a virtual class or interface class per IEEE 1800-2023 section 8.21");
            }
            auto fn = parse_function(is_pure, is_ext);
            if (fn) {
                if (is_interface_class && fn->name == "new") {
                    throw std::runtime_error("[CAS-DTINPCIL] Interface class '" + class_name + "' cannot define constructor 'new' per IEEE 1800-2023 section 8.26");
                }
                fn->is_pure = is_pure;
                fn->is_extern = is_ext;
                fn->has_body = !is_pure && !is_ext;
                fn->is_virtual = is_virt;
                fn->is_static = is_stat || fn->is_static;
                if (!fn->is_static) fn->is_automatic = true;
                cls->add_function(std::move(fn));
            }
            continue;
        } else if (current.type == TokenType::TOKEN_TASK) {
            advance();
            if (is_interface_class && (!is_pure || !is_virt)) {
                throw std::runtime_error("[CAS-DTINPCIL] Interface class '" + class_name + "' can only contain pure virtual method declarations per IEEE 1800-2023 section 8.26");
            }
            if (is_pure && !is_interface_class && !cls->is_abstract) {
                throw std::runtime_error("[CAS-DTINPCIL] Pure virtual method can only be declared inside a virtual class or interface class per IEEE 1800-2023 section 8.21");
            }
            auto t = parse_task(is_pure, is_ext);
            if (t) {
                t->is_pure = is_pure;
                t->is_extern = is_ext;
                t->has_body = !is_pure && !is_ext;
                t->is_virtual = is_virt;
                t->is_static = is_stat || t->is_static;
                if (!t->is_static) t->is_automatic = true;
                cls->add_task(std::move(t));
            }
            continue;
        }

        if (is_interface_class) {
            throw std::runtime_error("[CAS-DTINPCIL] Interface class '" + class_name + "' can only contain pure virtual method declarations per IEEE 1800-2023 section 8.26");
        }
        if (current.type == TokenType::TOKEN_CONSTRAINT) {
            advance();
            if (match(TokenType::TOKEN_STATIC)) {
                // static constraint
            }
            std::string cname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected constraint name");
            if (match(TokenType::TOKEN_SEMICOLON)) {
                // External / pure constraint prototype (§18.5.1)
                auto cb = std::make_unique<ConstraintBlockNode>(cname, std::vector<std::unique_ptr<ExpressionNode>>());
                cb->is_extern = is_ext;
                cb->is_pure = is_pure;
                cb->has_body = false;
                cls->add_constraint(std::move(cb));
                continue;
            }
            expect(TokenType::TOKEN_LBRACE, "Expected '{' after constraint name");
            std::vector<std::unique_ptr<ExpressionNode>> exprs;
            std::function<void(std::vector<std::unique_ptr<ExpressionNode>>&)> parse_constraint_item =
                [&](std::vector<std::unique_ptr<ExpressionNode>>& out_exprs) {
                    if (match(TokenType::TOKEN_SEMICOLON)) return;
                    if (match(TokenType::TOKEN_SOLVE)) {
                        std::vector<std::string> before_vars;
                        while (current.type != TokenType::TOKEN_BEFORE && current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_EOF) {
                            if (current.type == TokenType::TOKEN_IDENTIFIER) before_vars.push_back(current.text);
                            advance();
                        }
                        std::vector<std::string> after_vars;
                        if (match(TokenType::TOKEN_BEFORE)) {
                            while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_EOF) {
                                if (current.type == TokenType::TOKEN_IDENTIFIER) after_vars.push_back(current.text);
                                advance();
                            }
                        }
                        match(TokenType::TOKEN_SEMICOLON);
                        for (const auto& v : before_vars) {
                            if (cls->is_randc_property(v)) throw std::runtime_error("randc variable '" + v + "' cannot be specified in solve...before constraint per IEEE 1800-2023 18.5.10");
                        }
                        for (const auto& v : after_vars) {
                            if (cls->is_randc_property(v)) throw std::runtime_error("randc variable '" + v + "' cannot be specified in solve...before constraint per IEEE 1800-2023 18.5.10");
                        }
                        return;
                    }
                    if (current.type == TokenType::TOKEN_DISABLE) {
                        advance();
                        if (match(TokenType::TOKEN_UNIQUE)) {
                        expect(TokenType::TOKEN_LBRACE, "Expected '{' after 'unique'");
                        while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                            parse_expression();
                            if (match(TokenType::TOKEN_COMMA)) continue;
                        }
                        expect(TokenType::TOKEN_RBRACE, "Expected '}' closing unique constraint list");
                        match(TokenType::TOKEN_SEMICOLON);
                        return;
                    }
                    if (match(TokenType::TOKEN_SOFT)) {
                            parse_expression();
                        }
                        match(TokenType::TOKEN_SEMICOLON);
                        return;
                    }
                    if (match(TokenType::TOKEN_UNIQUE)) {
                        expect(TokenType::TOKEN_LBRACE, "Expected '{' after 'unique'");
                        while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                            parse_expression();
                            if (match(TokenType::TOKEN_COMMA)) continue;
                        }
                        expect(TokenType::TOKEN_RBRACE, "Expected '}' closing unique constraint list");
                        match(TokenType::TOKEN_SEMICOLON);
                        return;
                    }
                    bool is_soft_c = false;
                    if (match(TokenType::TOKEN_SOFT)) {
                        is_soft_c = true;
                    }
                    if (match(TokenType::TOKEN_FOREACH)) {
                        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'foreach' in constraint");
                        while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                            advance();
                        }
                        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing foreach in constraint");
                        if (match(TokenType::TOKEN_LBRACE)) {
                            while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                                parse_constraint_item(out_exprs);
                            }
                            expect(TokenType::TOKEN_RBRACE, "Expected '}' closing foreach constraint block");
                        } else {
                            parse_constraint_item(out_exprs);
                        }
                        return;
                    }
                    if (current.type == TokenType::TOKEN_IF) {
                        advance(); // consume 'if'
                        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'if' in constraint");
                        auto cond = parse_expression();
                        expect(TokenType::TOKEN_RPAREN, "Expected ')' after condition in constraint");
                        std::vector<std::unique_ptr<ExpressionNode>> then_exprs;
                        if (match(TokenType::TOKEN_LBRACE)) {
                            while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                                parse_constraint_item(then_exprs);
                            }
                            expect(TokenType::TOKEN_RBRACE, "Expected '}' closing constraint if-block");
                        } else {
                            parse_constraint_item(then_exprs);
                        }
                        std::vector<std::unique_ptr<ExpressionNode>> else_exprs;
                        if (match(TokenType::TOKEN_ELSE)) {
                            if (match(TokenType::TOKEN_LBRACE)) {
                                while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                                    parse_constraint_item(else_exprs);
                                }
                                expect(TokenType::TOKEN_RBRACE, "Expected '}' closing constraint else-block");
                            } else {
                                parse_constraint_item(else_exprs);
                            }
                        }
                        for (auto& t : then_exprs) {
                            auto true_node = std::make_unique<NumberNode>(1, 1, 0);
                            auto tern = std::make_unique<TernaryOpNode>(cond ? cond->clone() : nullptr, std::move(t), std::move(true_node));
                            out_exprs.push_back(std::move(tern));
                        }
                        for (auto& e : else_exprs) {
                            auto true_node = std::make_unique<NumberNode>(1, 1, 0);
                            auto tern = std::make_unique<TernaryOpNode>(cond ? cond->clone() : nullptr, std::move(true_node), std::move(e));
                            out_exprs.push_back(std::move(tern));
                        }
                        return;
                    }

                    auto expr = parse_expression();
                    if (match(TokenType::TOKEN_TRIGGER)) {
                        if (match(TokenType::TOKEN_LBRACE)) {
                            std::vector<std::unique_ptr<ExpressionNode>> imp_exprs;
                            while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                                parse_constraint_item(imp_exprs);
                            }
                            expect(TokenType::TOKEN_RBRACE, "Expected '}' closing implication block");
                            for (auto& ie : imp_exprs) {
                                auto true_node = std::make_unique<NumberNode>(1, 1, 0);
                                auto tern = std::make_unique<TernaryOpNode>(expr ? expr->clone() : nullptr, std::move(ie), std::move(true_node));
                                out_exprs.push_back(std::move(tern));
                            }
                            return;
                        } else {
                            std::vector<std::unique_ptr<ExpressionNode>> imp_exprs;
                            parse_constraint_item(imp_exprs);
                            for (auto& ie : imp_exprs) {
                                auto true_node = std::make_unique<NumberNode>(1, 1, 0);
                                auto tern = std::make_unique<TernaryOpNode>(expr ? expr->clone() : nullptr, std::move(ie), std::move(true_node));
                                out_exprs.push_back(std::move(tern));
                            }
                            return;
                        }
                    }

                    if (match(TokenType::TOKEN_DIST)) {
                        if (auto id = dynamic_cast<IdentifierNode*>(expr.get())) {
                            if (cls->is_randc_property(id->name)) {
                                throw std::runtime_error("randc variable '" + id->name + "' cannot be used in dist expression per IEEE 1800-2023 18.5.4");
                            }
                        }
                        expect(TokenType::TOKEN_LBRACE, "Expected '{' after 'dist'");
                        while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                            if (match(TokenType::TOKEN_LBRACKET)) {
                                parse_expression();
                                expect(TokenType::TOKEN_COLON, "Expected ':' in dist range");
                                parse_expression();
                                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dist range");
                            } else {
                                parse_expression();
                            }
                            if (match(TokenType::TOKEN_COLON_EQ) || match(TokenType::TOKEN_COLON_DIV)) {
                                parse_expression();
                            }
                            if (match(TokenType::TOKEN_COMMA)) continue;
                        }
                        expect(TokenType::TOKEN_RBRACE, "Expected '}' after dist list");
                    }

                    if (is_soft_c && expr) {
                        std::function<void(ExpressionNode*)> check_soft_randc = [&](ExpressionNode* e) {
                            if (!e) return;
                            if (auto id = dynamic_cast<IdentifierNode*>(e)) {
                                if (cls->is_randc_property(id->name)) {
                                    throw std::runtime_error("soft constraint cannot be applied to randc variable '" + id->name + "' per IEEE 1800-2023 18.5.14");
                                }
                            } else if (auto bin = dynamic_cast<BinaryOpNode*>(e)) {
                                check_soft_randc(bin->lhs.get());
                                check_soft_randc(bin->rhs.get());
                            } else if (auto un = dynamic_cast<UnaryOpNode*>(e)) {
                                check_soft_randc(un->operand.get());
                            }
                        };
                        check_soft_randc(expr.get());
                    }

                    if (expr) {
                        out_exprs.push_back(std::move(expr));
                    }
                    match(TokenType::TOKEN_SEMICOLON);
                };

            while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                parse_constraint_item(exprs);
            }
            expect(TokenType::TOKEN_RBRACE, "Expected '}' closing constraint block");
            cls->add_constraint(std::make_unique<ConstraintBlockNode>(cname, std::move(exprs)));
            continue;
        } else if (is_virt && current.type != TokenType::TOKEN_FUNCTION && current.type != TokenType::TOKEN_TASK) {
            std::string iface_name = "";
            std::string modport_name = "";
            if (current.type == TokenType::TOKEN_IDENTIFIER || is_interface_type(current.text)) {
                iface_name = current.text;
                advance();
                if (match(TokenType::TOKEN_DOT)) {
                    modport_name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected modport name after '.'");
                }
            }
            while (true) {
                std::string var_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected virtual interface variable name");
                std::unique_ptr<ExpressionNode> init_expr = nullptr;
                if (match(TokenType::TOKEN_EQUAL)) {
                    init_expr = parse_expression();
                }
                ClassMemberVar mvar;
                mvar.name = var_name;
                mvar.width = 64;
                mvar.is_signed = false;
                mvar.is_virtual_interface = true;
                mvar.interface_name = iface_name;
                mvar.modport_name = modport_name;
                mvar.default_expr = std::move(init_expr);
                cls->add_member_var(std::move(mvar));
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after virtual interface declaration");
            continue;
        } else if (current.type == TokenType::TOKEN_ENUM) {
            advance(); // consume 'enum'
            uint32_t e_width = 32;
            bool e_signed = true;
            std::string e_base = "int";
            if (current.type == TokenType::TOKEN_SIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "signed")) {
                e_signed = true; advance();
            } else if (current.type == TokenType::TOKEN_UNSIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned")) {
                e_signed = false; advance();
            }

            if (current.type == TokenType::TOKEN_INTEGER) {
                e_width = 32; e_base = "integer"; advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "int") {
                e_width = 32; e_base = "int"; advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "byte") {
                e_width = 8; e_base = "byte"; advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "shortint") {
                e_width = 16; e_base = "shortint"; advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "longint") {
                e_width = 64; e_base = "longint"; advance();
            } else if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_BIT || current.type == TokenType::TOKEN_REG) {
                e_base = current.text; e_width = 1; e_signed = false; advance();
            }

            if (current.type == TokenType::TOKEN_SIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "signed")) {
                e_signed = true; advance();
            } else if (current.type == TokenType::TOKEN_UNSIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned")) {
                e_signed = false; advance();
            }

            if (match(TokenType::TOKEN_LBRACKET)) {
                auto msb_e = parse_expression();
                expect(TokenType::TOKEN_COLON, "Expected ':' in range");
                auto lsb_e = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in range");
                try {
                    uint64_t m = eval_const_expr(msb_e.get());
                    uint64_t l = eval_const_expr(lsb_e.get());
                    e_width = (m >= l) ? (m - l + 1) : (l - m + 1);
                } catch (...) {}
            }

            expect(TokenType::TOKEN_LBRACE, "Expected '{' in enum declaration");
            auto enum_node = std::make_shared<EnumTypeNode>();
            enum_node->base_width = e_width;
            enum_node->is_signed = e_signed;
            enum_node->base_type_name = e_base;
            uint64_t current_val = 0;
            uint64_t current_bval = 0;

            while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                std::string member_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected enum member name");
                if (match(TokenType::TOKEN_EQUAL)) {
                    auto val_e = parse_expression();
                    current_val = eval_const_expr(val_e.get());
                    current_bval = 0;
                }
                LogicVal mval(e_width, current_val, current_bval);
                enum_node->members.push_back({member_name, mval});
                ParameterDef pdef;
                pdef.name = member_name;
                pdef.default_expr = std::make_unique<NumberNode>(mval.aval, mval.bval, e_width);
                cls->parameters.push_back(std::move(pdef));
                cls->param_values[member_name] = current_val;
                current_val++;
                current_bval = 0;
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_RBRACE, "Expected '}' closing enum member list");

            std::string anon_name = "__anon_enum_" + std::to_string(anon_struct_counter++);
            typedef_map[anon_name] = enum_node;

            while (true) {
                std::string var_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected member variable name");
                std::unique_ptr<ExpressionNode> def_val = nullptr;
                if (match(TokenType::TOKEN_EQUAL)) {
                    def_val = parse_expression();
                }
                ClassMemberVar mvar;
                mvar.name = var_name;
                mvar.width = e_width;
                mvar.is_signed = e_signed;
                mvar.type_name = anon_name;
                mvar.default_expr = std::move(def_val);
                cls->add_member_var(std::move(mvar));
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after member variable declaration");
            continue;
        } else {
            bool is_rand = false;
            bool is_randc = false;
            if (match(TokenType::TOKEN_RAND)) {
                is_rand = true;
            } else if (match(TokenType::TOKEN_RANDC)) {
                is_randc = true;
            }

            auto is_param_type = [&](const std::string& name) -> bool {
                for (const auto& p : cls->parameters) {
                    if (p.name == name) return true;
                }
                return false;
            };

            bool is_bare_unsigned = (current.type == TokenType::TOKEN_UNSIGNED || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned"));
            bool is_bare_signed = (current.type == TokenType::TOKEN_SIGNED);
            if (is_bare_unsigned || is_bare_signed ||
                current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_BIT ||
                current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_WIRE ||
                current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_STRING ||
                current.type == TokenType::TOKEN_REAL || current.type == TokenType::TOKEN_SHORTREAL ||
                (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint" || current.text == "string" || current.text == "real" || current.text == "shortreal" || current.text == "void" || (is_type_identifier(current.text) && !is_class(current.text) && !is_known_class(current.text)) || is_param_type(current.text)))) {
                std::string member_type_name = current.text;
                bool is_s = (current.type == TokenType::TOKEN_INTEGER || (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint")));
                if (is_bare_unsigned) {
                    member_type_name = "int";
                    is_s = false;
                    advance();
                    if (current.type == TokenType::TOKEN_INTEGER || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "int")) advance();
                } else if (is_bare_signed) {
                    member_type_name = "int";
                    is_s = true;
                    advance();
                    if (current.type == TokenType::TOKEN_INTEGER || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "int")) advance();
                } else {
                    advance();
                }
                if (match(TokenType::TOKEN_HASH)) {
                    expect(TokenType::TOKEN_LPAREN, "Expected '(' after '#' in type specialization");
                    int pdepth = 1;
                    while (pdepth > 0 && current.type != TokenType::TOKEN_EOF) {
                        if (current.type == TokenType::TOKEN_LPAREN) pdepth++;
                        else if (current.type == TokenType::TOKEN_RPAREN) pdepth--;
                        advance();
                    }
                }
                if (match(TokenType::TOKEN_SIGNED)) {
                    is_s = true;
                } else if (match(TokenType::TOKEN_UNSIGNED)) {
                    is_s = false;
                } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned") {
                    is_s = false;
                    advance();
                }
                uint32_t width = 1;
                if (member_type_name == "int" || member_type_name == "integer") width = 32;
                else if (member_type_name == "byte") width = 8;
                else if (member_type_name == "shortint") width = 16;
                else if (member_type_name == "longint" || member_type_name == "real" || member_type_name == "shortreal") width = 64;
                else if (is_s) width = 32;
                std::unique_ptr<ExpressionNode> mvar_msb = nullptr;
                std::unique_ptr<ExpressionNode> mvar_lsb = nullptr;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    auto msb = parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in range");
                    auto lsb = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in range");
                    auto msb_n = dynamic_cast<NumberNode*>(msb.get());
                    auto lsb_n = dynamic_cast<NumberNode*>(lsb.get());
                    if (msb_n && lsb_n) {
                        width = (msb_n->aval >= lsb_n->aval) ? (uint32_t)(msb_n->aval - lsb_n->aval + 1) : (uint32_t)(lsb_n->aval - msb_n->aval + 1);
                    }
                    mvar_msb = std::move(msb);
                    mvar_lsb = std::move(lsb);
                }
                while (true) {
                    std::string var_name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected member variable name");
                    bool is_dyn = false;
                    bool is_queue = false;
                    std::unique_ptr<ExpressionNode> q_bound = nullptr;
                    bool is_assoc = false;
                    std::string assoc_key_type = "";
                    std::vector<std::pair<std::unique_ptr<ExpressionNode>, std::unique_ptr<ExpressionNode>>> mvar_unpacked;
                    while (current.type == TokenType::TOKEN_REP_CONSEC || current.type == TokenType::TOKEN_LBRACKET) {
                        if (match(TokenType::TOKEN_REP_CONSEC)) {
                            is_assoc = true;
                            assoc_key_type = "*";
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                        } else if (match(TokenType::TOKEN_LBRACKET)) {
                            if (match(TokenType::TOKEN_DOLLAR)) {
                                is_queue = true;
                                if (match(TokenType::TOKEN_COLON)) {
                                    q_bound = parse_expression();
                                }
                                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                            } else if (match(TokenType::TOKEN_RBRACKET)) {
                                is_dyn = true;
                            } else if (match(TokenType::TOKEN_STAR)) {
                                is_assoc = true;
                                assoc_key_type = "*";
                                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                            } else if (current.type == TokenType::TOKEN_STRING || (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "string" || is_type_identifier(current.text)))) {
                                is_assoc = true;
                                assoc_key_type = current.text;
                                advance();
                                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                            } else {
                                auto d_msb = parse_expression();
                                std::unique_ptr<ExpressionNode> d_lsb = nullptr;
                                if (match(TokenType::TOKEN_COLON)) {
                                    d_lsb = parse_expression();
                                }
                                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                                mvar_unpacked.push_back({std::move(d_msb), std::move(d_lsb)});
                            }
                        }
                    }
                    std::unique_ptr<ExpressionNode> def_val = nullptr;
                    if (match(TokenType::TOKEN_EQUAL)) {
                        def_val = parse_expression();
                    }
                    ClassMemberVar mvar;
                    mvar.name = var_name;
                    mvar.width = width;
                    mvar.is_signed = is_s;
                    mvar.is_dynamic_array = is_dyn;
                    mvar.is_queue = is_queue;
                    mvar.queue_bound_expr = std::move(q_bound);
                    mvar.is_associative_array = is_assoc;
                    mvar.assoc_key_type = assoc_key_type;
                    mvar.type_name = member_type_name;
                    mvar.default_expr = std::move(def_val);
                    mvar.is_rand = is_rand;
                    mvar.is_randc = is_randc;
                    mvar.is_static = is_stat;
                    mvar.unpacked_dimensions = std::move(mvar_unpacked);
                    if (mvar_msb) mvar.msb_expr = mvar_msb->clone();
                    if (mvar_lsb) mvar.lsb_expr = mvar_lsb->clone();
                    cls->add_member_var(std::move(mvar));
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after member variable declaration");
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && (is_class(current.text) || is_known_class(current.text))) {
                std::string class_type = current.text;
                advance();
                if (match(TokenType::TOKEN_HASH)) {
                    expect(TokenType::TOKEN_LPAREN, "Expected '(' after '#' in class parameter specialization");
                    int pdepth = 1;
                    while (pdepth > 0 && current.type != TokenType::TOKEN_EOF) {
                        if (current.type == TokenType::TOKEN_LPAREN) pdepth++;
                        else if (current.type == TokenType::TOKEN_RPAREN) pdepth--;
                        advance();
                    }
                }
                while (true) {
                    std::string var_name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected member variable name");
                    std::vector<std::pair<std::unique_ptr<ExpressionNode>, std::unique_ptr<ExpressionNode>>> mvar_unpacked;
                    bool is_dyn = false;
                    bool is_queue = false;
                    std::unique_ptr<ExpressionNode> q_bound = nullptr;
                    bool is_assoc = false;
                    std::string assoc_key_type = "";
                    while (current.type == TokenType::TOKEN_LBRACKET || current.type == TokenType::TOKEN_REP_CONSEC) {
                        if (match(TokenType::TOKEN_REP_CONSEC)) {
                            is_assoc = true;
                            assoc_key_type = "*";
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                        } else if (match(TokenType::TOKEN_LBRACKET)) {
                            if (match(TokenType::TOKEN_DOLLAR)) {
                                is_queue = true;
                                if (match(TokenType::TOKEN_COLON)) {
                                    q_bound = parse_expression();
                                }
                                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                            } else if (match(TokenType::TOKEN_RBRACKET)) {
                                is_dyn = true;
                            } else if (match(TokenType::TOKEN_STAR)) {
                                is_assoc = true;
                                assoc_key_type = "*";
                                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                            } else if (current.type == TokenType::TOKEN_STRING || (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "string" || is_type_identifier(current.text)))) {
                                is_assoc = true;
                                assoc_key_type = current.text;
                                advance();
                                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                            } else {
                                auto d_msb = parse_expression();
                                std::unique_ptr<ExpressionNode> d_lsb = nullptr;
                                if (match(TokenType::TOKEN_COLON)) {
                                    d_lsb = parse_expression();
                                }
                                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                                mvar_unpacked.push_back({std::move(d_msb), std::move(d_lsb)});
                            }
                        }
                    }
                    std::unique_ptr<ExpressionNode> def_val = nullptr;
                    if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                        def_val = parse_expression();
                    }
                    ClassMemberVar mvar;
                    mvar.name = var_name;
                    mvar.width = 64;
                    mvar.is_signed = false;
                    mvar.is_rand = is_rand;
                    mvar.is_randc = is_randc;
                    mvar.is_static = is_stat;
                    mvar.is_class_handle = true;
                    mvar.is_dynamic_array = is_dyn;
                    mvar.is_queue = is_queue;
                    mvar.queue_bound_expr = std::move(q_bound);
                    mvar.is_associative_array = is_assoc;
                    mvar.assoc_key_type = assoc_key_type;
                    mvar.class_type_name = class_type;
                    mvar.default_expr = std::move(def_val);
                    mvar.unpacked_dimensions = std::move(mvar_unpacked);
                    cls->add_member_var(std::move(mvar));
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after member handle declaration");
            } else {
                advance();
            }
        }
    }
    expect(TokenType::TOKEN_ENDCLASS, "Expected 'endclass'");
    if (match(TokenType::TOKEN_COLON)) {
        if (current.type == TokenType::TOKEN_IDENTIFIER) {
            if (!cls->name.empty() && current.text != cls->name) {
                throw std::runtime_error("End label '" + current.text + "' does not match class name '" + cls->name + "'");
            }
            advance();
        }
    }
    match(TokenType::TOKEN_SEMICOLON);
    for (const auto& tname : local_class_typedef_names) {
        simple_typedef_map.erase(tname);
        if (current_parsing_module) {
            current_parsing_module->simple_typedefs.erase(tname);
        }
    }
    current_parsing_class = prev_cls;
    return cls;
}

uint64_t Parser::eval_const_expr_with_params(ExpressionNode* expr, const std::unordered_map<std::string, uint64_t>& params) {
    if (!expr) return 0;
    if (auto num = dynamic_cast<NumberNode*>(expr)) {
        return num->aval;
    }
    if (auto id = dynamic_cast<IdentifierNode*>(expr)) {
        auto it = params.find(id->name);
        if (it != params.end()) return it->second;
        if (current_parsing_module) {
            for (const auto& p : current_parsing_module->parameters) {
                if (p.name == id->name && p.default_expr && p.default_expr.get() != expr) {
                    try {
                        uint64_t v = eval_const_expr_with_params(p.default_expr.get(), params);
                        if (v != 0) return v;
                    } catch (...) {}
                }
            }
            auto eit = current_parsing_module->explicit_imports.find(id->name);
            if (eit != current_parsing_module->explicit_imports.end()) {
                auto pit = global_packages.find(eit->second.first);
                if (pit != global_packages.end()) {
                    auto cit = pit->second->param_constants.find(eit->second.second);
                    if (cit != pit->second->param_constants.end()) return cit->second;
                }
            }
            for (const auto& pkg_name : current_parsing_module->wildcard_imports) {
                auto pit = global_packages.find(pkg_name);
                if (pit != global_packages.end()) {
                    auto cit = pit->second->param_constants.find(id->name);
                    if (cit != pit->second->param_constants.end()) return cit->second;
                }
            }
        }
        auto colon = id->name.find("::");
        if (colon != std::string::npos) {
            std::string pkg = id->name.substr(0, colon);
            std::string item = id->name.substr(colon + 2);
            auto pit = global_packages.find(pkg);
            if (pit != global_packages.end()) {
                auto cit = pit->second->param_constants.find(item);
                if (cit != pit->second->param_constants.end()) return cit->second;
            }
        }
        for (const auto& kv : global_packages) {
            auto cit = kv.second->param_constants.find(id->name);
            if (cit != kv.second->param_constants.end()) return cit->second;
        }
        return 0;
    }
    if (auto ma = dynamic_cast<MemberAccessNode*>(expr)) {
        auto it = params.find(ma->var_name + "." + ma->member_name);
        if (it != params.end()) return it->second;
        auto it2 = params.find(ma->member_name);
        if (it2 != params.end()) return it2->second;

        auto it_def = Lexer::global_defines.find("RV_" + ma->member_name);
        if (it_def != Lexer::global_defines.end()) {
            std::string s = it_def->second;
            auto hpos = s.find("'h");
            if (hpos == std::string::npos) hpos = s.find("'H");
            if (hpos != std::string::npos) {
                try { return std::stoull(s.substr(hpos + 2), nullptr, 16); } catch (...) {}
            }
            auto bpos = s.find("'b");
            if (bpos == std::string::npos) bpos = s.find("'B");
            if (bpos != std::string::npos) {
                try { return std::stoull(s.substr(bpos + 2), nullptr, 2); } catch (...) {}
            }
            try { return std::stoull(s, nullptr, 0); } catch (...) {}
        }

        for (const auto& kv : global_packages) {
            auto cit = kv.second->param_constants.find(ma->member_name);
            if (cit != kv.second->param_constants.end()) return cit->second;
        }

        if (current_parsing_module) {
            for (const auto& p : current_parsing_module->parameters) {
                if (p.name == ma->member_name && p.default_expr) {
                    try {
                        uint64_t v = eval_const_expr_with_params(p.default_expr.get(), params);
                        if (v != 0) return v;
                    } catch (...) {}
                }
            }
        }
        if (current_parsing_package) {
            auto it4 = current_parsing_package->param_constants.find(ma->member_name);
            if (it4 != current_parsing_package->param_constants.end()) return it4->second;
        }
        return 0;
    }
    if (auto sma = dynamic_cast<StaticMemberAccessNode*>(expr)) {
        auto pit = global_packages.find(sma->class_type_name);
        if (pit != global_packages.end()) {
            auto cit = pit->second->param_constants.find(sma->member_name);
            if (cit != pit->second->param_constants.end()) return cit->second;
        }
        return 0;
    }
    if (auto un = dynamic_cast<UnaryOpNode*>(expr)) {
        uint64_t val = eval_const_expr_with_params(un->operand.get(), params);
        if (un->op == "-" || un->op == "NEG") return -val;
        if (un->op == "~") return ~val;
        if (un->op == "!") return !val;
        return val;
    }
    if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
        uint64_t l = eval_const_expr_with_params(bin->lhs.get(), params);
        uint64_t r = eval_const_expr_with_params(bin->rhs.get(), params);
        if (bin->op == "+") return l + r;
        if (bin->op == "-") return l - r;
        if (bin->op == "*") return l * r;
        if (bin->op == "**") {
            uint64_t base = l;
            uint64_t exp = r;
            uint64_t res = 1;
            while (exp > 0) {
                if (exp & 1) res *= base;
                base *= base;
                exp >>= 1;
            }
            return res;
        }
        if (bin->op == "/") return r != 0 ? (l / r) : 0;
        if (bin->op == "%") return r != 0 ? (l % r) : 0;
        if (bin->op == "<<" || bin->op == "<<<") return l << r;
        if (bin->op == ">>" || bin->op == ">>>") return l >> r;
        if (bin->op == "&") return l & r;
        if (bin->op == "|") return l | r;
        if (bin->op == "^") return l ^ r;
        if (bin->op == "==" || bin->op == "===") return l == r;
        if (bin->op == "!=" || bin->op == "!==") return l != r;
        if (bin->op == "<") return l < r;
        if (bin->op == "<=") return l <= r;
        if (bin->op == ">") return l > r;
        if (bin->op == ">=") return l >= r;
        return l;
    }
    if (auto to = dynamic_cast<TernaryOpNode*>(expr)) {
        uint64_t cond = eval_const_expr_with_params(to->condition.get(), params);
        if (cond) return eval_const_expr_with_params(to->true_expr.get(), params);
        else return eval_const_expr_with_params(to->false_expr.get(), params);
    }
    if (auto clog = dynamic_cast<Clog2Node*>(expr)) {
        uint64_t v = eval_const_expr_with_params(clog->operand.get(), params);
        return compute_clog2(v);
    }
    if (auto smc = dynamic_cast<StaticMethodCallNode*>(expr)) {
        if (smc->method_name == "vbits") {
            if (!smc->args.empty()) {
                uint64_t val = eval_const_expr_with_params(smc->args[0].get(), params);
                return (val == 1) ? 1 : compute_clog2(val);
            }
        }
        if (smc->method_name == "ceil_div") {
            if (smc->args.size() >= 2) {
                uint64_t dividend = eval_const_expr_with_params(smc->args[0].get(), params);
                uint64_t divisor = eval_const_expr_with_params(smc->args[1].get(), params);
                return (divisor != 0) ? (dividend + divisor - 1) / divisor : 0;
            }
        }
    }
    if (auto fc = dynamic_cast<FunctionCallNode*>(expr)) {
        std::string fname = fc->func_name;
        auto colon = fname.rfind("::");
        if (colon != std::string::npos) fname = fname.substr(colon + 2);
        if (fname == "vbits") {
            if (!fc->args.empty()) {
                uint64_t val = eval_const_expr_with_params(fc->args[0].get(), params);
                return (val == 1) ? 1 : compute_clog2(val);
            }
        }
        if (fname == "ceil_div") {
            if (fc->args.size() >= 2) {
                uint64_t dividend = eval_const_expr_with_params(fc->args[0].get(), params);
                uint64_t divisor = eval_const_expr_with_params(fc->args[1].get(), params);
                return (divisor != 0) ? (dividend + divisor - 1) / divisor : 0;
            }
        }
    }
    if (auto cbn = dynamic_cast<CountBitsNode*>(expr)) {
        uint64_t tval = eval_const_expr_with_params(cbn->target_expr.get(), params);
        uint32_t w = 32;
        if (auto num = dynamic_cast<NumberNode*>(cbn->target_expr.get())) {
            w = num->width;
        }
        bool match_0 = false;
        bool match_1 = false;
        for (auto& c : cbn->control_bits) {
            uint64_t cv = eval_const_expr_with_params(c.get(), params);
            if ((cv & 1ULL) == 0) match_0 = true;
            else match_1 = true;
        }
        uint32_t count = 0;
        uint32_t limit = std::min(w, 64U);
        for (uint32_t i = 0; i < limit; ++i) {
            bool bit = (tval >> i) & 1ULL;
            if (!bit && match_0) count++;
            else if (bit && match_1) count++;
        }
        return (uint64_t)count;
    }
    if (auto adqn = dynamic_cast<ArrayDimensionQueryNode*>(expr)) {
        std::string var_name = adqn->type_name_arg;
        if (var_name.empty() && adqn->target) {
            if (auto id = dynamic_cast<IdentifierNode*>(adqn->target.get())) {
                var_name = id->name;
            }
        }
        const VariableDimMetadata* tmeta = find_type_dim_metadata(var_name);
        if (tmeta && !tmeta->dimensions.empty()) {
            if (adqn->kind == DimQueryKind::TOTAL_DIMENSIONS) return (uint64_t)tmeta->dimensions.size();
            else return (uint64_t)tmeta->unpacked_dims;
        }
        throw std::runtime_error("[CAS-DTINPCIL] Illegal first argument to array query system function; please pass an array identifier or array data type");
    }
    if (auto abqn = dynamic_cast<ArrayBoundQueryNode*>(expr)) {
        std::string var_name = abqn->type_name_arg;
        if (var_name.empty()) {
            if (auto id = dynamic_cast<IdentifierNode*>(abqn->target.get())) {
                var_name = id->name;
            }
        }
        const VariableDimMetadata* tmeta = find_type_dim_metadata(var_name);
        if (!tmeta || tmeta->dimensions.empty()) {
            throw std::runtime_error("[CAS-DTINPCIL] Illegal first argument to array query system function; please pass an array identifier or array data type");
        }
        int64_t dim = 1;
        if (abqn->dimension_expr) {
            dim = (int64_t)eval_const_expr_with_params(abqn->dimension_expr.get(), params);
        }
        if (dim < 1 || (size_t)dim > tmeta->dimensions.size()) {
            throw std::runtime_error("[CAS-DTINPCIL] Invalid dimension index " + std::to_string(dim) + " for array querying function per IEEE 1800-2023 section 20.7");
        }
        const auto& d = tmeta->dimensions[dim - 1];
        switch (abqn->kind) {
            case BoundQueryKind::LEFT: return (uint64_t)(int64_t)d.left;
            case BoundQueryKind::RIGHT: return (uint64_t)(int64_t)d.right;
            case BoundQueryKind::LOW: return (uint64_t)(int64_t)d.low();
            case BoundQueryKind::HIGH: return (uint64_t)(int64_t)d.high();
            case BoundQueryKind::INCREMENT: {
                int64_t inc = d.increment();
                return (inc < 0) ? (uint64_t)(uint32_t)-1 : (uint64_t)inc;
            }
        }
    }
    if (auto aqn = dynamic_cast<ArrayQueryNode*>(expr)) {
        std::string var_name = "";
        if (auto id = dynamic_cast<IdentifierNode*>(aqn->array_expr.get())) {
            var_name = id->name;
        }
        const VariableDimMetadata* tmeta = find_type_dim_metadata(var_name);
        if (tmeta && !tmeta->dimensions.empty()) {
            if (aqn->query_type == ArrayQueryType::DIMENSIONS) return (uint64_t)tmeta->dimensions.size();
            if (aqn->query_type == ArrayQueryType::UNPACKED_DIMENSIONS) return (uint64_t)tmeta->unpacked_dims;
            int64_t dim = 1;
            if (aqn->dim_expr) {
                dim = (int64_t)eval_const_expr_with_params(aqn->dim_expr.get(), params);
            }
            if (dim >= 1 && (size_t)dim <= tmeta->dimensions.size()) {
                const auto& d = tmeta->dimensions[dim - 1];
                switch (aqn->query_type) {
                    case ArrayQueryType::SIZE: return (uint64_t)d.size();
                    case ArrayQueryType::LEFT: return (uint64_t)(int64_t)d.left;
                    case ArrayQueryType::RIGHT: return (uint64_t)(int64_t)d.right;
                    case ArrayQueryType::LOW: return (uint64_t)(int64_t)d.low();
                    case ArrayQueryType::HIGH: return (uint64_t)(int64_t)d.high();
                    case ArrayQueryType::INCREMENT: {
                        int64_t inc = d.increment();
                        return (inc < 0) ? (uint64_t)(uint32_t)-1 : (uint64_t)inc;
                    }
                    default: break;
                }
            }
        }
    }
    if (auto pm = dynamic_cast<PatternMemberNode*>(expr)) {
        return eval_const_expr_with_params(pm->value.get(), params);
    }
    if (auto cat = dynamic_cast<ConcatNode*>(expr)) {
        uint64_t res = 0;
        for (auto& elem : cat->elements) {
            uint64_t val = eval_const_expr_with_params(elem.get(), params);
            uint32_t w = 1;
            if (auto num = dynamic_cast<NumberNode*>(elem.get())) {
                w = num->width;
            }
            res = (res << w) | (val & ((w >= 64) ? ~0ULL : ((1ULL << w) - 1)));
        }
        return res;
    }
    if (auto ucat = dynamic_cast<UnpackedConcatNode*>(expr)) {
        std::vector<std::string> named_members;
        for (const auto& elem : ucat->elements) {
            if (auto pm = dynamic_cast<PatternMemberNode*>(elem.get())) {
                if (!pm->member_name.empty() && pm->member_name != "default") {
                    named_members.push_back(pm->member_name);
                }
            }
        }

        const StructTypeNode* st = nullptr;
        if (!named_members.empty()) {
            auto matches = [&](const StructTypeNode* s) -> bool {
                if (!s) return false;
                for (const auto& nm : named_members) {
                    bool found = false;
                    for (const auto& m : s->members) {
                        if (m.name == nm) { found = true; break; }
                    }
                    if (!found) return false;
                }
                return true;
            };

            if (current_parsing_package) {
                for (const auto& pair : current_parsing_package->struct_typedefs) {
                    if (matches(pair.second.get())) { st = pair.second.get(); break; }
                }
            }
            if (!st && current_parsing_module) {
                for (const auto& pair : current_parsing_module->struct_typedefs) {
                    if (matches(pair.second.get())) { st = pair.second.get(); break; }
                }
            }
            if (!st) {
                for (const auto& kv : global_packages) {
                    for (const auto& pair : kv.second->struct_typedefs) {
                        if (matches(pair.second.get())) { st = pair.second.get(); break; }
                    }
                    if (st) break;
                }
            }
        }

        if (st) {
            size_t n = st->members.size();
            std::vector<uint32_t> offsets(n, 0);
            uint32_t running_offset = 0;
            for (int i = (int)n - 1; i >= 0; --i) {
                offsets[i] = running_offset;
                running_offset += st->members[i].width;
            }
            uint64_t total = 0;
            size_t pos_idx = 0;
            for (const auto& elem : ucat->elements) {
                if (auto pm = dynamic_cast<PatternMemberNode*>(elem.get())) {
                    if (pm->member_name == "default") continue;
                    if (!pm->member_name.empty()) {
                        for (size_t i = 0; i < n; ++i) {
                            if (st->members[i].name == pm->member_name) {
                                uint64_t v = eval_const_expr_with_params(pm->value.get(), params);
                                uint32_t w = st->members[i].width;
                                uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
                                total |= ((v & mask) << offsets[i]);
                                break;
                            }
                        }
                    } else if (pos_idx < n) {
                        uint64_t v = eval_const_expr_with_params(pm->value.get(), params);
                        uint32_t w = st->members[pos_idx].width;
                        uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
                        total |= ((v & mask) << offsets[pos_idx]);
                        pos_idx++;
                    }
                } else if (pos_idx < n) {
                    uint64_t v = eval_const_expr_with_params(elem.get(), params);
                    uint32_t w = st->members[pos_idx].width;
                    uint64_t mask = (w >= 64) ? ~0ULL : ((1ULL << w) - 1);
                    total |= ((v & mask) << offsets[pos_idx]);
                    pos_idx++;
                }
            }
            return total;
        }

        uint64_t total = 0;
        for (auto& el : ucat->elements) {
            uint64_t v = eval_const_expr_with_params(el.get(), params);
            total = (total << 8) | (v & 0xFF);
        }
        return total;
    }
    return 0;
}

uint64_t Parser::eval_const_expr(ExpressionNode* expr) {
    std::unordered_map<std::string, uint64_t> empty_map;
    return eval_const_expr_with_params(expr, empty_map);
}

std::string Parser::parse_class_type_and_specialize() {
    std::string base_name = current.text;
    auto hash_idx = base_name.find('#');
    if (hash_idx != std::string::npos) {
        base_name = base_name.substr(0, hash_idx);
        while (!base_name.empty() && std::isspace(base_name.back())) base_name.pop_back();
    }
    advance();

    ClassNode* base_cls = find_class(base_name);

    if (!base_cls) {
        if (match(TokenType::TOKEN_HASH)) {
            expect(TokenType::TOKEN_LPAREN, "Expected '(' after '#' in class specialization");
            int pdepth = 1;
            while (pdepth > 0 && current.type != TokenType::TOKEN_EOF) {
                if (current.type == TokenType::TOKEN_LPAREN) pdepth++;
                else if (current.type == TokenType::TOKEN_RPAREN) pdepth--;
                advance();
            }
        }
        return base_name;
    }

    if (!match(TokenType::TOKEN_HASH)) {
        if (!base_cls->is_parameterized) {
            return base_name;
        }
        std::unordered_map<std::string, uint64_t> resolved;
        for (const auto& p : base_cls->parameters) {
            if (p.default_expr) resolved[p.name] = eval_const_expr(p.default_expr.get());
            else resolved[p.name] = 0;
        }
        std::string canon_name = base_name + "#(";
        for (size_t i = 0; i < base_cls->parameters.size(); ++i) {
            if (i > 0) canon_name += ",";
            canon_name += std::to_string(resolved[base_cls->parameters[i].name]);
        }
        canon_name += ")";

        if (class_map.find(canon_name) == class_map.end()) {
            auto spec_cls = std::make_unique<ClassNode>(canon_name, base_cls->base_class_name);
            spec_cls->template_class_name = base_name;
            spec_cls->param_values = resolved;
            spec_cls->is_parameterized = false;
            spec_cls->parameters = base_cls->parameters;

            for (const auto& mv : base_cls->member_vars) {
                ClassMemberVar smv;
                smv.name = mv.name;
                smv.is_signed = mv.is_signed;
                smv.is_rand = mv.is_rand;
                smv.is_randc = mv.is_randc;
                smv.is_static = mv.is_static;
                if (mv.default_expr) smv.default_expr = mv.default_expr->clone();
                if (mv.msb_expr && mv.lsb_expr) {
                    uint64_t m = eval_const_expr_with_params(mv.msb_expr.get(), resolved);
                    uint64_t l = eval_const_expr_with_params(mv.lsb_expr.get(), resolved);
                    smv.msb_expr = std::make_unique<NumberNode>(m, 32);
                    smv.lsb_expr = std::make_unique<NumberNode>(l, 32);
                    smv.width = (m >= l) ? (uint32_t)(m - l + 1) : (uint32_t)(l - m + 1);
                } else {
                    smv.width = mv.width;
                }
                spec_cls->add_member_var(std::move(smv));
            }

            for (const auto& fn : base_cls->functions) {
                auto sfn = std::make_unique<FunctionNode>();
                sfn->name = fn->name;
                sfn->is_automatic = fn->is_automatic;
                sfn->is_virtual = fn->is_virtual;
                sfn->is_static = fn->is_static;
                sfn->return_signed = fn->return_signed;
                sfn->shared_body = &fn->body;
                if (fn->return_msb_expr && fn->return_lsb_expr) {
                    uint64_t m = eval_const_expr_with_params(fn->return_msb_expr.get(), resolved);
                    uint64_t l = eval_const_expr_with_params(fn->return_lsb_expr.get(), resolved);
                    sfn->return_msb_expr = std::make_unique<NumberNode>(m, 32);
                    sfn->return_lsb_expr = std::make_unique<NumberNode>(l, 32);
                    sfn->return_width = (m >= l) ? (uint32_t)(m - l + 1) : (uint32_t)(l - m + 1);
                } else {
                    sfn->return_width = fn->return_width;
                }
                for (const auto& inp : fn->inputs) {
                    PortNode sinp(inp.name, inp.direction);
                    sinp.is_signed = inp.is_signed;
                    if (inp.default_value) sinp.default_value = inp.default_value->clone();
                    if (inp.msb_expr && inp.lsb_expr) {
                        uint64_t m = eval_const_expr_with_params(inp.msb_expr.get(), resolved);
                        uint64_t l = eval_const_expr_with_params(inp.lsb_expr.get(), resolved);
                        sinp.msb_expr = std::make_unique<NumberNode>(m, 32);
                        sinp.lsb_expr = std::make_unique<NumberNode>(l, 32);
                    }
                    sfn->inputs.push_back(std::move(sinp));
                }
                spec_cls->add_function(std::move(sfn));
            }

            for (const auto& t : base_cls->tasks) {
                auto st = std::make_unique<TaskNode>();
                st->name = t->name;
                st->is_automatic = t->is_automatic;
                st->is_virtual = t->is_virtual;
                st->is_static = t->is_static;
                st->shared_body = &t->body;
                for (const auto& port : t->ports) {
                    PortNode sport(port.name, port.direction);
                    sport.is_signed = port.is_signed;
                    if (port.default_value) sport.default_value = port.default_value->clone();
                    if (port.msb_expr && port.lsb_expr) {
                        uint64_t m = eval_const_expr_with_params(port.msb_expr.get(), resolved);
                        uint64_t l = eval_const_expr_with_params(port.lsb_expr.get(), resolved);
                        sport.msb_expr = std::make_unique<NumberNode>(m, 32);
                        sport.lsb_expr = std::make_unique<NumberNode>(l, 32);
                    }
                    st->ports.push_back(std::move(sport));
                }
                spec_cls->add_task(std::move(st));
            }

            for (const auto& c : base_cls->constraints) {
                std::vector<std::unique_ptr<ExpressionNode>> cexprs;
                for (const auto& e : c->exprs) if (e) cexprs.push_back(e->clone());
                spec_cls->add_constraint(std::make_unique<ConstraintBlockNode>(c->name, std::move(cexprs)));
            }

            std::shared_ptr<ClassNode> s_spec = std::move(spec_cls);
            class_map[canon_name] = s_spec;
            global_classes.push_back(s_spec);
            if (current_parsing_module) {
                current_parsing_module->classes.push_back(s_spec);
                current_parsing_module->class_map[canon_name] = s_spec.get();
            }
        }
        return canon_name;
    }

    expect(TokenType::TOKEN_LPAREN, "Expected '(' after '#' in class specialization");
    std::vector<std::pair<std::string, std::unique_ptr<ExpressionNode>>> named_args;
    std::vector<std::unique_ptr<ExpressionNode>> pos_args;

    while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
        if (match(TokenType::TOKEN_DOT)) {
            std::string pname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter identifier after '.'");
            expect(TokenType::TOKEN_LPAREN, "Expected '(' after parameter name");
            auto pexpr = parse_expression();
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing parameter value");
            named_args.push_back({pname, std::move(pexpr)});
        } else {
            bool is_type_arg = false;
            std::string type_arg_name = "";
            if (current.type == TokenType::TOKEN_VIRTUAL) {
                is_type_arg = true;
                type_arg_name = "virtual";
                advance();
                if (current.type == TokenType::TOKEN_IDENTIFIER) {
                    type_arg_name += " " + current.text;
                    advance();
                }
            } else if (current.type == TokenType::TOKEN_TYPE) {
                is_type_arg = true;
                advance();
                type_arg_name = current.text;
                advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && is_interface_type(current.text)) {
                is_type_arg = true;
                type_arg_name = current.text;
                advance();
            }
            if (is_type_arg) {
                pos_args.push_back(std::make_unique<IdentifierNode>(type_arg_name));
            } else {
                pos_args.push_back(parse_expression());
            }
        }
        if (match(TokenType::TOKEN_COMMA)) continue;
        break;
    }
    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing class specialization arguments");

    std::unordered_map<std::string, uint64_t> resolved;
    std::unordered_map<std::string, std::string> type_resolved;
    for (const auto& p : base_cls->parameters) {
        if (p.default_expr) resolved[p.name] = eval_const_expr(p.default_expr.get());
        else resolved[p.name] = 0;
    }
    for (size_t i = 0; i < pos_args.size() && i < base_cls->parameters.size(); ++i) {
        if (auto id = dynamic_cast<IdentifierNode*>(pos_args[i].get())) {
            if (id->name.rfind("virtual", 0) == 0 || is_type_identifier(id->name) || is_class(id->name) || is_interface_type(id->name)) {
                type_resolved[base_cls->parameters[i].name] = id->name;
                continue;
            }
        }
        try {
            resolved[base_cls->parameters[i].name] = eval_const_expr(pos_args[i].get());
        } catch (...) {
            if (auto id2 = dynamic_cast<IdentifierNode*>(pos_args[i].get())) {
                type_resolved[base_cls->parameters[i].name] = id2->name;
            }
        }
    }
    for (const auto& na : named_args) {
        resolved[na.first] = eval_const_expr(na.second.get());
    }

    std::string canon_name = base_name + "#(";
    for (size_t i = 0; i < base_cls->parameters.size(); ++i) {
        if (i > 0) canon_name += ",";
        std::string pname = base_cls->parameters[i].name;
        if (type_resolved.count(pname)) {
            canon_name += type_resolved[pname];
        } else {
            canon_name += std::to_string(resolved[pname]);
        }
    }
    canon_name += ")";

    if (class_map.find(canon_name) == class_map.end()) {
        auto spec_cls = std::make_unique<ClassNode>(canon_name, base_cls->base_class_name);
        spec_cls->template_class_name = base_name;
        spec_cls->param_values = resolved;
        spec_cls->is_parameterized = false;
        spec_cls->parameters = base_cls->parameters;

        for (const auto& mv : base_cls->member_vars) {
            ClassMemberVar smv;
            smv.name = mv.name;
            smv.is_signed = mv.is_signed;
            smv.is_rand = mv.is_rand;
            smv.is_randc = mv.is_randc;
            smv.is_static = mv.is_static;
            if (mv.default_expr) smv.default_expr = mv.default_expr->clone();
            if (mv.msb_expr && mv.lsb_expr) {
                uint64_t m = eval_const_expr_with_params(mv.msb_expr.get(), resolved);
                uint64_t l = eval_const_expr_with_params(mv.lsb_expr.get(), resolved);
                smv.msb_expr = std::make_unique<NumberNode>(m, 32);
                smv.lsb_expr = std::make_unique<NumberNode>(l, 32);
                smv.width = (m >= l) ? (uint32_t)(m - l + 1) : (uint32_t)(l - m + 1);
            } else {
                smv.width = mv.width;
            }
            spec_cls->add_member_var(std::move(smv));
        }

        for (const auto& fn : base_cls->functions) {
            auto sfn = std::make_unique<FunctionNode>();
            sfn->name = fn->name;
            sfn->is_automatic = fn->is_automatic;
            sfn->is_virtual = fn->is_virtual;
            sfn->is_static = fn->is_static;
            sfn->return_signed = fn->return_signed;
            sfn->shared_body = &fn->body;
            if (fn->return_msb_expr && fn->return_lsb_expr) {
                uint64_t m = eval_const_expr_with_params(fn->return_msb_expr.get(), resolved);
                uint64_t l = eval_const_expr_with_params(fn->return_lsb_expr.get(), resolved);
                sfn->return_msb_expr = std::make_unique<NumberNode>(m, 32);
                sfn->return_lsb_expr = std::make_unique<NumberNode>(l, 32);
                sfn->return_width = (m >= l) ? (uint32_t)(m - l + 1) : (uint32_t)(l - m + 1);
            } else {
                sfn->return_width = fn->return_width;
            }
            for (const auto& inp : fn->inputs) {
                PortNode sinp(inp.name, inp.direction);
                sinp.is_signed = inp.is_signed;
                if (inp.default_value) sinp.default_value = inp.default_value->clone();
                if (inp.msb_expr && inp.lsb_expr) {
                    uint64_t m = eval_const_expr_with_params(inp.msb_expr.get(), resolved);
                    uint64_t l = eval_const_expr_with_params(inp.lsb_expr.get(), resolved);
                    sinp.msb_expr = std::make_unique<NumberNode>(m, 32);
                    sinp.lsb_expr = std::make_unique<NumberNode>(l, 32);
                }
                sfn->inputs.push_back(std::move(sinp));
            }
            spec_cls->add_function(std::move(sfn));
        }

        for (const auto& t : base_cls->tasks) {
            auto st = std::make_unique<TaskNode>();
            st->name = t->name;
            st->is_automatic = t->is_automatic;
            st->is_virtual = t->is_virtual;
            st->is_static = t->is_static;
            st->shared_body = &t->body;
            for (const auto& port : t->ports) {
                PortNode sport(port.name, port.direction);
                sport.is_signed = port.is_signed;
                if (port.default_value) sport.default_value = port.default_value->clone();
                if (port.msb_expr && port.lsb_expr) {
                    uint64_t m = eval_const_expr_with_params(port.msb_expr.get(), resolved);
                    uint64_t l = eval_const_expr_with_params(port.lsb_expr.get(), resolved);
                    sport.msb_expr = std::make_unique<NumberNode>(m, 32);
                    sport.lsb_expr = std::make_unique<NumberNode>(l, 32);
                }
                st->ports.push_back(std::move(sport));
            }
            spec_cls->add_task(std::move(st));
        }

        for (const auto& c : base_cls->constraints) {
            std::vector<std::unique_ptr<ExpressionNode>> cexprs;
            for (const auto& e : c->exprs) if (e) cexprs.push_back(e->clone());
            spec_cls->add_constraint(std::make_unique<ConstraintBlockNode>(c->name, std::move(cexprs)));
        }

        std::shared_ptr<ClassNode> s_spec = std::move(spec_cls);
        class_map[canon_name] = s_spec;
        global_classes.push_back(s_spec);
        if (current_parsing_module) {
            current_parsing_module->classes.push_back(s_spec);
            current_parsing_module->class_map[canon_name] = s_spec.get();
        }
    }

    return canon_name;
}


std::unique_ptr<CovergroupNode> Parser::parse_covergroup() {
    expect(TokenType::TOKEN_COVERGROUP, "Expected 'covergroup'");
    std::string cg_name = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected covergroup name");

    auto cg = std::make_unique<CovergroupNode>(cg_name);

    if (match(TokenType::TOKEN_LPAREN)) {
        while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
            std::string dir = "input";
            if (current.type == TokenType::TOKEN_REF || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "ref")) {
                dir = "ref";
                advance();
            } else if (match(TokenType::TOKEN_INPUT)) {
                dir = "input";
            } else if (match(TokenType::TOKEN_OUTPUT)) {
                dir = "output";
            } else if (match(TokenType::TOKEN_INOUT)) {
                dir = "inout";
            }

            std::string type_str = "logic";
            bool is_signed = false;
            std::unique_ptr<ExpressionNode> msb = nullptr;
            std::unique_ptr<ExpressionNode> lsb = nullptr;

            if (match(TokenType::TOKEN_SIGNED)) is_signed = true;

            if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE ||
                current.type == TokenType::TOKEN_REG   || current.type == TokenType::TOKEN_BIT) {
                type_str = current.text;
                advance();
            } else if (current.type == TokenType::TOKEN_INTEGER) {
                type_str = "integer";
                is_signed = true;
                msb = std::make_unique<NumberNode>(31, 32);
                lsb = std::make_unique<NumberNode>(0, 32);
                advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint")) {
                type_str = current.text;
                is_signed = true;
                if (current.text == "byte") { msb = std::make_unique<NumberNode>(7, 32); }
                else if (current.text == "shortint") { msb = std::make_unique<NumberNode>(15, 32); }
                else if (current.text == "longint") { msb = std::make_unique<NumberNode>(63, 32); }
                else { msb = std::make_unique<NumberNode>(31, 32); }
                lsb = std::make_unique<NumberNode>(0, 32);
                advance();
            }

            if (match(TokenType::TOKEN_LBRACKET)) {
                msb = parse_expression();
                expect(TokenType::TOKEN_COLON, "Expected ':' in port range");
                lsb = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in port range");
            }

            std::string port_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected argument name");

            PortNode pnode(port_name, dir, std::move(msb), std::move(lsb), is_signed, NetKind::VAR);
            pnode.type_name = type_str;

            if (match(TokenType::TOKEN_EQUAL)) {
                pnode.default_value = parse_expression();
            }

            cg->arguments.push_back(std::move(pnode));

            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing covergroup arguments");
    }

    if (match(TokenType::TOKEN_AT)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after '@'");
        cg->has_event_trigger = true;
        if (match(TokenType::TOKEN_POSEDGE)) cg->event_edge = EdgeType::POSEDGE;
        else if (match(TokenType::TOKEN_NEGEDGE)) cg->event_edge = EdgeType::NEGEDGE;
        else cg->event_edge = EdgeType::LEVEL;

        cg->event_signal = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected clock/event signal name");
        while (match(TokenType::TOKEN_DOT)) {
            cg->event_signal += "." + current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.'");
        }
        if (current.type == TokenType::TOKEN_IFF || current.text == "iff") {
            advance();
            cg->iff_expr = parse_expression();
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing event trigger");
    }

    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after covergroup header");

    while (current.type != TokenType::TOKEN_ENDGROUP && current.type != TokenType::TOKEN_EOF) {
        std::string label = "";
        if (current.type == TokenType::TOKEN_IDENTIFIER) {
            Token next_tok = peek_token();
            if (next_tok.type == TokenType::TOKEN_COLON) {
                label = current.text;
                advance();
                advance();
            } else if (current.text == "option") {
                advance();
                expect(TokenType::TOKEN_DOT, "Expected '.' after option");
                std::string opt_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected option identifier");
                expect(TokenType::TOKEN_EQUAL, "Expected '=' in option assignment");
                auto val_expr = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after option assignment");
                if (opt_name == "per_instance") {
                    cg->per_instance = true;
                }
                continue;
            }
        }

        if (current.type == TokenType::TOKEN_COVERPOINT) {
            advance();
            auto target_expr = parse_expression();
            auto cp = std::make_unique<CoverpointNode>(label, std::move(target_expr));

            if (match(TokenType::TOKEN_LBRACE)) {
                while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                    if (match(TokenType::TOKEN_BINS)) {
                        std::string bin_name = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected bin name");
                        if (match(TokenType::TOKEN_LBRACKET)) {
                            while (current.type != TokenType::TOKEN_RBRACKET && current.type != TokenType::TOKEN_EOF) {
                                advance();
                            }
                            match(TokenType::TOKEN_RBRACKET);
                        }
                        expect(TokenType::TOKEN_EQUAL, "Expected '=' after bin name");
                        expect(TokenType::TOKEN_LBRACE, "Expected '{' for bin values");
                        std::vector<InsideRange> bin_ranges;
                        while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                            if (match(TokenType::TOKEN_LBRACKET)) {
                                std::unique_ptr<ExpressionNode> low_expr;
                                if (current.text == "$") {
                                    advance();
                                    low_expr = std::make_unique<NumberNode>(0, 0, 64);
                                } else {
                                    low_expr = parse_expression();
                                }
                                expect(TokenType::TOKEN_COLON, "Expected ':' in bin range");
                                std::unique_ptr<ExpressionNode> high_expr;
                                if (current.text == "$") {
                                    advance();
                                    high_expr = std::make_unique<NumberNode>(~0ULL, 0, 64);
                                } else {
                                    high_expr = parse_expression();
                                }
                                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in bin range");
                                bin_ranges.push_back({std::move(low_expr), std::move(high_expr)});
                            } else {
                                auto val_expr = parse_expression();
                                bin_ranges.push_back({std::move(val_expr), nullptr});
                            }
                            match(TokenType::TOKEN_COMMA);
                        }
                        expect(TokenType::TOKEN_RBRACE, "Expected '}' closing bin values");
                        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after bin declaration");
                        cp->bins.push_back(std::make_unique<CovergroupBinNode>(bin_name, std::move(bin_ranges)));
                    } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "option") {
                        advance();
                        expect(TokenType::TOKEN_DOT, "Expected '.' after option");
                        std::string opt_name = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected option identifier");
                        expect(TokenType::TOKEN_EQUAL, "Expected '=' in option assignment");
                        auto val_expr = parse_expression();
                        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after option assignment");
                        continue;
                    } else {
                        advance();
                    }
                }
                expect(TokenType::TOKEN_RBRACE, "Expected '}' closing coverpoint body");
            } else {
                match(TokenType::TOKEN_SEMICOLON);
            }
            cg->add_coverpoint(std::move(cp));
        } else if (current.type == TokenType::TOKEN_CROSS) {
            advance();
            std::vector<std::string> cps;
            while (current.type == TokenType::TOKEN_IDENTIFIER) {
                cps.push_back(current.text);
                advance();
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            if (match(TokenType::TOKEN_LBRACE)) {
                int depth = 1;
                while (depth > 0 && current.type != TokenType::TOKEN_EOF) {
                    if (current.type == TokenType::TOKEN_LBRACE) depth++;
                    else if (current.type == TokenType::TOKEN_RBRACE) depth--;
                    advance();
                }
            }
            match(TokenType::TOKEN_SEMICOLON);
            cg->add_cross(std::make_unique<CoverCrossNode>(label, std::move(cps)));
        } else {
            advance();
        }
    }

    expect(TokenType::TOKEN_ENDGROUP, "Expected 'endgroup'");
    if (match(TokenType::TOKEN_COLON)) {
        if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
    }
    return cg;
}


std::unique_ptr<PropertyNode> Parser::parse_property() {
    expect(TokenType::TOKEN_PROPERTY, "Expected 'property'");
    std::string prop_name = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected property name");

    if (match(TokenType::TOKEN_LPAREN)) {
        int depth = 1;
        while (depth > 0 && current.type != TokenType::TOKEN_EOF) {
            if (current.type == TokenType::TOKEN_LPAREN) depth++;
            else if (current.type == TokenType::TOKEN_RPAREN) depth--;
            advance();
        }
    }
    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after property header");

    auto prop = std::make_unique<PropertyNode>();
    prop->name = prop_name;

    auto parse_sva_local_decl = [&](std::vector<SvaLocalVar>& local_vars) {
        while (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG ||
               current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_BIT ||
               current.type == TokenType::TOKEN_INTEGER ||
               (current.type == TokenType::TOKEN_IDENTIFIER && 
                (current.text == "byte" || current.text == "shortint" || current.text == "int" || current.text == "longint"))) {
            std::string type_name = current.text;
            bool is_int = (current.type == TokenType::TOKEN_INTEGER || current.text == "int");
            advance();
            bool is_signed = is_int;
            if (current.type == TokenType::TOKEN_SIGNED) {
                is_signed = true;
                advance();
            }
            uint32_t width = is_int ? 32 : 1;
            if (type_name == "byte") width = 8;
            else if (type_name == "shortint") width = 16;
            else if (type_name == "longint") width = 64;

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

            while (true) {
                std::string vname = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected variable identifier in local variable declaration");
                if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                    parse_expression();
                }
                SvaLocalVar p;
                p.name = vname;
                p.width = width;
                p.is_signed = is_signed;
                local_vars.push_back(p);
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after local variable declaration");
        }
    };

    parse_sva_local_decl(prop->local_vars);

    if (match(TokenType::TOKEN_AT)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after '@'");
        if (match(TokenType::TOKEN_POSEDGE)) {
            prop->clock_edge = EdgeType::POSEDGE;
        } else if (match(TokenType::TOKEN_NEGEDGE)) {
            prop->clock_edge = EdgeType::NEGEDGE;
        } else {
            prop->clock_edge = EdgeType::LEVEL;
        }
        prop->clock_signal = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected clock identifier in property");
        while (match(TokenType::TOKEN_DOT)) {
            prop->clock_signal += "." + current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in property clock");
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing property clock");
    }

    if (match(TokenType::TOKEN_DISABLE)) {
        expect(TokenType::TOKEN_IFF, "Expected 'iff' after 'disable'");
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'disable iff'");
        prop->disable_expr = parse_expression();
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing 'disable iff'");
    }

    auto first_expr = parse_sequence_expression();
    if (match(TokenType::TOKEN_IFF)) {
        auto rhs_expr = parse_sequence_expression();
        first_expr = std::make_unique<BinaryOpNode>("iff", std::move(first_expr), std::move(rhs_expr));
    } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "iff") {
        advance();
        auto rhs_expr = parse_sequence_expression();
        first_expr = std::make_unique<BinaryOpNode>("iff", std::move(first_expr), std::move(rhs_expr));
    }
    if (match(TokenType::TOKEN_NON_OVERLAP_IMP)) {
        prop->imp_type = ImplicationType::NON_OVERLAPPING;
        prop->antecedent = std::move(first_expr);
        prop->consequent = parse_sequence_expression();
    } else if (match(TokenType::TOKEN_OVERLAP_IMP)) {
        prop->imp_type = ImplicationType::OVERLAPPING;
        prop->antecedent = std::move(first_expr);
        prop->consequent = parse_sequence_expression();
    } else {
        prop->imp_type = ImplicationType::NONE;
        prop->antecedent = std::move(first_expr);
    }

    match(TokenType::TOKEN_SEMICOLON);
    expect(TokenType::TOKEN_ENDPROPERTY, "Expected 'endproperty'");
    if (match(TokenType::TOKEN_COLON)) {
        if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
    }
    return prop;
}

std::unique_ptr<SequenceNode> Parser::parse_sequence() {
    expect(TokenType::TOKEN_SEQUENCE, "Expected 'sequence'");
    std::string seq_name = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected sequence name");

    if (match(TokenType::TOKEN_LPAREN)) {
        int depth = 1;
        while (depth > 0 && current.type != TokenType::TOKEN_EOF) {
            if (current.type == TokenType::TOKEN_LPAREN) depth++;
            else if (current.type == TokenType::TOKEN_RPAREN) depth--;
            advance();
        }
    }
    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after sequence header");

    auto seq = std::make_unique<SequenceNode>();
    seq->name = seq_name;

    while (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG ||
           current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_BIT ||
           current.type == TokenType::TOKEN_INTEGER ||
           (current.type == TokenType::TOKEN_IDENTIFIER && 
            (current.text == "byte" || current.text == "shortint" || current.text == "int" || current.text == "longint"))) {
        std::string type_name = current.text;
        bool is_int = (current.type == TokenType::TOKEN_INTEGER || current.text == "int");
        advance();
        bool is_signed = is_int;
        if (current.type == TokenType::TOKEN_SIGNED) {
            is_signed = true;
            advance();
        }
        uint32_t width = is_int ? 32 : 1;
        if (type_name == "byte") width = 8;
        else if (type_name == "shortint") width = 16;
        else if (type_name == "longint") width = 64;

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

        while (true) {
            std::string vname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected variable identifier in local variable declaration");
            if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                parse_expression();
            }
            SvaLocalVar p;
            p.name = vname;
            p.width = width;
            p.is_signed = is_signed;
            seq->local_vars.push_back(p);
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after local variable declaration");
    }

    if (match(TokenType::TOKEN_AT)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after '@'");
        if (match(TokenType::TOKEN_POSEDGE)) {
            seq->clock_edge = EdgeType::POSEDGE;
        } else if (match(TokenType::TOKEN_NEGEDGE)) {
            seq->clock_edge = EdgeType::NEGEDGE;
        } else {
            seq->clock_edge = EdgeType::LEVEL;
        }
        seq->clock_signal = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected clock identifier in sequence");
        while (match(TokenType::TOKEN_DOT)) {
            seq->clock_signal += "." + current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in sequence clock");
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing sequence clock");
    }

    auto seq_expr = parse_sequence_expression();
    if (auto concat = dynamic_cast<SequenceConcatNode*>(seq_expr.get())) {
        for (auto& item : concat->elements) {
            seq->items.push_back({item.delay, item.max_delay, item.is_range, item.clock_signal, item.clock_edge, std::move(item.expr)});
        }
    } else {
        seq->items.push_back({0, 0, false, "", EdgeType::POSEDGE, std::move(seq_expr)});
    }

    match(TokenType::TOKEN_SEMICOLON);
    expect(TokenType::TOKEN_ENDSEQUENCE, "Expected 'endsequence'");
    if (match(TokenType::TOKEN_COLON)) {
        if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
    }
    return seq;
}

std::unique_ptr<AssertPropertyStatementNode> Parser::parse_assert_property(const std::string& label) {
    bool is_assume = (current.type == TokenType::TOKEN_ASSUME);
    bool is_expect = (current.type == TokenType::TOKEN_EXPECT);
    bool is_assert = (current.type == TokenType::TOKEN_ASSERT);
    advance(); // consume assert/assume/expect

    bool is_deferred_zero = false;
    bool is_final = false;
    if (match(TokenType::TOKEN_HASH)) {
        if (current.type == TokenType::TOKEN_NUMBER && current.num_val == 0) {
            advance();
            is_deferred_zero = true;
        }
    } else if (match(TokenType::TOKEN_FINAL)) {
        is_final = true;
    }

    bool is_property = match(TokenType::TOKEN_PROPERTY);
    expect(TokenType::TOKEN_LPAREN, "Expected '(' after assertion");

    auto assert_node = std::make_unique<AssertPropertyStatementNode>();
    assert_node->label = label;
    assert_node->is_assume = is_assume;
    assert_node->is_expect = is_expect;
    assert_node->is_deferred_zero = is_deferred_zero;
    assert_node->is_final = is_final;

    if (is_property && current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_RPAREN) {
        assert_node->property_name = current.text;
        advance();
    } else if (is_property && current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_DOT) {
        std::string pname = current.text;
        advance(); // ident
        expect(TokenType::TOKEN_DOT, "Expected '.' in property name");
        pname += "." + current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in property name");
        assert_node->property_name = pname;
    } else {
        auto inlined = std::make_unique<PropertyNode>();
        if (is_property || is_expect) {
            while (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG ||
                   current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_BIT ||
                   current.type == TokenType::TOKEN_INTEGER ||
                   (current.type == TokenType::TOKEN_IDENTIFIER && 
                    (current.text == "byte" || current.text == "shortint" || current.text == "int" || current.text == "longint"))) {
                std::string type_name = current.text;
                bool is_int = (current.type == TokenType::TOKEN_INTEGER || current.text == "int");
                advance();
                bool is_signed = is_int;
                if (current.type == TokenType::TOKEN_SIGNED) {
                    is_signed = true;
                    advance();
                }
                uint32_t width = is_int ? 32 : 1;
                if (type_name == "byte") width = 8;
                else if (type_name == "shortint") width = 16;
                else if (type_name == "longint") width = 64;

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

                while (true) {
                    std::string vname = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected variable identifier in local variable declaration");
                    if (match(TokenType::TOKEN_EQUAL) || match(TokenType::TOKEN_ASSIGN)) {
                        parse_expression();
                    }
                    SvaLocalVar p;
                    p.name = vname;
                    p.width = width;
                    p.is_signed = is_signed;
                    inlined->local_vars.push_back(p);
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after local variable declaration");
            }
            if (match(TokenType::TOKEN_AT)) {
                expect(TokenType::TOKEN_LPAREN, "Expected '(' after '@'");
                if (match(TokenType::TOKEN_POSEDGE)) inlined->clock_edge = EdgeType::POSEDGE;
                else if (match(TokenType::TOKEN_NEGEDGE)) inlined->clock_edge = EdgeType::NEGEDGE;
                inlined->clock_signal = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected clock identifier");
                while (match(TokenType::TOKEN_DOT)) {
                    inlined->clock_signal += "." + current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in clock signal");
                }
                expect(TokenType::TOKEN_RPAREN, "Expected ')' closing clock");
            }
            if (match(TokenType::TOKEN_DISABLE)) {
                expect(TokenType::TOKEN_IFF, "Expected 'iff'");
                expect(TokenType::TOKEN_LPAREN, "Expected '('");
                inlined->disable_expr = parse_expression();
                expect(TokenType::TOKEN_RPAREN, "Expected ')'");
            }
        }
        auto first_expr = parse_sequence_expression();
        if (is_property || is_expect) {
            if (match(TokenType::TOKEN_IFF)) {
                auto rhs_expr = parse_sequence_expression();
                first_expr = std::make_unique<BinaryOpNode>("iff", std::move(first_expr), std::move(rhs_expr));
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "iff") {
                advance();
                auto rhs_expr = parse_sequence_expression();
                first_expr = std::make_unique<BinaryOpNode>("iff", std::move(first_expr), std::move(rhs_expr));
            }
        }
        if ((is_property || is_expect) && match(TokenType::TOKEN_NON_OVERLAP_IMP)) {
            inlined->imp_type = ImplicationType::NON_OVERLAPPING;
            inlined->antecedent = std::move(first_expr);
            inlined->consequent = parse_sequence_expression();
        } else if ((is_property || is_expect) && match(TokenType::TOKEN_OVERLAP_IMP)) {
            inlined->imp_type = ImplicationType::OVERLAPPING;
            inlined->antecedent = std::move(first_expr);
            inlined->consequent = parse_sequence_expression();
        } else {
            inlined->imp_type = ImplicationType::NONE;
            inlined->antecedent = std::move(first_expr);
        }
        assert_node->inlined_property = std::move(inlined);
    }
    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing assertion");

    if (current.type == TokenType::TOKEN_BEGIN ||
        (current.type != TokenType::TOKEN_ELSE && current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_EOF)) {
        assert_node->pass_statements = parse_statement_or_block();
    }

    if (match(TokenType::TOKEN_ELSE)) {
        assert_node->fail_statements = parse_statement_or_block();
    }

    match(TokenType::TOKEN_SEMICOLON);
    return assert_node;
}

std::unique_ptr<DpiImportNode> Parser::parse_dpi_import() {
    expect(TokenType::TOKEN_IMPORT, "Expected 'import'");
    if (current.type != TokenType::TOKEN_STRING || (current.text != "DPI-C" && current.text != "DPI")) {
        throw std::runtime_error(current.filename + ":" + std::to_string(current.line) + 
            ": error: Expected '\"DPI-C\"' or '\"DPI\"' in DPI import (found: '" + current.text + "')");
    }
    advance();

    auto dpi_node = std::make_unique<DpiImportNode>();

    while (current.type == TokenType::TOKEN_PURE || current.type == TokenType::TOKEN_CONTEXT) {
        if (current.type == TokenType::TOKEN_PURE) {
            dpi_node->is_pure = true;
            advance();
        } else if (current.type == TokenType::TOKEN_CONTEXT) {
            dpi_node->is_context = true;
            advance();
        }
    }

    std::string explicit_c_name = "";
    if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_EQUAL) {
        explicit_c_name = current.text;
        advance();
        expect(TokenType::TOKEN_EQUAL, "Expected '=' after foreign C identifier in DPI import");
    }

    if (current.type == TokenType::TOKEN_FUNCTION) {
        dpi_node->is_task = false;
        advance();

        uint32_t ret_w = 32;
        bool ret_signed = true;
        std::string ret_type = "int";

        if (current.type == TokenType::TOKEN_VOID || current.text == "void") {
            ret_type = "void";
            ret_w = 0;
            ret_signed = false;
            advance();
        } else if (current.type == TokenType::TOKEN_CHANDLE || current.text == "chandle") {
            ret_type = "chandle";
            ret_w = 64;
            ret_signed = false;
            advance();
        } else if (current.type == TokenType::TOKEN_INTEGER || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "int")) {
            ret_type = "int";
            ret_w = 32;
            ret_signed = true;
            advance();
        } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "byte") {
            ret_type = "byte";
            ret_w = 8;
            ret_signed = true;
            advance();
        } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "shortint") {
            ret_type = "shortint";
            ret_w = 16;
            ret_signed = true;
            advance();
        } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "longint") {
            ret_type = "longint";
            ret_w = 64;
            ret_signed = true;
            advance();
        } else if (current.type == TokenType::TOKEN_BIT || current.type == TokenType::TOKEN_LOGIC) {
            ret_type = current.text;
            ret_w = 1;
            ret_signed = false;
            advance();
        } else if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type != TokenType::TOKEN_LPAREN) {
            ret_type = current.text;
            ret_w = 32;
            advance();
        }

        if (match(TokenType::TOKEN_LBRACKET)) {
            auto msb_e = parse_expression();
            expect(TokenType::TOKEN_COLON, "Expected ':' in return type range");
            auto lsb_e = parse_expression();
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in return type range");
            if (auto nm = dynamic_cast<NumberNode*>(msb_e.get())) {
                if (auto nl = dynamic_cast<NumberNode*>(lsb_e.get())) {
                    ret_w = (nm->aval >= nl->aval) ? (uint32_t)(nm->aval - nl->aval + 1) : (uint32_t)(nl->aval - nm->aval + 1);
                }
            }
        }

        dpi_node->return_type = ret_type;
        dpi_node->return_width = ret_w;
        dpi_node->return_signed = ret_signed;

        if (explicit_c_name.empty() && current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_EQUAL) {
            explicit_c_name = current.text;
            advance();
            expect(TokenType::TOKEN_EQUAL, "Expected '=' after foreign C identifier in DPI import");
        }

        std::string fname = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected function identifier in DPI import");
        dpi_node->sv_name = fname;
    } else if (current.type == TokenType::TOKEN_TASK) {
        dpi_node->is_task = true;
        dpi_node->return_type = "void";
        dpi_node->return_width = 0;
        advance();

        if (explicit_c_name.empty() && current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_EQUAL) {
            explicit_c_name = current.text;
            advance();
            expect(TokenType::TOKEN_EQUAL, "Expected '=' after foreign C identifier in DPI import");
        }

        std::string tname = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected task identifier in DPI import");
        dpi_node->sv_name = tname;
    } else {
        throw std::runtime_error(current.filename + ":" + std::to_string(current.line) + 
            ": error: Expected 'function' or 'task' in DPI import declaration");
    }

    dpi_node->c_name = explicit_c_name.empty() ? dpi_node->sv_name : explicit_c_name;

    expect(TokenType::TOKEN_LPAREN, "Expected '(' in DPI import port list");
    if (current.type != TokenType::TOKEN_RPAREN) {
        while (true) {
            std::string pdir = "input";
            if (current.type == TokenType::TOKEN_INPUT) { pdir = "input"; advance(); }
            else if (current.type == TokenType::TOKEN_OUTPUT) { pdir = "output"; advance(); }
            else if (current.type == TokenType::TOKEN_INOUT) { pdir = "inout"; advance(); }

            std::string ptype = "logic";
            uint32_t pw = 32;
            bool psigned = false;

            if (current.type == TokenType::TOKEN_CHANDLE || current.text == "chandle") {
                ptype = "chandle";
                pw = 64;
                psigned = false;
                advance();
            } else if (current.type == TokenType::TOKEN_INTEGER || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "int")) {
                ptype = "int";
                pw = 32;
                psigned = true;
                advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "byte") {
                ptype = "byte";
                pw = 8;
                psigned = true;
                advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "shortint") {
                ptype = "shortint";
                pw = 16;
                psigned = true;
                advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "longint") {
                ptype = "longint";
                pw = 64;
                psigned = true;
                advance();
            } else if (current.type == TokenType::TOKEN_BIT || current.type == TokenType::TOKEN_LOGIC) {
                ptype = current.text;
                pw = 1;
                psigned = false;
                advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_IDENTIFIER) {
                ptype = current.text;
                pw = 32;
                advance();
            }

            bool is_open_array = false;
            if (match(TokenType::TOKEN_LBRACKET)) {
                if (match(TokenType::TOKEN_RBRACKET)) {
                    is_open_array = true;
                } else {
                    auto msb_e = parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in port range");
                    auto lsb_e = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in port range");
                    if (auto nm = dynamic_cast<NumberNode*>(msb_e.get())) {
                        if (auto nl = dynamic_cast<NumberNode*>(lsb_e.get())) {
                            pw = (nm->aval >= nl->aval) ? (uint32_t)(nm->aval - nl->aval + 1) : (uint32_t)(nl->aval - nm->aval + 1);
                        }
                    }
                }
            }

            std::string pname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected port identifier in DPI import");

            if (match(TokenType::TOKEN_LBRACKET)) {
                if (match(TokenType::TOKEN_RBRACKET)) {
                    is_open_array = true;
                } else {
                    auto msb_e = parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in port range");
                    auto lsb_e = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in port range");
                }
            }

            PortNode port(pname, pdir, nullptr, nullptr, psigned, NetKind::WIRE);
            port.type_name = ptype;
            if (ptype == "chandle") {
                port.is_chandle = true;
            }
            port.is_open_array = is_open_array;
            port.msb_expr = std::make_unique<NumberNode>(pw > 0 ? pw - 1 : 0, 32);
            port.lsb_expr = std::make_unique<NumberNode>(0, 32);
            dpi_node->tf_ports.push_back(std::move(port));

            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
    }
    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing DPI import port list");
    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after DPI import declaration");

    return dpi_node;
}

std::unique_ptr<DpiExportNode> Parser::parse_dpi_export() {
    expect(TokenType::TOKEN_EXPORT, "Expected 'export'");
    if (current.type != TokenType::TOKEN_STRING || (current.text != "DPI-C" && current.text != "DPI")) {
        throw std::runtime_error(current.filename + ":" + std::to_string(current.line) + 
            ": error: Expected '\"DPI-C\"' or '\"DPI\"' in DPI export (found: '" + current.text + "')");
    }
    advance(); // consume string

    auto node = std::make_unique<DpiExportNode>();

    std::string first_ident = "";
    if (current.type == TokenType::TOKEN_FUNCTION) {
        node->is_task = false;
        advance();
        if (current.type != TokenType::TOKEN_IDENTIFIER) {
            throw std::runtime_error(current.filename + ":" + std::to_string(current.line) + 
                ": error: Expected function identifier in DPI export");
        }
        node->sv_name = current.text;
        node->c_name = current.text;
        advance();
    } else if (current.type == TokenType::TOKEN_TASK) {
        node->is_task = true;
        advance();
        if (current.type != TokenType::TOKEN_IDENTIFIER) {
            throw std::runtime_error(current.filename + ":" + std::to_string(current.line) + 
                ": error: Expected task identifier in DPI export");
        }
        node->sv_name = current.text;
        node->c_name = current.text;
        advance();
    } else if (current.type == TokenType::TOKEN_IDENTIFIER) {
        first_ident = current.text;
        advance();
        expect(TokenType::TOKEN_ASSIGN, "Expected '=' after C identifier in DPI export");
        if (current.type == TokenType::TOKEN_FUNCTION) {
            node->is_task = false;
        } else if (current.type == TokenType::TOKEN_TASK) {
            node->is_task = true;
        } else {
            throw std::runtime_error(current.filename + ":" + std::to_string(current.line) + 
                ": error: Expected 'function' or 'task' in DPI export");
        }
        advance();
        if (current.type != TokenType::TOKEN_IDENTIFIER) {
            throw std::runtime_error(current.filename + ":" + std::to_string(current.line) + 
                ": error: Expected function or task identifier in DPI export");
        }
        node->c_name = first_ident;
        node->sv_name = current.text;
        advance();
    } else {
        throw std::runtime_error(current.filename + ":" + std::to_string(current.line) + 
            ": error: Expected 'function', 'task', or C identifier in DPI export");
    }

    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after DPI export declaration");
    return node;
}

std::shared_ptr<PackageNode> Parser::parse_package() {
    expect(TokenType::TOKEN_PACKAGE, "Expected 'package'");
    auto pkg = std::make_shared<PackageNode>();
    pkg->time_unit = !global_timeunit_val.empty() ? global_timeunit_val : Lexer::global_time_unit_str;
    pkg->time_precision = !global_timeprec_val.empty() ? global_timeprec_val : Lexer::global_time_precision_str;
    pkg->defined_in_file = current.filename;
    pkg->defined_on_line = current.line;
    pkg->name = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected package name");
    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after package name");

    global_packages[pkg->name] = pkg;
    current_parsing_package = pkg;

    while (current.type != TokenType::TOKEN_ENDPACKAGE && current.type != TokenType::TOKEN_EOF) {
        if (current.type == TokenType::TOKEN_SPECIFY) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: 'specify' block is not permitted inside package per IEEE 1800-2023 section 32");
        }
        if (current.type == TokenType::TOKEN_SYS_SETUP || current.type == TokenType::TOKEN_SYS_HOLD ||
            current.type == TokenType::TOKEN_SYS_WIDTH || current.type == TokenType::TOKEN_SYS_RECOVERY) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: timing check system task '" + current.text + "' is not permitted inside package per IEEE 1800-2023 section 31.8 & 32.7");
        }
        if (current.type == TokenType::TOKEN_FATAL || current.type == TokenType::TOKEN_ERROR ||
            current.type == TokenType::TOKEN_WARNING || current.type == TokenType::TOKEN_INFO) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: severity system task '" + current.text + "' is not permitted in package scope per IEEE 1800-2023 section 20.10");
        }
        if (current.type == TokenType::TOKEN_MODULE || 
            (current.type == TokenType::TOKEN_INTERFACE && peek_token().type != TokenType::TOKEN_CLASS)) {
            std::string loc = current.filename.empty() 
                ? ("line " + std::to_string(current.line)) 
                : (current.filename + ":" + std::to_string(current.line));
            throw std::runtime_error(loc + ": error: [CAS-DTINPCIL] Module or interface declarations and instantiations are illegal in package scope");
        }

        if (current.type == TokenType::TOKEN_PARAMETER || current.type == TokenType::TOKEN_LOCALPARAM) {
            advance();
            while (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
                   current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_INTEGER ||
                   current.type == TokenType::TOKEN_SIGNED || current.type == TokenType::TOKEN_UNSIGNED || 
                   current.type == TokenType::TOKEN_BIT ||
                   (current.type == TokenType::TOKEN_IDENTIFIER && 
                    (current.text == "int" || current.text == "byte" || current.text == "shortint" || 
                     current.text == "longint" || current.text == "unsigned" || current.text == "signed" ||
                     current.text == "string" || current.text == "real" || current.text == "chandle" ||
                     peek_token().type == TokenType::TOKEN_IDENTIFIER ||
                     peek_token().type == TokenType::TOKEN_UNSIGNED ||
                     peek_token().type == TokenType::TOKEN_SIGNED))) {
                advance();
                while (current.type == TokenType::TOKEN_LBRACKET) {
                    advance();
                    parse_expression();
                    if (match(TokenType::TOKEN_COLON)) {
                        parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
                }
            }
            std::vector<ArrayDimInfo> pdims;
            while (match(TokenType::TOKEN_LBRACKET)) {
                auto l = parse_expression();
                int64_t left_v = 0, right_v = 0;
                if (match(TokenType::TOKEN_COLON)) {
                    auto r = parse_expression();
                    left_v = (int64_t)eval_const_expr_with_params(l.get(), pkg->param_constants);
                    right_v = (int64_t)eval_const_expr_with_params(r.get(), pkg->param_constants);
                }
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
                pdims.push_back({left_v, right_v, false, false, false});
            }

            while (true) {
                std::string pname = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter identifier");
                while (match(TokenType::TOKEN_LBRACKET)) {
                    parse_expression();
                    if (match(TokenType::TOKEN_COLON)) {
                        parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
                }
                expect(TokenType::TOKEN_EQUAL, "Expected '=' in parameter assignment");
                auto val_expr = parse_expression();

                uint64_t val = eval_const_expr_with_params(val_expr.get(), pkg->param_constants);
                pkg->param_constants[pname] = val;

                if (!pdims.empty()) {
                    VariableDimMetadata vdm;
                    vdm.name = pname;
                    vdm.dimensions = pdims;
                    vdm.is_vector_or_array = true;
                    pkg->type_dimensions[pname] = vdm;
                    pkg->type_dimensions[pkg->name + "::" + pname] = vdm;
                }

                ParameterDef pdef;
                pdef.name = pname;
                pdef.default_expr = std::move(val_expr);
                pkg->parameters.push_back(std::move(pdef));

                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after parameter declaration");
            continue;
        }

        if (match(TokenType::TOKEN_EVENT)) {
            while (true) {
                std::string ev_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected event identifier in package");
                pkg->events.push_back(ev_name);
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after event declaration");
            continue;
        }

        if (current.type == TokenType::TOKEN_TYPEDEF) {
            auto td = parse_typedef();
            if (td) {
                if (td->enum_type) {
                    pkg->enum_typedefs[td->type_name] = td->enum_type;
                    for (const auto& m : td->enum_type->members) {
                        pkg->enum_constant_map[m.first] = m.second;
                    }
                    VariableDimMetadata edim;
                    edim.name = td->type_name;
                    edim.dimensions.push_back({(int64_t)td->enum_type->base_width - 1, 0, false, false, false});
                    edim.is_vector_or_array = true;
                    edim.unpacked_dims = 0;
                    pkg->type_dimensions[td->type_name] = edim;
                    global_type_dimensions[td->type_name] = edim;
                    global_type_dimensions[pkg->name + "::" + td->type_name] = edim;
                }
                if (td->struct_type) {
                    pkg->struct_typedefs[td->type_name] = td->struct_type;
                    VariableDimMetadata sdim;
                    sdim.name = td->type_name;
                    sdim.dimensions.push_back({(int64_t)td->struct_type->total_width - 1, 0, false, false, false});
                    sdim.is_vector_or_array = true;
                    sdim.unpacked_dims = 0;
                    pkg->type_dimensions[td->type_name] = sdim;
                    global_type_dimensions[td->type_name] = sdim;
                    global_type_dimensions[pkg->name + "::" + td->type_name] = sdim;
                }
                if (!td->base_type.empty()) {
                    pkg->simple_typedefs[td->type_name] = {td->type_name, td->base_type, td->width, td->is_signed};
                }
                pkg->typedefs.push_back(std::move(td));
            }
            continue;
        }

        bool has_pkg_virt_qual = false;
        bool has_pkg_stat_qual = false;
        bool has_pkg_ext_qual = false;
        if (current.type == TokenType::TOKEN_VIRTUAL && peek_token().type != TokenType::TOKEN_CLASS && peek_token().type != TokenType::TOKEN_INTERFACE) {
            has_pkg_virt_qual = true;
            advance();
        }
        if (current.type == TokenType::TOKEN_STATIC) {
            has_pkg_stat_qual = true;
            advance();
        }
        if (current.type == TokenType::TOKEN_EXTERN) {
            has_pkg_ext_qual = true;
            advance();
        }

        if (match(TokenType::TOKEN_FUNCTION)) {
            auto fn = parse_function();
            if (fn) {
                if (fn->name.find("::") != std::string::npos) {
                    if (has_pkg_virt_qual || has_pkg_stat_qual || has_pkg_ext_qual || fn->is_static) {
                        throw std::runtime_error("[CAS-DTINPCIL] Out-of-body method implementation cannot specify qualifiers ('virtual', 'static', 'extern') per IEEE 1800-2023 section 8.24: '" + fn->name + "'");
                    }
                    bind_out_of_body_function(std::move(fn));
                } else {
                    fn->scope = pkg->name;
                    pkg->function_map[fn->name] = fn.get();
                    pkg->functions.push_back(std::move(fn));
                }
            }
            continue;
        }

        if (match(TokenType::TOKEN_TASK)) {
            auto t = parse_task();
            if (t) {
                if (t->name.find("::") != std::string::npos) {
                    if (has_pkg_virt_qual || has_pkg_stat_qual || has_pkg_ext_qual || t->is_static) {
                        throw std::runtime_error("[CAS-DTINPCIL] Out-of-body task implementation cannot specify qualifiers ('virtual', 'static', 'extern') per IEEE 1800-2023 section 8.24: '" + t->name + "'");
                    }
                    bind_out_of_body_task(std::move(t));
                } else {
                    t->scope = pkg->name;
                    pkg->task_map[t->name] = t.get();
                    pkg->tasks.push_back(std::move(t));
                }
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_VIRTUAL && peek_token().type == TokenType::TOKEN_CLASS) {
            advance(); // consume 'virtual'
            auto cls = parse_class(false, true);
            if (cls) {
                std::shared_ptr<ClassNode> scls = std::move(cls);
                pkg->class_map[scls->name] = scls.get();
                pkg->classes.push_back(scls);
                class_map[scls->name] = scls;
                class_map[pkg->name + "::" + scls->name] = scls;
                global_classes.push_back(scls);
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_CONSTRAINT) {
            advance();
            if (match(TokenType::TOKEN_STATIC)) {}
            std::string class_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected class name in out-of-block constraint");
            expect(TokenType::TOKEN_COLON_COLON, "Expected '::' after class name");
            std::string cname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected constraint name");
            ClassNode* target_cls = find_class(class_name);
            if (target_cls) {
                auto it_c = target_cls->constraint_map.find(cname);
                if (it_c != target_cls->constraint_map.end()) {
                    it_c->second->is_extern = false;
                }
            }
            expect(TokenType::TOKEN_LBRACE, "Expected '{' after constraint name");
            int cdepth = 1;
            while (cdepth > 0 && current.type != TokenType::TOKEN_EOF) {
                if (current.type == TokenType::TOKEN_LBRACE) cdepth++;
                else if (current.type == TokenType::TOKEN_RBRACE) cdepth--;
                advance();
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_INTERFACE && peek_token().type == TokenType::TOKEN_CLASS) {
            advance(); // consume 'interface'
            auto cls = parse_class(true);
            if (cls) {
                std::shared_ptr<ClassNode> scls = std::move(cls);
                pkg->class_map[scls->name] = scls.get();
                pkg->classes.push_back(scls);
                class_map[scls->name] = scls;
                class_map[pkg->name + "::" + scls->name] = scls;
                global_classes.push_back(scls);
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_CONSTRAINT) {
            advance();
            if (match(TokenType::TOKEN_STATIC)) {}
            std::string class_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected class name in out-of-block constraint");
            expect(TokenType::TOKEN_COLON_COLON, "Expected '::' after class name");
            std::string cname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected constraint name");
            ClassNode* target_cls = find_class(class_name);
            if (target_cls) {
                auto it_c = target_cls->constraint_map.find(cname);
                if (it_c != target_cls->constraint_map.end()) {
                    it_c->second->is_extern = false;
                }
            }
            expect(TokenType::TOKEN_LBRACE, "Expected '{' after constraint name");
            int cdepth = 1;
            while (cdepth > 0 && current.type != TokenType::TOKEN_EOF) {
                if (current.type == TokenType::TOKEN_LBRACE) cdepth++;
                else if (current.type == TokenType::TOKEN_RBRACE) cdepth--;
                advance();
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_CLASS) {
            auto cls = parse_class();
            if (cls) {
                std::shared_ptr<ClassNode> scls = std::move(cls);
                pkg->class_map[scls->name] = scls.get();
                pkg->classes.push_back(scls);
                class_map[scls->name] = scls;
                class_map[pkg->name + "::" + scls->name] = scls;
                global_classes.push_back(scls);
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_CONSTRAINT) {
            advance();
            if (match(TokenType::TOKEN_STATIC)) {}
            std::string class_name = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected class name in out-of-block constraint");
            expect(TokenType::TOKEN_COLON_COLON, "Expected '::' after class name");
            std::string cname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected constraint name");
            ClassNode* target_cls = find_class(class_name);
            if (target_cls) {
                auto it_c = target_cls->constraint_map.find(cname);
                if (it_c != target_cls->constraint_map.end()) {
                    it_c->second->is_extern = false;
                }
            }
            expect(TokenType::TOKEN_LBRACE, "Expected '{' after constraint name");
            int cdepth = 1;
            while (cdepth > 0 && current.type != TokenType::TOKEN_EOF) {
                if (current.type == TokenType::TOKEN_LBRACE) cdepth++;
                else if (current.type == TokenType::TOKEN_RBRACE) cdepth--;
                advance();
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_LET) {
            auto let_decl = parse_let_declaration();
            if (let_decl) {
                pkg->let_decls.push_back(let_decl);
                pkg->let_decl_map[let_decl->name] = let_decl;
                global_let_decls[pkg->name + "::" + let_decl->name] = let_decl;
            }
            continue;
        }

        if (match(TokenType::TOKEN_TIMEUNIT)) {
            std::string t_unit = current.text;
            advance();
            if (current.type == TokenType::TOKEN_IDENTIFIER) {
                t_unit += current.text;
                advance();
            }
            std::string t_prec = "";
            if (match(TokenType::TOKEN_SLASH)) {
                t_prec = current.text;
                advance();
                if (current.type == TokenType::TOKEN_IDENTIFIER) {
                    t_prec += current.text;
                    advance();
                }
            }
            if (!validate_time_literal_str(t_unit)) {
                throw std::runtime_error("[CAS-TIME] Invalid time literal '" + t_unit + "' in timeunit per IEEE 1800-2023 §3.14.2");
            }
            if (!t_prec.empty() && !validate_time_literal_str(t_prec)) {
                throw std::runtime_error("[CAS-TIME] Invalid time literal '" + t_prec + "' in timeunit precision per IEEE 1800-2023 §3.14.2");
            }
            if (!global_timeunit_val.empty() && global_timeunit_val != t_unit) {
                throw std::runtime_error("[CAS-TIME] Repeated timeunit declaration '" + t_unit + "' differs from previous '" + global_timeunit_val + "' per IEEE 1800-2023 §3.14.2");
            }
            global_timeunit_val = t_unit;
            if (!t_prec.empty()) {
                if (!global_timeprec_val.empty() && global_timeprec_val != t_prec) {
                    throw std::runtime_error("[CAS-TIME] Repeated timeprecision declaration '" + t_prec + "' differs from previous '" + global_timeprec_val + "' per IEEE 1800-2023 §3.14.2");
                }
                global_timeprec_val = t_prec;
            }
            while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_EOF) advance();
            match(TokenType::TOKEN_SEMICOLON);
            continue;
        }
        if (match(TokenType::TOKEN_TIMEPRECISION)) {
            std::string t_prec = current.text;
            advance();
            if (current.type == TokenType::TOKEN_IDENTIFIER) {
                t_prec += current.text;
                advance();
            }
            if (!validate_time_literal_str(t_prec)) {
                throw std::runtime_error("[CAS-TIME] Invalid time literal '" + t_prec + "' in timeprecision per IEEE 1800-2023 §3.14.2");
            }
            if (!global_timeprec_val.empty() && global_timeprec_val != t_prec) {
                throw std::runtime_error("[CAS-TIME] Repeated timeprecision declaration '" + t_prec + "' differs from previous '" + global_timeprec_val + "' per IEEE 1800-2023 §3.14.2");
            }
            global_timeprec_val = t_prec;
            while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_EOF) advance();
            match(TokenType::TOKEN_SEMICOLON);
            continue;
        }

        if (current.type == TokenType::TOKEN_COVERGROUP) {
            auto cg = parse_covergroup();
            if (cg) {
                std::string cname = cg->name;
                std::shared_ptr<CovergroupNode> scg = std::move(cg);
                covergroup_map[cname] = scg;
                covergroup_map[pkg->name + "::" + cname] = scg;
                global_covergroups.push_back(scg);
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_IMPORT) {
            if (peek_token().type == TokenType::TOKEN_STRING) {
                auto dpi = parse_dpi_import();
                if (dpi) {
                    std::shared_ptr<DpiImportNode> sdpi = std::move(dpi);
                    pkg->dpi_import_map[sdpi->sv_name] = sdpi.get();
                    pkg->dpi_imports.push_back(sdpi);
                }
            } else {
                advance(); // consume 'import'
                while (true) {
                    std::string imp_pkg_name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected package name in import");
                    expect(TokenType::TOKEN_COLON_COLON, "Expected '::' after package name in import");
                    if (match(TokenType::TOKEN_STAR)) {
                        pkg->wildcard_imports.push_back(imp_pkg_name);
                    } else {
                        std::string item_name = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected item name or '*' in import");
                        pkg->explicit_imports[item_name] = {imp_pkg_name, item_name};
                    }
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after import declaration");
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_EXPORT) {
            auto exp = parse_dpi_export();
            if (exp) {
                std::shared_ptr<DpiExportNode> sexp = std::move(exp);
                pkg->dpi_export_map[sexp->c_name] = sexp.get();
                pkg->dpi_export_map[sexp->sv_name] = sexp.get();
                pkg->dpi_exports.push_back(sexp);
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_IDENTIFIER) {
            Token next = peek_token();
            if (next.type == TokenType::TOKEN_HASH) {
                std::string loc = current.filename.empty() 
                    ? ("line " + std::to_string(current.line)) 
                    : (current.filename + ":" + std::to_string(current.line));
                throw std::runtime_error(loc + ": error: [CAS-DTINPCIL] Module or interface declarations and instantiations are illegal in package scope");
            }
            if (next.type == TokenType::TOKEN_IDENTIFIER) {
                Token after_next = peek_token_n(2);
                if (after_next.type == TokenType::TOKEN_LPAREN) {
                    std::string loc = current.filename.empty() 
                        ? ("line " + std::to_string(current.line)) 
                        : (current.filename + ":" + std::to_string(current.line));
                    throw std::runtime_error(loc + ": error: [CAS-DTINPCIL] Module or interface declarations and instantiations are illegal in package scope");
                }
            }
        }

        if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE || 
            current.type == TokenType::TOKEN_WAND  || current.type == TokenType::TOKEN_WOR  || 
            current.type == TokenType::TOKEN_TRI   || current.type == TokenType::TOKEN_REG  || 
            current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_BIT ||
            current.type == TokenType::TOKEN_SIGNED || current.type == TokenType::TOKEN_UNSIGNED ||
            (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint" || current.text == "real" || is_type_identifier(current.text) || is_class(current.text)))) {
            
            NetKind pkind = NetKind::VAR;
            if (current.type == TokenType::TOKEN_WIRE) pkind = NetKind::WIRE;
            else if (current.type == TokenType::TOKEN_WAND) pkind = NetKind::WAND;
            else if (current.type == TokenType::TOKEN_WOR) pkind = NetKind::WOR;
            else if (current.type == TokenType::TOKEN_TRI) pkind = NetKind::TRI;
            std::string tname = current.text;
            bool is_class_sig = is_class(tname);
            if (is_class_sig) pkind = NetKind::VAR;
            bool is_signed_sig = (current.type == TokenType::TOKEN_SIGNED);
            advance();

            if (match(TokenType::TOKEN_SIGNED)) {
                is_signed_sig = true;
            } else if (match(TokenType::TOKEN_UNSIGNED)) {
                is_signed_sig = false;
            }

            std::vector<AstDimension> packed_dims;
            std::unique_ptr<ExpressionNode> msb = nullptr;
            std::unique_ptr<ExpressionNode> lsb = nullptr;
            while (match(TokenType::TOKEN_LBRACKET)) {
                auto l = parse_expression();
                std::unique_ptr<ExpressionNode> r = nullptr;
                if (match(TokenType::TOKEN_COLON)) {
                    r = parse_expression();
                }
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in signal range");
                AstDimension ad;
                ad.left_expr = l->clone();
                ad.right_expr = r ? r->clone() : nullptr;
                ad.is_single = (r == nullptr);
                packed_dims.push_back(std::move(ad));
                if (!msb) {
                    msb = std::move(l);
                    lsb = std::move(r);
                }
            }

            while (true) {
                std::string sname = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier in package");
                PortNode pnode(sname, "internal", msb ? msb->clone() : nullptr, lsb ? lsb->clone() : nullptr, is_signed_sig, pkind);
                pnode.type_name = tname;
                if (is_class_sig) {
                    pnode.is_class_handle = true;
                    pnode.class_type_name = tname;
                }
                pnode.packed_dimensions = packed_dims;
                while (match(TokenType::TOKEN_LBRACKET)) {
                    if (match(TokenType::TOKEN_DOLLAR)) {
                        pnode.is_queue = true;
                        if (match(TokenType::TOKEN_COLON)) {
                            pnode.queue_bound_expr = parse_expression();
                        }
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                        break;
                    } else if (match(TokenType::TOKEN_RBRACKET)) {
                        pnode.is_dynamic_array = true;
                        break;
                    } else if (is_assoc_key_token(current)) {
                        pnode.is_associative_array = true;
                        parse_assoc_key_type(pnode.assoc_key_type, pnode.assoc_key_width);
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in associative array dimension");
                        break;
                    } else {
                        pnode.is_array = true;
                        auto l = parse_expression();
                        std::unique_ptr<ExpressionNode> r = nullptr;
                        bool is_single = true;
                        if (match(TokenType::TOKEN_COLON)) {
                            r = parse_expression();
                            is_single = false;
                        }
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in array dimension");
                        AstDimension ad;
                        ad.left_expr = std::move(l);
                        ad.right_expr = std::move(r);
                        ad.is_single = is_single;
                        pnode.unpacked_dimensions.push_back(std::move(ad));
                    }
                }

                if (match(TokenType::TOKEN_EQUAL)) {
                    auto init_expr = parse_expression();
                    pnode.default_value = init_expr->clone();
                }

                pkg->signal_map[sname] = pkg->signals.size();
                pkg->signals.push_back(std::move(pnode));
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after package signal declaration");
            continue;
        }

        advance();
    }

    expect(TokenType::TOKEN_ENDPACKAGE, "Expected 'endpackage'");
    if (match(TokenType::TOKEN_COLON)) {
        if (current.type == TokenType::TOKEN_IDENTIFIER) {
            if (!pkg->name.empty() && current.text != pkg->name) {
                throw std::runtime_error("End label '" + current.text + "' does not match package name '" + pkg->name + "'");
            }
            advance();
        }
    }
    current_parsing_package = nullptr;
    return pkg;
}

std::shared_ptr<ConfigNode> Parser::parse_config() {
    expect(TokenType::TOKEN_CONFIG, "Expected 'config'");
    auto cfg = std::make_shared<ConfigNode>();
    cfg->defined_in_file = current.filename;
    cfg->defined_on_line = current.line;
    cfg->name = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected config identifier");
    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after config identifier");

    global_configs[cfg->name] = cfg;

    while (current.type != TokenType::TOKEN_ENDCONFIG && current.type != TokenType::TOKEN_EOF) {
        if (current.type == TokenType::TOKEN_INITIAL ||
            current.type == TokenType::TOKEN_ALWAYS ||
            current.type == TokenType::TOKEN_ALWAYS_COMB ||
            current.type == TokenType::TOKEN_ALWAYS_FF ||
            current.type == TokenType::TOKEN_ALWAYS_LATCH ||
            current.type == TokenType::TOKEN_ASSIGN ||
            current.type == TokenType::TOKEN_IF ||
            current.type == TokenType::TOKEN_FOR ||
            current.type == TokenType::TOKEN_WHILE ||
            current.type == TokenType::TOKEN_REPEAT ||
            current.type == TokenType::TOKEN_FOREVER) {
            std::string loc = current.filename.empty() 
                ? ("line " + std::to_string(current.line)) 
                : (current.filename + ":" + std::to_string(current.line));
            throw std::runtime_error(loc + ": error: [CAS-DTINPCIL] Procedural blocks, statements, and continuous assignments are illegal in config scope");
        }

        if (match(TokenType::TOKEN_DESIGN)) {
            std::string first_id = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected design top module identifier");
            if (match(TokenType::TOKEN_DOT)) {
                cfg->design_lib = first_id;
                cfg->design_top_module = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected top module identifier after '.' in design clause");
            } else {
                cfg->design_top_module = first_id;
            }
            while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_EOF) {
                advance();
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after design statement");
            continue;
        }

        if (match(TokenType::TOKEN_DEFAULT)) {
            expect(TokenType::TOKEN_LIBLIST, "Expected 'liblist' after 'default'");
            while (current.type != TokenType::TOKEN_SEMICOLON && current.type != TokenType::TOKEN_EOF) {
                if (current.type == TokenType::TOKEN_IDENTIFIER) {
                    cfg->default_liblist.push_back(current.text);
                }
                advance();
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after default liblist");
            continue;
        }

        if (match(TokenType::TOKEN_INSTANCE)) {
            auto rule = std::make_unique<ConfigRuleNode>();
            rule->rule_type = ConfigRuleType::INSTANCE;

            std::string hier_path = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected instance path");
            while (match(TokenType::TOKEN_DOT)) {
                hier_path += "." + current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in instance path");
            }
            rule->target_path = hier_path;

            expect(TokenType::TOKEN_USE, "Expected 'use' after instance path");

            std::string first_id = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected cell identifier after 'use'");
            if (match(TokenType::TOKEN_DOT)) {
                rule->lib_name = first_id;
                rule->cell_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected cell name after '.'");
            } else {
                rule->cell_name = first_id;
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after instance rule");

            cfg->instance_cell_map[rule->target_path] = rule->cell_name;
            cfg->rules.push_back(std::move(rule));
            continue;
        }

        if (match(TokenType::TOKEN_CELL)) {
            auto rule = std::make_unique<ConfigRuleNode>();
            rule->rule_type = ConfigRuleType::CELL;

            std::string cell_target = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected cell identifier");
            rule->target_path = cell_target;

            expect(TokenType::TOKEN_USE, "Expected 'use' after cell identifier");

            std::string first_id = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected cell identifier after 'use'");
            if (match(TokenType::TOKEN_DOT)) {
                rule->lib_name = first_id;
                rule->cell_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected cell name after '.'");
            } else {
                rule->cell_name = first_id;
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after cell rule");

            cfg->cell_rebind_map[rule->target_path] = rule->cell_name;
            cfg->rules.push_back(std::move(rule));
            continue;
        }

        if (match(TokenType::TOKEN_SEMICOLON)) {
            continue;
        }

        std::string loc = current.filename.empty() 
            ? ("line " + std::to_string(current.line)) 
            : (current.filename + ":" + std::to_string(current.line));
        throw std::runtime_error(loc + ": error: Unexpected token in config declaration: '" + current.text + "'");
    }

    expect(TokenType::TOKEN_ENDCONFIG, "Expected 'endconfig'");
    if (match(TokenType::TOKEN_COLON)) {
        if (current.type == TokenType::TOKEN_IDENTIFIER) {
            advance();
        }
    }
    return cfg;
}

std::shared_ptr<LoopGenerateNode> Parser::parse_generate_for(ModuleNode* mod) {
    expect(TokenType::TOKEN_FOR, "Expected 'for' in loop generate");
    expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'for'");
    bool inline_genvar = false;
    if (match(TokenType::TOKEN_GENVAR)) {
        inline_genvar = true;
    }
    std::string gname = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected genvar identifier in for loop header");
    if (!inline_genvar) {
        mod->genvars.insert(gname);
    }
    expect(TokenType::TOKEN_EQUAL, "Expected '=' in for loop header");
    auto init_e = parse_expression();
    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after for loop init");
    auto cond_e = parse_expression();
    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after for loop condition");
    
    std::string step_op = "=";
    std::unique_ptr<ExpressionNode> step_e = nullptr;
    if (match(TokenType::TOKEN_PLUS_PLUS)) {
        step_op = "++";
        expect(TokenType::TOKEN_IDENTIFIER, "Expected genvar name after '++'");
    } else if (match(TokenType::TOKEN_MINUS_MINUS)) {
        step_op = "--";
        expect(TokenType::TOKEN_IDENTIFIER, "Expected genvar name after '--'");
    } else {
        std::string step_target = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected genvar identifier in step");
        if (match(TokenType::TOKEN_PLUS_PLUS)) {
            step_op = "++";
        } else if (match(TokenType::TOKEN_MINUS_MINUS)) {
            step_op = "--";
        } else if (match(TokenType::TOKEN_PLUS_EQUAL)) {
            step_op = "+=";
            step_e = parse_expression();
        } else if (match(TokenType::TOKEN_MINUS_EQUAL)) {
            step_op = "-=";
            step_e = parse_expression();
        } else if (match(TokenType::TOKEN_EQUAL)) {
            step_op = "=";
            step_e = parse_expression();
        }
    }
    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing for loop header");

    auto loop = std::make_shared<LoopGenerateNode>();
    loop->genvar_name = gname;
    loop->init_expr = std::move(init_e);
    loop->cond_expr = std::move(cond_e);
    loop->step_op = step_op;
    loop->step_expr = std::move(step_e);

    if (match(TokenType::TOKEN_BEGIN)) {
        if (match(TokenType::TOKEN_COLON)) {
            loop->block_label = current.text;
            loop->body->label = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected block label after ':'");
        }
        while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
            parse_generate_item(mod, loop->body.get());
        }
        expect(TokenType::TOKEN_END, "Expected 'end' closing generate block");
        if (match(TokenType::TOKEN_COLON)) {
            match(TokenType::TOKEN_IDENTIFIER);
        }
    } else {
        parse_generate_item(mod, loop->body.get());
    }
    return loop;
}

std::shared_ptr<IfGenerateNode> Parser::parse_generate_if(ModuleNode* mod) {
    expect(TokenType::TOKEN_IF, "Expected 'if' in conditional generate");
    expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'if'");
    auto cond_e = parse_expression();
    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing if condition");

    auto if_node = std::make_shared<IfGenerateNode>();
    if_node->cond_expr = std::move(cond_e);

    if (match(TokenType::TOKEN_BEGIN)) {
        if (match(TokenType::TOKEN_COLON)) {
            if_node->then_label = current.text;
            if_node->then_block->label = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected block label after ':'");
        }
        while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
            parse_generate_item(mod, if_node->then_block.get());
        }
        expect(TokenType::TOKEN_END, "Expected 'end' closing generate block");
        if (match(TokenType::TOKEN_COLON)) {
            match(TokenType::TOKEN_IDENTIFIER);
        }
    } else {
        parse_generate_item(mod, if_node->then_block.get());
    }

    if (match(TokenType::TOKEN_ELSE)) {
        if_node->has_else = true;
        if (match(TokenType::TOKEN_BEGIN)) {
            if (match(TokenType::TOKEN_COLON)) {
                if_node->else_label = current.text;
                if_node->else_block->label = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected block label after ':'");
            }
            while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
                parse_generate_item(mod, if_node->else_block.get());
            }
            expect(TokenType::TOKEN_END, "Expected 'end' closing generate block");
            if (match(TokenType::TOKEN_COLON)) {
                match(TokenType::TOKEN_IDENTIFIER);
            }
        } else {
            parse_generate_item(mod, if_node->else_block.get());
        }
    }
    return if_node;
}

std::shared_ptr<CaseGenerateNode> Parser::parse_generate_case(ModuleNode* mod) {
    expect(TokenType::TOKEN_CASE, "Expected 'case' in case generate");
    expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'case'");
    auto sel_e = parse_expression();
    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing case select");

    auto case_node = std::make_shared<CaseGenerateNode>();
    case_node->select_expr = std::move(sel_e);

    while (current.type != TokenType::TOKEN_ENDCASE && current.type != TokenType::TOKEN_EOF) {
        CaseGenerateItem item;
        item.block = std::make_unique<GenerateBlock>();
        if (match(TokenType::TOKEN_DEFAULT)) {
            match(TokenType::TOKEN_COLON);
            item.is_default = true;
        } else {
            while (true) {
                item.match_exprs.push_back(parse_expression());
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_COLON, "Expected ':' after case item");
        }
        if (match(TokenType::TOKEN_BEGIN)) {
            if (match(TokenType::TOKEN_COLON)) {
                item.label = current.text;
                item.block->label = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected block label after ':'");
            }
            while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
                parse_generate_item(mod, item.block.get());
            }
            expect(TokenType::TOKEN_END, "Expected 'end' closing generate block");
            if (match(TokenType::TOKEN_COLON)) {
                match(TokenType::TOKEN_IDENTIFIER);
            }
        } else {
            parse_generate_item(mod, item.block.get());
        }
        case_node->items.push_back(std::move(item));
    }
    expect(TokenType::TOKEN_ENDCASE, "Expected 'endcase'");
    return case_node;
}

void Parser::parse_generate_item(ModuleNode* mod, GenerateBlock* blk) {
    if (match(TokenType::TOKEN_BEGIN)) {
        std::string gen_label = "";
        if (match(TokenType::TOKEN_COLON)) {
            gen_label = current.text;
            match(TokenType::TOKEN_IDENTIFIER);
        }
        while (current.type != TokenType::TOKEN_END && current.type != TokenType::TOKEN_EOF) {
            parse_generate_item(mod, blk);
        }
        expect(TokenType::TOKEN_END, "Expected 'end'");
        if (match(TokenType::TOKEN_COLON)) {
            if (current.type == TokenType::TOKEN_IDENTIFIER) {
                if (!gen_label.empty() && current.text != gen_label) {
                    throw std::runtime_error("Generate block end label '" + current.text + "' does not match begin label '" + gen_label + "'");
                }
                advance();
            }
        }
        return;
    }
    if (current.type == TokenType::TOKEN_TYPEDEF) {
        auto td = parse_typedef();
        if (td && td->enum_type) {
            for (const auto& m : td->enum_type->members) {
                ParameterDef pdef;
                pdef.name = m.first;
                pdef.default_expr = std::make_unique<NumberNode>(m.second.aval, m.second.bval, td->enum_type->base_width);
                mod->parameters.push_back(std::move(pdef));
            }
        }
        if (td && !td->base_type.empty()) {
            mod->simple_typedefs[td->type_name] = {td->type_name, td->base_type, td->width, td->is_signed};
        }
        mod->typedefs.push_back(std::move(td));
        return;
    }
    if (match(TokenType::TOKEN_FUNCTION)) {
        auto fn = parse_function();
        if (fn) {
            mod->functions.push_back(std::move(fn));
        }
        return;
    }
    if (match(TokenType::TOKEN_TASK)) {
        auto t = parse_task();
        if (t) {
            mod->tasks.push_back(std::move(t));
        }
        return;
    }
    if (match(TokenType::TOKEN_GENVAR)) {
        while (true) {
            std::string gname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected genvar identifier");
            mod->genvars.insert(gname);
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after genvar declaration");
        return;
    }
    if (current.type == TokenType::TOKEN_PARAMETER || current.type == TokenType::TOKEN_LOCALPARAM) {
        bool is_local = (current.type == TokenType::TOKEN_LOCALPARAM);
        advance();
        bool has_type = false;
        std::string ptype = "";
        bool is_signed = false;
        std::unique_ptr<ExpressionNode> p_msb;
        std::unique_ptr<ExpressionNode> p_lsb;
        bool has_range = false;
        while (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
               current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_INTEGER ||
               current.type == TokenType::TOKEN_SIGNED || current.type == TokenType::TOKEN_UNSIGNED || 
               current.type == TokenType::TOKEN_BIT ||
               (!has_type && is_type_identifier(current.text)) ||
               (!has_type && current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON_COLON)) {
            has_type = true;
            if (current.type == TokenType::TOKEN_SIGNED) is_signed = true;
            if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON_COLON) {
                ptype = current.text;
                advance();
                ptype += current.text;
                advance();
                ptype += current.text;
                advance();
            } else {
                if (ptype.empty()) ptype = current.text;
                advance();
            }
            while (current.type == TokenType::TOKEN_LBRACKET) {
                advance();
                p_msb = parse_expression();
                if (match(TokenType::TOKEN_COLON)) {
                    p_lsb = parse_expression();
                }
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
                has_range = true;
            }
        }
        while (match(TokenType::TOKEN_LBRACKET)) {
            p_msb = parse_expression();
            if (match(TokenType::TOKEN_COLON)) {
                p_lsb = parse_expression();
            }
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
            has_range = true;
        }
        while (true) {
            std::string pname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter identifier");
            while (match(TokenType::TOKEN_LBRACKET)) {
                parse_expression();
                if (match(TokenType::TOKEN_COLON)) {
                    parse_expression();
                }
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
            }
            expect(TokenType::TOKEN_EQUAL, "Expected '=' in parameter assignment");
            auto val_expr = parse_expression();
            ParameterDef pdef(pname, std::move(val_expr), is_local, ptype);
            if (has_range && p_msb) {
                pdef.has_range = true;
                pdef.msb_expr = p_msb->clone();
                if (p_lsb) pdef.lsb_expr = p_lsb->clone();
            }
            pdef.is_signed = is_signed;
            mod->parameters.push_back(std::move(pdef));
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after parameter declaration");
        return;
    }
    if (current.type == TokenType::TOKEN_UNIQUE || current.type == TokenType::TOKEN_UNIQUE0 || current.type == TokenType::TOKEN_PRIORITY) {
        std::string mod_name = current.text;
        advance();
        if (current.type == TokenType::TOKEN_IF) {
            auto node = parse_generate_if(mod);
            if (blk) blk->nested_generates.push_back(node);
            else mod->generate_nodes.push_back(node);
            return;
        } else if (current.type == TokenType::TOKEN_CASE) {
            auto node = parse_generate_case(mod);
            if (blk) blk->nested_generates.push_back(node);
            else mod->generate_nodes.push_back(node);
            return;
        } else {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: '" + mod_name + "' modifier can only prefix an 'if' or 'case' statement per IEEE 1800-2023 section 12.5");
        }
    }
    if (current.type == TokenType::TOKEN_FOR) {
        auto node = parse_generate_for(mod);
        if (blk) blk->nested_generates.push_back(node);
        else mod->generate_nodes.push_back(node);
        return;
    }
    if (current.type == TokenType::TOKEN_IF) {
        auto node = parse_generate_if(mod);
        if (blk) blk->nested_generates.push_back(node);
        else mod->generate_nodes.push_back(node);
        return;
    }
    if (current.type == TokenType::TOKEN_CASE) {
        auto node = parse_generate_case(mod);
        if (blk) blk->nested_generates.push_back(node);
        else mod->generate_nodes.push_back(node);
        return;
    }
    if (match(TokenType::TOKEN_GENERATE)) {
        while (current.type != TokenType::TOKEN_ENDGENERATE && current.type != TokenType::TOKEN_EOF) {
            parse_generate_item(mod, blk);
        }
        expect(TokenType::TOKEN_ENDGENERATE, "Expected 'endgenerate'");
        return;
    }
    if (match(TokenType::TOKEN_ASSIGN)) {
        if (match(TokenType::TOKEN_LPAREN)) {
            while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                advance();
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' after drive strength in assign");
        }
        if (match(TokenType::TOKEN_HASH)) {
            if (match(TokenType::TOKEN_LPAREN)) {
                parse_expression();
                expect(TokenType::TOKEN_RPAREN, "Expected ')' after delay in assign");
            } else {
                advance();
            }
        }
        if (match(TokenType::TOKEN_LBRACE)) {
            std::vector<std::string> concat_targets;
            std::vector<std::unique_ptr<ExpressionNode>> concat_target_exprs;
            while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                std::string t = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier in concatenation target list");
                while (match(TokenType::TOKEN_DOT)) {
                    t += "." + current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in concat target");
                }
                std::unique_ptr<ExpressionNode> target_expr = nullptr;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    auto idx_expr = parse_expression();
                    if (match(TokenType::TOKEN_PLUS_COLON)) {
                        auto w_expr = parse_expression();
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                        auto inode = std::make_unique<IndexSelectNode>(t, std::move(idx_expr), std::move(w_expr));
                        inode->is_plus_colon = true;
                        target_expr = std::move(inode);
                    } else if (match(TokenType::TOKEN_MINUS_COLON)) {
                        auto w_expr = parse_expression();
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                        auto inode = std::make_unique<IndexSelectNode>(t, std::move(idx_expr), std::move(w_expr));
                        inode->is_minus_colon = true;
                        target_expr = std::move(inode);
                    } else if (match(TokenType::TOKEN_COLON)) {
                        auto lsb_expr = parse_expression();
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                        auto inode = std::make_unique<IndexSelectNode>(t, std::move(idx_expr), std::move(lsb_expr));
                        inode->is_part_select = true;
                        target_expr = std::move(inode);
                    } else {
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                        target_expr = std::make_unique<IndexSelectNode>(t, std::move(idx_expr));
                    }
                } else {
                    target_expr = std::make_unique<IdentifierNode>(t);
                }
                concat_targets.push_back(t);
                concat_target_exprs.push_back(std::move(target_expr));
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_RBRACE, "Expected '}' closing concatenation target list");
            expect(TokenType::TOKEN_EQUAL, "Expected '=' in assign statement");
            auto rhs = parse_expression();
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assign");
            auto ca = std::make_unique<ContinuousAssignNode>("", std::move(rhs));
            ca->concat_targets = std::move(concat_targets);
            ca->concat_target_exprs = std::move(concat_target_exprs);
            if (blk) blk->continuous_assigns.push_back(std::move(ca));
            else mod->continuous_assigns.push_back(std::move(ca));
            return;
        }
        std::string target = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected target identifier in assign");
        while (match(TokenType::TOKEN_DOT)) {
            target += "." + current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in assign target");
        }
        std::unique_ptr<ExpressionNode> target_idx = nullptr;
        if (match(TokenType::TOKEN_LBRACKET)) {
            auto idx_expr = parse_expression();
            if (match(TokenType::TOKEN_PLUS_COLON)) {
                auto w_expr = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                auto inode = std::make_unique<IndexSelectNode>(target, std::move(idx_expr), std::move(w_expr));
                inode->is_plus_colon = true;
                target_idx = std::move(inode);
            } else if (match(TokenType::TOKEN_MINUS_COLON)) {
                auto w_expr = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                auto inode = std::make_unique<IndexSelectNode>(target, std::move(idx_expr), std::move(w_expr));
                inode->is_minus_colon = true;
                target_idx = std::move(inode);
            } else if (match(TokenType::TOKEN_COLON)) {
                auto lsb_expr = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                auto inode = std::make_unique<IndexSelectNode>(target, std::move(idx_expr), std::move(lsb_expr));
                inode->is_part_select = true;
                target_idx = std::move(inode);
            } else {
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                target_idx = std::make_unique<IndexSelectNode>(target, std::move(idx_expr));
            }
            if (match(TokenType::TOKEN_LBRACKET)) {
                if (auto inode = dynamic_cast<IndexSelectNode*>(target_idx.get())) {
                    inode->has_bit_select = true;
                    inode->bit_msb_expr = parse_expression();
                    if (match(TokenType::TOKEN_PLUS_COLON)) {
                        inode->bit_lsb_expr = parse_expression();
                        inode->is_plus_colon = true;
                    } else if (match(TokenType::TOKEN_MINUS_COLON)) {
                        inode->bit_lsb_expr = parse_expression();
                        inode->is_minus_colon = true;
                    } else if (match(TokenType::TOKEN_COLON)) {
                        inode->bit_lsb_expr = parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing bit-select in assign target");
                    while (match(TokenType::TOKEN_LBRACKET)) {
                        auto extra = parse_expression();
                        if (match(TokenType::TOKEN_COLON)) {
                            auto lsb = parse_expression();
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing part-select in assign target");
                            auto part_node = std::make_unique<IndexSelectNode>("", std::move(extra), std::move(lsb));
                            inode->extra_indices.push_back(std::move(part_node));
                        } else if (match(TokenType::TOKEN_PLUS_COLON)) {
                            auto w = parse_expression();
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing part-select in assign target");
                            auto part_node = std::make_unique<IndexSelectNode>("", std::move(extra), std::move(w));
                            part_node->is_plus_colon = true;
                            inode->extra_indices.push_back(std::move(part_node));
                        } else if (match(TokenType::TOKEN_MINUS_COLON)) {
                            auto w = parse_expression();
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing part-select in assign target");
                            auto part_node = std::make_unique<IndexSelectNode>("", std::move(extra), std::move(w));
                            part_node->is_minus_colon = true;
                            inode->extra_indices.push_back(std::move(part_node));
                        } else {
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' closing index in assign target");
                            inode->extra_indices.push_back(std::move(extra));
                        }
                    }
                }
            }
        }
        std::string member_name = "";
        std::unique_ptr<ExpressionNode> member_msb = nullptr;
        std::unique_ptr<ExpressionNode> member_lsb = nullptr;
        bool has_mem_sel = false;
        bool is_part_sel = false;
        while (match(TokenType::TOKEN_DOT)) {
            if (!member_name.empty()) member_name += ".";
            member_name += current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in assign target");
            if (match(TokenType::TOKEN_LBRACKET)) {
                has_mem_sel = true;
                member_msb = parse_expression();
                if (match(TokenType::TOKEN_COLON)) {
                    is_part_sel = true;
                    member_lsb = parse_expression();
                }
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in member select in assign target");
            }
        }
        expect(TokenType::TOKEN_EQUAL, "Expected '=' in assign statement");
        auto rhs = parse_expression();
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assign");
        auto ca = std::make_unique<ContinuousAssignNode>(target, std::move(rhs));
        ca->target_index_expr = std::move(target_idx);
        ca->member_name = member_name;
        ca->has_member_select = has_mem_sel;
        ca->is_part_select = is_part_sel;
        ca->member_msb_expr = std::move(member_msb);
        ca->member_lsb_expr = std::move(member_lsb);
        if (blk) blk->continuous_assigns.push_back(std::move(ca));
        else mod->continuous_assigns.push_back(std::move(ca));
        return;
    }
    if (match(TokenType::TOKEN_ALWAYS_FF)) {
        auto proc = parse_always_ff();
        proc->is_always_ff = true;
        if (blk) blk->processes.push_back(std::move(proc));
        else mod->processes.push_back(std::move(proc));
        return;
    }
    if (match(TokenType::TOKEN_ALWAYS_COMB)) {
        auto proc = std::make_unique<ProcessNode>(ProcessType::COMBINATIONAL);
        proc->is_always_comb = true;
        proc->body = parse_statement_or_block();
        if (blk) blk->processes.push_back(std::move(proc));
        else mod->processes.push_back(std::move(proc));
        return;
    }
    if (match(TokenType::TOKEN_ALWAYS_LATCH)) {
        auto proc = std::make_unique<ProcessNode>(ProcessType::COMBINATIONAL);
        proc->is_always_latch = true;
        proc->body = parse_statement_or_block();
        if (blk) blk->processes.push_back(std::move(proc));
        else mod->processes.push_back(std::move(proc));
        return;
    }
    if (match(TokenType::TOKEN_ALWAYS)) {
        if (current.type == TokenType::TOKEN_AT) {
            advance();
            if (match(TokenType::TOKEN_STAR)) {
                auto proc = std::make_unique<ProcessNode>(ProcessType::COMBINATIONAL);
                proc->body = parse_statement_or_block();
                if (blk) blk->processes.push_back(std::move(proc));
                else mod->processes.push_back(std::move(proc));
                return;
            }
            if (match(TokenType::TOKEN_LPAREN)) {
                if (match(TokenType::TOKEN_STAR)) {
                    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing sensitivity list");
                    auto proc = std::make_unique<ProcessNode>(ProcessType::COMBINATIONAL);
                    proc->body = parse_statement_or_block();
                    if (blk) blk->processes.push_back(std::move(proc));
                    else mod->processes.push_back(std::move(proc));
                    return;
                }
                auto proc = std::make_unique<ProcessNode>(ProcessType::SEQUENTIAL);
                while (true) {
                    EdgeType edge = EdgeType::LEVEL;
                    if (match(TokenType::TOKEN_POSEDGE)) edge = EdgeType::POSEDGE;
                    else if (match(TokenType::TOKEN_NEGEDGE)) edge = EdgeType::NEGEDGE;
                    else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "edge") {
                        advance();
                        edge = EdgeType::LEVEL;
                    }
                    if (current.type == TokenType::TOKEN_NOT || current.type == TokenType::TOKEN_TILDE ||
                        current.type == TokenType::TOKEN_AND || current.type == TokenType::TOKEN_OR_BIT) {
                        advance();
                    }
                    std::string sig = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected signal name in sensitivity list");
                    while (match(TokenType::TOKEN_DOT)) {
                        sig += "." + current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in sensitivity list");
                    }
                    while (match(TokenType::TOKEN_LBRACKET)) {
                        parse_expression();
                        if (match(TokenType::TOKEN_COLON)) parse_expression();
                        expect(TokenType::TOKEN_RBRACKET, "Expected ']' in sensitivity list select");
                    }
                    if (match(TokenType::TOKEN_IFF)) {
                        parse_expression();
                    } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "iff") {
                        advance();
                        parse_expression();
                    }
                    proc->sensitivity_list.push_back({edge, sig});
                    if (match(TokenType::TOKEN_OR) || match(TokenType::TOKEN_GATE_OR) || match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_RPAREN, "Expected ')' closing sensitivity list");
                proc->body = parse_statement_or_block();
                if (blk) blk->processes.push_back(std::move(proc));
                else mod->processes.push_back(std::move(proc));
            } else if (current.type == TokenType::TOKEN_IDENTIFIER) {
                auto proc = std::make_unique<ProcessNode>(ProcessType::SEQUENTIAL);
                std::string sig = current.text;
                advance();
                while (match(TokenType::TOKEN_DOT)) {
                    sig += "." + current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in sensitivity list");
                }
                proc->sensitivity_list.push_back({EdgeType::LEVEL, sig});
                proc->body = parse_statement_or_block();
                if (blk) blk->processes.push_back(std::move(proc));
                else mod->processes.push_back(std::move(proc));
            }
        } else {
            auto proc = std::make_unique<ProcessNode>(ProcessType::ALWAYS);
            proc->body = parse_statement_or_block();
            if (blk) blk->processes.push_back(std::move(proc));
            else mod->processes.push_back(std::move(proc));
        }
        return;
    }
    if (match(TokenType::TOKEN_INITIAL)) {
        auto proc = std::make_unique<ProcessNode>(ProcessType::INITIAL);
        proc->body = parse_statement_or_block();
        if (blk) blk->processes.push_back(std::move(proc));
        else mod->processes.push_back(std::move(proc));
        return;
    }
    if (match(TokenType::TOKEN_FINAL)) {
        in_final_procedure = true;
        auto proc = std::make_unique<ProcessNode>(ProcessType::FINAL);
        proc->body = parse_statement_or_block();
        in_final_procedure = false;
        if (blk) blk->processes.push_back(std::move(proc));
        else mod->processes.push_back(std::move(proc));
        return;
    }
    if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE || 
        current.type == TokenType::TOKEN_WAND  || current.type == TokenType::TOKEN_WOR  || 
        current.type == TokenType::TOKEN_TRI   || current.type == TokenType::TOKEN_REG  || 
        current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_BIT ||
        current.type == TokenType::TOKEN_SIGNED || current.type == TokenType::TOKEN_UNSIGNED ||
        (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint" || is_type_identifier(current.text)))) {
        
        NetKind pkind = NetKind::VAR;
        if (current.type == TokenType::TOKEN_WIRE) pkind = NetKind::WIRE;
        else if (current.type == TokenType::TOKEN_WAND) pkind = NetKind::WAND;
        else if (current.type == TokenType::TOKEN_WOR) pkind = NetKind::WOR;
        else if (current.type == TokenType::TOKEN_TRI) pkind = NetKind::TRI;
        std::string tname = current.text;
        bool is_signed_sig = (current.type == TokenType::TOKEN_SIGNED);
        advance();

        if (match(TokenType::TOKEN_SIGNED)) {
            is_signed_sig = true;
        } else if (match(TokenType::TOKEN_UNSIGNED)) {
            is_signed_sig = false;
        }

        std::vector<AstDimension> packed_dims;
        std::unique_ptr<ExpressionNode> msb = nullptr;
        std::unique_ptr<ExpressionNode> lsb = nullptr;
        while (match(TokenType::TOKEN_LBRACKET)) {
            auto l = parse_expression();
            std::unique_ptr<ExpressionNode> r = nullptr;
            if (match(TokenType::TOKEN_COLON)) {
                r = parse_expression();
            }
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in signal range");
            AstDimension ad;
            ad.left_expr = l->clone();
            ad.right_expr = r ? r->clone() : nullptr;
            ad.is_single = (r == nullptr);
            packed_dims.push_back(std::move(ad));
            if (!msb) {
                msb = std::move(l);
                lsb = std::move(r);
            }
        }

        while (true) {
            std::string sname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier");
            PortNode pnode(sname, "internal", msb ? msb->clone() : nullptr, lsb ? lsb->clone() : nullptr, is_signed_sig, pkind);
            pnode.type_name = tname;
            pnode.packed_dimensions = packed_dims;
            while (match(TokenType::TOKEN_LBRACKET)) {
                if (match(TokenType::TOKEN_DOLLAR)) {
                    pnode.is_queue = true;
                    if (match(TokenType::TOKEN_COLON)) {
                        pnode.queue_bound_expr = parse_expression();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                    break;
                } else if (match(TokenType::TOKEN_RBRACKET)) {
                    pnode.is_dynamic_array = true;
                    break;
                } else if (is_assoc_key_token(current)) {
                    pnode.is_associative_array = true;
                    parse_assoc_key_type(pnode.assoc_key_type, pnode.assoc_key_width);
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in associative array dimension");
                    break;
                } else {
                    pnode.is_array = true;
                    auto l = parse_expression();
                    std::unique_ptr<ExpressionNode> r = nullptr;
                    bool is_single = true;
                    if (match(TokenType::TOKEN_COLON)) {
                        r = parse_expression();
                        is_single = false;
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in array dimension");
                    AstDimension ad;
                    ad.left_expr = std::move(l);
                    ad.right_expr = std::move(r);
                    ad.is_single = is_single;
                    pnode.unpacked_dimensions.push_back(std::move(ad));
                }
            }

            if (match(TokenType::TOKEN_EQUAL)) {
                auto init_expr = parse_expression();
                pnode.default_value = init_expr->clone();
                if (pkind == NetKind::WIRE) {
                    auto ca = std::make_unique<ContinuousAssignNode>(sname, init_expr->clone());
                    if (blk) blk->continuous_assigns.push_back(std::move(ca));
                    else mod->continuous_assigns.push_back(std::move(ca));
                } else {
                    auto init_proc = std::make_unique<ProcessNode>(ProcessType::INITIAL);
                    init_proc->is_decl_init = true;
                    init_proc->body.push_back(std::make_unique<BlockingAssignNode>(sname, std::move(init_expr)));
                    if (blk) blk->processes.push_back(std::move(init_proc));
                    else mod->processes.push_back(std::move(init_proc));
                }
            }

            if (blk) blk->signals.push_back(std::move(pnode));
            else mod->signals.push_back(std::move(pnode));
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after signal declaration");
        return;
    }

    if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON) {
        std::string label = current.text;
        advance(); // ident
        advance(); // colon
        if (current.type == TokenType::TOKEN_ASSERT) {
            auto asrt = parse_assert_property(label);
            if (asrt) {
                mod->assertions.push_back(std::move(asrt));
            }
            return;
        }
    }

    if (current.type == TokenType::TOKEN_IDENTIFIER) {
        std::string mtype = current.text;
        advance();

        std::vector<ParamOverride> param_overrides;
        if (match(TokenType::TOKEN_HASH)) {
            expect(TokenType::TOKEN_LPAREN, "Expected '(' in parameter override list");
            while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
                if (match(TokenType::TOKEN_DOT)) {
                    std::string pname = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter name after '.'");
                    if (match(TokenType::TOKEN_LPAREN)) {
                        std::unique_ptr<ExpressionNode> pval = nullptr;
                        if (current.type != TokenType::TOKEN_RPAREN) {
                            pval = parse_expression();
                        }
                        expect(TokenType::TOKEN_RPAREN, "Expected ')' in named parameter override");
                        param_overrides.push_back({pname, std::move(pval)});
                    } else {
                        param_overrides.push_back({pname, std::make_unique<IdentifierNode>(pname)});
                    }
                } else {
                    param_overrides.push_back({"", parse_expression()});
                }
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing parameter override list");
        }

        std::string iname = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected instance identifier");
        bool is_instance_array = false;
        int64_t arr_msb = 0, arr_lsb = 0;
        if (match(TokenType::TOKEN_LBRACKET)) {
            is_instance_array = true;
            auto r_msb = parse_expression();
            if (auto num = dynamic_cast<NumberNode*>(r_msb.get())) arr_msb = num->aval;
            if (match(TokenType::TOKEN_COLON)) {
                auto r_lsb = parse_expression();
                if (auto num = dynamic_cast<NumberNode*>(r_lsb.get())) arr_lsb = num->aval;
            } else {
                arr_lsb = 0;
                arr_msb = arr_msb > 0 ? (arr_msb - 1) : 0;
            }
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in instance range");
        }
        if (match(TokenType::TOKEN_SEMICOLON)) {
            if (is_instance_array) {
                int64_t low = std::min(arr_msb, arr_lsb);
                int64_t high = std::max(arr_msb, arr_lsb);
                for (int64_t idx = low; idx <= high; ++idx) {
                    std::string elem_name = iname + "[" + std::to_string(idx) + "]";
                    auto inst = std::make_unique<InstantiationNode>(mtype, elem_name, false);
                    for (const auto& po : param_overrides) {
                        inst->param_overrides.push_back({po.param_name, po.expr ? po.expr->clone() : nullptr});
                    }
                    if (blk) blk->instantiations.push_back(std::move(inst));
                    else mod->instantiations.push_back(std::move(inst));
                }
            } else {
                auto inst = std::make_unique<InstantiationNode>(mtype, iname, false);
                inst->param_overrides = std::move(param_overrides);
                if (blk) blk->instantiations.push_back(std::move(inst));
                else mod->instantiations.push_back(std::move(inst));
            }
            return;
        }
        expect(TokenType::TOKEN_LPAREN, "Expected '(' in instance port connections");

        bool positional = false;
        std::vector<PortConnection> conns;
        while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
            if (match(TokenType::TOKEN_COMMA)) {
                positional = true;
                conns.push_back({"", nullptr});
                continue;
            }
            if (match(TokenType::TOKEN_DOT)) {
                if (match(TokenType::TOKEN_STAR)) {
                    conns.push_back({"*", nullptr});
                } else {
                    std::string port_name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected port name after '.'");
                    if (match(TokenType::TOKEN_LPAREN)) {
                        std::unique_ptr<ExpressionNode> expr = nullptr;
                        if (current.type != TokenType::TOKEN_RPAREN) {
                            expr = parse_expression();
                        }
                        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing port connection");
                        conns.push_back({port_name, std::move(expr)});
                    } else {
                        PortConnection pc;
                        pc.port_name = port_name;
                        pc.expr = std::make_unique<IdentifierNode>(port_name);
                        pc.is_implicit_name_only = true;
                        conns.push_back(std::move(pc));
                    }
                }
            } else {
                positional = true;
                auto expr = parse_expression();
                conns.push_back({"", std::move(expr)});
            }
            if (match(TokenType::TOKEN_COMMA)) {
                if (current.type == TokenType::TOKEN_COMMA) {
                    positional = true;
                    conns.push_back({"", nullptr});
                    advance();
                }
                continue;
            }
            break;
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing instance port connections");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after instance");

        if (is_instance_array) {
            int64_t low = std::min(arr_msb, arr_lsb);
            int64_t high = std::max(arr_msb, arr_lsb);
            for (int64_t idx = low; idx <= high; ++idx) {
                std::string elem_name = iname + "[" + std::to_string(idx) + "]";
                auto inst = std::make_unique<InstantiationNode>(mtype, elem_name, positional);
                for (const auto& po : param_overrides) {
                    inst->param_overrides.push_back({po.param_name, po.expr ? po.expr->clone() : nullptr});
                }
                for (const auto& pc : conns) {
                    inst->connections.push_back({pc.port_name, pc.expr ? pc.expr->clone() : nullptr, pc.is_implicit_name_only});
                }
                if (blk) blk->instantiations.push_back(std::move(inst));
                else mod->instantiations.push_back(std::move(inst));
            }
        } else {
            auto inst = std::make_unique<InstantiationNode>(mtype, iname, positional);
            inst->param_overrides = std::move(param_overrides);
            inst->connections = std::move(conns);
            if (blk) blk->instantiations.push_back(std::move(inst));
            else mod->instantiations.push_back(std::move(inst));
        }
        return;
    }

    advance();
}

static bool expr_has_side_effects(const ExpressionNode* expr) {
    if (!expr) return false;
    if (auto bo = dynamic_cast<const BinaryOpNode*>(expr)) {
        if (bo->op == "=" || bo->op == "+=" || bo->op == "-=" || bo->op == "*=" ||
            bo->op == "/=" || bo->op == "%=" || bo->op == "&=" || bo->op == "|=" ||
            bo->op == "^=" || bo->op == "<<=" || bo->op == ">>=" || bo->op == "<<<=" ||
            bo->op == ">>>=") {
            return true;
        }
        return expr_has_side_effects(bo->lhs.get()) || expr_has_side_effects(bo->rhs.get());
    }
    if (auto uo = dynamic_cast<const UnaryOpNode*>(expr)) {
        if (uo->op == "++" || uo->op == "--") return true;
        return expr_has_side_effects(uo->operand.get());
    }
    if (auto to = dynamic_cast<const TernaryOpNode*>(expr)) {
        return expr_has_side_effects(to->condition.get()) ||
               expr_has_side_effects(to->true_expr.get()) ||
               expr_has_side_effects(to->false_expr.get());
    }
    if (auto co = dynamic_cast<const ConcatNode*>(expr)) {
        for (const auto& el : co->elements) {
            if (expr_has_side_effects(el.get())) return true;
        }
    }
    if (auto uco = dynamic_cast<const UnpackedConcatNode*>(expr)) {
        for (const auto& el : uco->elements) {
            if (expr_has_side_effects(el.get())) return true;
        }
    }
    if (auto ro = dynamic_cast<const ReplicateNode*>(expr)) {
        return expr_has_side_effects(ro->count_expr.get()) || expr_has_side_effects(ro->target_expr.get());
    }
    if (auto iso = dynamic_cast<const IndexSelectNode*>(expr)) {
        return expr_has_side_effects(iso->msb_expr.get()) || expr_has_side_effects(iso->lsb_expr.get());
    }
    if (auto cast_n = dynamic_cast<const CastNode*>(expr)) {
        return expr_has_side_effects(cast_n->expr.get());
    }
    if (auto scast_n = dynamic_cast<const StaticCastExprNode*>(expr)) {
        return expr_has_side_effects(scast_n->expr.get());
    }
    if (auto fco = dynamic_cast<const FunctionCallNode*>(expr)) {
        for (const auto& a : fco->args) {
            if (expr_has_side_effects(a.get())) return true;
        }
    }
    return false;
}

static bool expr_references_identifier(const ExpressionNode* expr, const std::string& target_id) {
    if (!expr) return false;
    if (auto id = dynamic_cast<const IdentifierNode*>(expr)) {
        return id->name == target_id;
    }
    if (auto fco = dynamic_cast<const FunctionCallNode*>(expr)) {
        if (fco->func_name == target_id) return true;
        for (const auto& a : fco->args) {
            if (expr_references_identifier(a.get(), target_id)) return true;
        }
    }
    if (auto bo = dynamic_cast<const BinaryOpNode*>(expr)) {
        return expr_references_identifier(bo->lhs.get(), target_id) || expr_references_identifier(bo->rhs.get(), target_id);
    }
    if (auto uo = dynamic_cast<const UnaryOpNode*>(expr)) {
        return expr_references_identifier(uo->operand.get(), target_id);
    }
    if (auto to = dynamic_cast<const TernaryOpNode*>(expr)) {
        return expr_references_identifier(to->condition.get(), target_id) ||
               expr_references_identifier(to->true_expr.get(), target_id) ||
               expr_references_identifier(to->false_expr.get(), target_id);
    }
    if (auto co = dynamic_cast<const ConcatNode*>(expr)) {
        for (const auto& el : co->elements) {
            if (expr_references_identifier(el.get(), target_id)) return true;
        }
    }
    if (auto uco = dynamic_cast<const UnpackedConcatNode*>(expr)) {
        for (const auto& el : uco->elements) {
            if (expr_references_identifier(el.get(), target_id)) return true;
        }
    }
    return false;
}

std::shared_ptr<LetDeclNode> Parser::parse_let_declaration() {
    expect(TokenType::TOKEN_LET, "Expected 'let'");
    std::string name = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after 'let'");

    if (current_parsing_module && current_parsing_module->let_decl_map.count(name)) {
        throw std::runtime_error("[CAS-DTINPCIL] Duplicate 'let' identifier '" + name + "' in same scope per IEEE 1800-2023 section 11.12");
    }
    if (current_parsing_package && current_parsing_package->let_decl_map.count(name)) {
        throw std::runtime_error("[CAS-DTINPCIL] Duplicate 'let' identifier '" + name + "' in same package per IEEE 1800-2023 section 11.12");
    }

    std::vector<LetPortItem> ports;
    if (match(TokenType::TOKEN_LPAREN)) {
        if (current.type != TokenType::TOKEN_RPAREN) {
            while (true) {
                ports.push_back(parse_let_port_item());
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing let formal argument list");
    }

    expect(TokenType::TOKEN_EQUAL, "Expected '=' in let declaration");
    auto body_expr = parse_expression();
    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after let declaration");

    if (expr_has_side_effects(body_expr.get())) {
        throw std::runtime_error("[CAS-DTINPCIL] Side effects or state modifications are illegal inside 'let' expressions per IEEE 1800-2023 section 11.12");
    }

    if (expr_references_identifier(body_expr.get(), name)) {
        throw std::runtime_error("[CAS-DTINPCIL] Recursive let declaration or invocation is illegal per IEEE 1800-2023 section 11.12: '" + name + "'");
    }

    auto let_node = std::make_shared<LetDeclNode>(name, std::move(ports), std::move(body_expr));
    let_node->file = current.filename;
    let_node->line = current.line;
    return let_node;
}

LetPortItem Parser::parse_let_port_item() {
    bool is_typed = false;
    std::string data_type = "";
    uint32_t width = 32;
    bool is_signed = false;

    if (current.type == TokenType::TOKEN_UNTYPED) {
        advance();
        is_typed = false;
    } else if (current.type == TokenType::TOKEN_LOGIC ||
               current.type == TokenType::TOKEN_BIT ||
               current.type == TokenType::TOKEN_REG ||
               current.type == TokenType::TOKEN_INTEGER) {
        is_typed = true;
        data_type = current.text;
        if (current.type == TokenType::TOKEN_INTEGER) {
            width = 32;
            is_signed = true;
        } else {
            width = 1;
            is_signed = false;
        }
        advance();
        if (match(TokenType::TOKEN_LBRACKET)) {
            auto msb_e = parse_expression();
            expect(TokenType::TOKEN_COLON, "Expected ':' in port range");
            auto lsb_e = parse_expression();
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in port range");
            if (auto nm = dynamic_cast<NumberNode*>(msb_e.get())) {
                if (auto nl = dynamic_cast<NumberNode*>(lsb_e.get())) {
                    width = (nm->aval >= nl->aval) ? (uint32_t)(nm->aval - nl->aval + 1) : (uint32_t)(nl->aval - nm->aval + 1);
                }
            }
        }
    } else if (current.type == TokenType::TOKEN_IDENTIFIER) {
        if (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint") {
            is_typed = true;
            data_type = current.text;
            is_signed = true;
            if (current.text == "byte") width = 8;
            else if (current.text == "shortint") width = 16;
            else if (current.text == "int") width = 32;
            else if (current.text == "longint") width = 64;
            advance();
        } else if (is_typedef(current.text)) {
            is_typed = true;
            data_type = current.text;
            advance();
        }
    }

    std::string port_name = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected formal argument name in let declaration");

    std::unique_ptr<ExpressionNode> default_value = nullptr;
    if (match(TokenType::TOKEN_EQUAL)) {
        default_value = parse_expression();
    }

    return LetPortItem(port_name, is_typed, data_type, width, is_signed, std::move(default_value));
}

void Parser::bind_out_of_body_function(std::unique_ptr<FunctionNode> fn) {
    if (!fn) return;
    size_t colon_pos = fn->name.find("::");
    if (colon_pos == std::string::npos) return;

    std::string class_name = fn->name.substr(0, colon_pos);
    std::string method_name = fn->name.substr(colon_pos + 2);

    ClassNode* cls = find_class(class_name);
    if (!cls) {
        throw std::runtime_error("[CAS-DTINPCIL] Out-of-body method implementation for undeclared class '" + class_name + "' per IEEE 1800-2023 section 8.24");
    }

    auto fit = cls->function_map.find(method_name);
    if (fit == cls->function_map.end()) {
        throw std::runtime_error("[CAS-DTINPCIL] Out-of-body method '" + method_name + "' was not declared in class '" + class_name + "' per IEEE 1800-2023 section 8.24");
    }

    FunctionNode* proto = fit->second;
    if (!proto->is_extern) {
        throw std::runtime_error("[CAS-DTINPCIL] Out-of-body method '" + method_name + "' in class '" + class_name + "' was not declared with 'extern' qualifier per IEEE 1800-2023 section 8.24");
    }

    if (proto->has_body) {
        throw std::runtime_error("[CAS-DTINPCIL] Duplicate implementation of extern method '" + method_name + "' in class '" + class_name + "' per IEEE 1800-2023 section 8.24");
    }

    if (fn->inputs.size() != proto->inputs.size()) {
        throw std::runtime_error("[CAS-DTINPCIL] Argument count mismatch in out-of-body implementation of method '" + method_name + "' in class '" + class_name + "' (expected " + std::to_string(proto->inputs.size()) + ", got " + std::to_string(fn->inputs.size()) + ") per IEEE 1800-2023 section 8.24");
    }

    if (method_name != "new" && fn->return_width != 0 && proto->return_width != 0) {
        if (fn->return_width != proto->return_width || fn->return_signed != proto->return_signed) {
            throw std::runtime_error("[CAS-DTINPCIL] Return type mismatch in out-of-body implementation of method '" + method_name + "' in class '" + class_name + "' per IEEE 1800-2023 section 8.24");
        }
    }

    proto->body = std::move(fn->body);
    proto->has_body = true;
    if (!fn->inputs.empty()) {
        proto->inputs = std::move(fn->inputs);
    }
}

void Parser::bind_out_of_body_task(std::unique_ptr<TaskNode> t) {
    if (!t) return;
    size_t colon_pos = t->name.find("::");
    if (colon_pos == std::string::npos) return;

    std::string class_name = t->name.substr(0, colon_pos);
    std::string method_name = t->name.substr(colon_pos + 2);

    ClassNode* cls = find_class(class_name);
    if (!cls) {
        throw std::runtime_error("[CAS-DTINPCIL] Out-of-body task implementation for undeclared class '" + class_name + "' per IEEE 1800-2023 section 8.24");
    }

    auto tit = cls->task_map.find(method_name);
    if (tit == cls->task_map.end()) {
        throw std::runtime_error("[CAS-DTINPCIL] Out-of-body task '" + method_name + "' was not declared in class '" + class_name + "' per IEEE 1800-2023 section 8.24");
    }

    TaskNode* proto = tit->second;
    if (!proto->is_extern) {
        throw std::runtime_error("[CAS-DTINPCIL] Out-of-body task '" + method_name + "' in class '" + class_name + "' was not declared with 'extern' qualifier per IEEE 1800-2023 section 8.24");
    }

    if (proto->has_body) {
        throw std::runtime_error("[CAS-DTINPCIL] Duplicate implementation of extern task '" + method_name + "' in class '" + class_name + "' per IEEE 1800-2023 section 8.24");
    }

    if (t->ports.size() != proto->ports.size()) {
        throw std::runtime_error("[CAS-DTINPCIL] Argument count mismatch in out-of-body implementation of task '" + method_name + "' in class '" + class_name + "' (expected " + std::to_string(proto->ports.size()) + ", got " + std::to_string(t->ports.size()) + ") per IEEE 1800-2023 section 8.24");
    }

    proto->body = std::move(t->body);
    proto->has_body = true;
    if (!t->ports.empty()) {
        proto->ports = std::move(t->ports);
    }
}

TypeInferenceResult Parser::parse_type_spec_from_type_op() {
    expect(TokenType::TOKEN_TYPE, "Expected 'type'");
    expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'type'");
    if (current.type == TokenType::TOKEN_RPAREN) {
        throw std::runtime_error("[CAS-DTINPCIL] type() requires an argument per IEEE 1800-2023 section 6.23");
    }

    TypeInferenceResult res;

    // 1. Primitive keyword directly inside type(...)
    bool is_prim_keyword = (current.type == TokenType::TOKEN_LOGIC ||
                            current.type == TokenType::TOKEN_REG ||
                            current.type == TokenType::TOKEN_BIT ||
                            current.type == TokenType::TOKEN_INTEGER ||
                            current.type == TokenType::TOKEN_REAL ||
                            current.type == TokenType::TOKEN_SHORTREAL ||
                            current.type == TokenType::TOKEN_CHANDLE ||
                            current.type == TokenType::TOKEN_VOID ||
                            (current.type == TokenType::TOKEN_IDENTIFIER &&
                             (current.text == "int" || current.text == "byte" ||
                              current.text == "shortint" || current.text == "longint" ||
                              current.text == "time" || current.text == "string" ||
                              current.text == "shortreal" || current.text == "realtime")));

    if (is_prim_keyword) {
        std::string kw = current.text;
        advance();
        if (kw == "int" || kw == "integer") {
            res.width = 32; res.is_signed = true; res.type_name = "int";
        } else if (kw == "byte") {
            res.width = 8; res.is_signed = true; res.type_name = "byte";
        } else if (kw == "shortint") {
            res.width = 16; res.is_signed = true; res.type_name = "shortint";
        } else if (kw == "longint") {
            res.width = 64; res.is_signed = true; res.type_name = "longint";
        } else if (kw == "time") {
            res.width = 64; res.is_signed = false; res.type_name = "time";
        } else if (kw == "shortreal") {
            res.width = 32; res.type_name = "shortreal";
        } else if (kw == "string" || kw == "real" || kw == "chandle") {
            res.width = 64; res.type_name = kw;
        } else {
            // logic, bit, reg
            res.width = 1;
            res.type_name = kw;
            if (match(TokenType::TOKEN_LBRACKET)) {
                auto msb_e = parse_expression();
                expect(TokenType::TOKEN_COLON, "Expected ':' in dimension");
                auto lsb_e = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                int64_t m = 0, l = 0;
                if (auto nm = dynamic_cast<NumberNode*>(msb_e.get())) m = nm->aval;
                if (auto nl = dynamic_cast<NumberNode*>(lsb_e.get())) l = nl->aval;
                res.width = (m >= l) ? (uint32_t)(m - l + 1) : (uint32_t)(l - m + 1);
                res.msb = std::move(msb_e);
                res.lsb = std::move(lsb_e);
            }
        }
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing type()");
        return res;
    }

    // 2. An expression or identifier
    std::string full_name = "";
    if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_RPAREN) {
        full_name = current.text;
        advance();
    } else if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON_COLON) {
        std::string ident = current.text;
        advance(); advance();
        std::string sub = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '::'");
        full_name = ident + "::" + sub;
    } else {
        auto expr = parse_expression();
        if (auto id = dynamic_cast<IdentifierNode*>(expr.get())) {
            full_name = id->name;
        } else if (expr) {
            std::function<std::string(const ExpressionNode*)> find_first_id = [&](const ExpressionNode* e) -> std::string {
                if (!e) return "";
                if (auto id_node = dynamic_cast<const IdentifierNode*>(e)) return id_node->name;
                if (auto b = dynamic_cast<const BinaryOpNode*>(e)) {
                    std::string l = find_first_id(b->lhs.get());
                    if (!l.empty()) return l;
                    return find_first_id(b->rhs.get());
                }
                if (auto u = dynamic_cast<const UnaryOpNode*>(e)) return find_first_id(u->operand.get());
                return "";
            };
            std::string sample_id = find_first_id(expr.get());
            if (!sample_id.empty()) {
                full_name = sample_id;
            }
        }
    }

    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing type()");

    // Now resolve full_name:
    if (auto en = get_enum_typedef(full_name)) {
        res.width = en->base_width;
        res.is_signed = en->is_signed;
        res.type_name = full_name;
        return res;
    }
    if (auto st = get_struct_typedef(full_name)) {
        res.width = st->total_width;
        res.is_signed = st->is_signed;
        res.type_name = full_name;
        return res;
    }
    if (is_class(full_name)) {
        res.width = 64;
        res.is_class_handle = true;
        res.class_type_name = full_name;
        res.type_name = full_name;
        return res;
    }

    // Check local variables
    if (auto loc = lookup_local_decl_var(full_name)) {
        res.width = loc->width;
        res.is_signed = loc->is_signed;
        res.type_name = loc->type_name;
        if (loc->msb) res.msb = loc->msb->clone();
        if (loc->lsb) res.lsb = loc->lsb->clone();
        return res;
    }

    // Check current module ports & signals
    if (current_parsing_module) {
        for (const auto& p : current_parsing_module->ports) {
            if (p.name == full_name) {
                res.is_signed = p.is_signed;
                res.type_name = p.type_name;
                res.is_class_handle = p.is_class_handle;
                res.class_type_name = p.class_type_name;
                res.is_covergroup_handle = p.is_covergroup_handle;
                res.covergroup_type_name = p.covergroup_type_name;
                res.is_virtual_interface = p.is_virtual_interface;
                res.interface_name = p.interface_name;
                res.modport_name = p.modport_name;
                if (p.msb_expr) {
                    res.msb = p.msb_expr->clone();
                    if (p.lsb_expr) res.lsb = p.lsb_expr->clone();
                    int64_t m = 0, l = 0;
                    if (auto nm = dynamic_cast<NumberNode*>(p.msb_expr.get())) m = nm->aval;
                    if (auto nl = dynamic_cast<NumberNode*>(p.lsb_expr.get())) l = nl->aval;
                    res.width = (m >= l) ? (uint32_t)(m - l + 1) : (uint32_t)(l - m + 1);
                } else {
                    res.width = 1;
                }
                return res;
            }
        }
        for (const auto& s : current_parsing_module->signals) {
            if (s.name == full_name) {
                res.is_signed = s.is_signed;
                res.type_name = s.type_name;
                res.is_class_handle = s.is_class_handle;
                res.class_type_name = s.class_type_name;
                res.is_covergroup_handle = s.is_covergroup_handle;
                res.covergroup_type_name = s.covergroup_type_name;
                res.is_virtual_interface = s.is_virtual_interface;
                res.interface_name = s.interface_name;
                res.modport_name = s.modport_name;
                if (s.msb_expr) {
                    res.msb = s.msb_expr->clone();
                    if (s.lsb_expr) res.lsb = s.lsb_expr->clone();
                    int64_t m = 0, l = 0;
                    if (auto nm = dynamic_cast<NumberNode*>(s.msb_expr.get())) m = nm->aval;
                    if (auto nl = dynamic_cast<NumberNode*>(s.lsb_expr.get())) l = nl->aval;
                    res.width = (m >= l) ? (uint32_t)(m - l + 1) : (uint32_t)(l - m + 1);
                } else {
                    res.width = 1;
                }
                return res;
            }
        }
    }

    throw std::runtime_error("[CAS-DTINPCIL] Argument of type() cannot be statically resolved per IEEE 1800-2023 section 6.23: '" + full_name + "'");
}

std::string Parser::resolve_type_from_type_op(const TypeOpNode* ton) const {
    if (!ton) return "";
    if (!ton->type_name_arg.empty()) {
        auto en = get_enum_typedef(ton->type_name_arg);
        if (en) return "enum " + ton->type_name_arg;
        auto st = get_struct_typedef(ton->type_name_arg);
        if (st) return "struct " + ton->type_name_arg;
        return ton->type_name_arg;
    }
    if (!ton->target_expr) return "";

    if (auto id = dynamic_cast<IdentifierNode*>(ton->target_expr.get())) {
        std::string name = id->name;
        if (name == "int" || name == "byte" || name == "shortint" || name == "longint" ||
            name == "integer" || name == "time" || name == "string" || name == "real" ||
            name == "shortreal" || name == "realtime" || name == "chandle" || name == "void") return name;
        if (name == "logic" || name == "bit" || name == "reg") return name;

        if (auto loc = lookup_local_decl_var(name)) {
            if (!loc->type_name.empty() && loc->type_name != "logic" && loc->type_name != "bit" && loc->type_name != "reg") {
                auto en = get_enum_typedef(loc->type_name);
                if (en) return "enum " + loc->type_name;
                auto st = get_struct_typedef(loc->type_name);
                if (st) return "struct " + loc->type_name;
                return loc->type_name;
            }
            if (loc->msb) {
                int64_t m = 0, l = 0;
                if (auto nm = dynamic_cast<NumberNode*>(loc->msb.get())) m = nm->aval;
                if (loc->lsb) {
                    if (auto nl = dynamic_cast<NumberNode*>(loc->lsb.get())) l = nl->aval;
                }
                std::string base = (loc->type_name == "bit") ? "bit" : "logic";
                return base + "[" + std::to_string(m) + ":" + std::to_string(l) + "]";
            }
            if (loc->width > 1) {
                std::string base = (loc->type_name == "bit") ? "bit" : "logic";
                return base + "[" + std::to_string(loc->width - 1) + ":0]";
            }
            return (loc->type_name == "bit") ? "bit" : "logic";
        }

        if (current_parsing_module) {
            for (const auto& p : current_parsing_module->ports) {
                if (p.name == name) {
                    if (!p.type_name.empty()) {
                        auto en = get_enum_typedef(p.type_name);
                        if (en) return "enum " + p.type_name;
                        auto st = get_struct_typedef(p.type_name);
                        if (st) return "struct " + p.type_name;
                        return p.type_name;
                    }
                    if (p.msb_expr) {
                        int64_t m = 0, l = 0;
                        if (auto nm = dynamic_cast<NumberNode*>(p.msb_expr.get())) m = nm->aval;
                        if (p.lsb_expr) {
                            if (auto nl = dynamic_cast<NumberNode*>(p.lsb_expr.get())) l = nl->aval;
                        }
                        return "logic[" + std::to_string(m) + ":" + std::to_string(l) + "]";
                    }
                    return "logic";
                }
            }
            for (const auto& s : current_parsing_module->signals) {
                if (s.name == name) {
                    if (!s.type_name.empty()) {
                        auto en = get_enum_typedef(s.type_name);
                        if (en) return "enum " + s.type_name;
                        auto st = get_struct_typedef(s.type_name);
                        if (st) return "struct " + s.type_name;
                        return s.type_name;
                    }
                    if (s.msb_expr) {
                        int64_t m = 0, l = 0;
                        if (auto nm = dynamic_cast<NumberNode*>(s.msb_expr.get())) m = nm->aval;
                        if (s.lsb_expr) {
                            if (auto nl = dynamic_cast<NumberNode*>(s.lsb_expr.get())) l = nl->aval;
                        }
                        return "logic[" + std::to_string(m) + ":" + std::to_string(l) + "]";
                    }
                    return "logic";
                }
            }
        }

        auto en = get_enum_typedef(name);
        if (en) return "enum " + name;
        auto st = get_struct_typedef(name);
        if (st) return "struct " + name;
    }

    if (auto sma = dynamic_cast<StaticMemberAccessNode*>(ton->target_expr.get())) {
        std::string full = sma->class_type_name + "::" + sma->member_name;
        auto en = get_enum_typedef(full);
        if (en) return "enum " + full;
        auto st = get_struct_typedef(full);
        if (st) return "struct " + full;
        return full;
    }

    return "";
}

