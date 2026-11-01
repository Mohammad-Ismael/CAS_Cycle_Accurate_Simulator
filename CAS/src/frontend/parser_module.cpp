#include "../../include/frontend/parser.hpp"
#include <stdexcept>

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
        if (current.type == TokenType::TOKEN_PARAMETER || current.type == TokenType::TOKEN_LOCALPARAM) {
            bool is_local = (current.type == TokenType::TOKEN_LOCALPARAM);
            advance();
            
            if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
                current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_INTEGER ||
                current.type == TokenType::TOKEN_SIGNED || current.type == TokenType::TOKEN_BIT ||
                (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint" || current.text == "bit" || current.text == "unsigned"))) {
                advance();
                if (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "unsigned")) {
                    advance();
                }
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

                ParameterDef pdef(pname, std::move(val_expr), is_local);
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

        if (current.type == TokenType::TOKEN_CONFIG) {
            auto cfg = parse_config();
            if (cfg) {
                global_configs[cfg->name] = cfg;
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

        if (current.type == TokenType::TOKEN_PRIMITIVE) {
            auto udp = parse_udp();
            if (udp) {
                global_udp_defs[udp->name] = udp;
            }
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
        for (const auto& w : global_wildcard_imports) {
            mod->wildcard_imports.push_back(w);
        }
        for (const auto& kv : global_explicit_imports) {
            mod->explicit_imports[kv.first] = kv.second;
        }
        mod->defined_in_file = current.filename;
        mod->defined_on_line = current.line;
        mod->name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, is_iface ? "Expected interface name" : (is_prog ? "Expected program name" : "Expected module name"));
        if (is_iface) known_interfaces.insert(mod->name);

        for (const auto& gp : global_parameters) {
            ParameterDef pcopy;
            pcopy.name = gp.name;
            pcopy.default_expr = gp.default_expr ? gp.default_expr->clone() : nullptr;
            mod->parameters.push_back(std::move(pcopy));
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
                if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
                    current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_INTEGER ||
                    current.type == TokenType::TOKEN_SIGNED || current.type == TokenType::TOKEN_BIT ||
                    (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint" || current.text == "bit" || current.text == "unsigned"))) {
                    advance();
                    if (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "unsigned")) {
                        advance();
                    }
                }
                if (match(TokenType::TOKEN_LBRACKET)) {
                    parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in parameter range");
                    parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in parameter range");
                }
                std::string pname = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter identifier");
                expect(TokenType::TOKEN_EQUAL, "Expected '=' in parameter definition");
                auto pexpr = parse_expression();
                mod->parameters.push_back(ParameterDef(pname, std::move(pexpr), is_local));

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
                if (current.type == TokenType::TOKEN_SIGNED) {
                    is_signed_port = true;
                    advance();
                }
                bool is_chandle_port = (current.type == TokenType::TOKEN_CHANDLE || 
                                       (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "chandle"));
                bool is_semaphore_port = (current.type == TokenType::TOKEN_SEMAPHORE || 
                                         (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "semaphore"));
                bool is_mailbox_port = (current.type == TokenType::TOKEN_MAILBOX || 
                                       (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "mailbox"));
                bool is_string_port = (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "string");
                NetKind port_kind = (dir == "input" || dir == "inout") ? NetKind::WIRE : NetKind::WIRE;
                if (is_chandle_port || is_semaphore_port || is_mailbox_port || is_string_port) {
                    port_kind = NetKind::VAR;
                    advance();
                    if (is_mailbox_port && match(TokenType::TOKEN_HASH)) {
                        expect(TokenType::TOKEN_LPAREN, "Expected '(' in mailbox parameter");
                        while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) advance();
                        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing mailbox parameter");
                    }
                }
                bool is_int_port = false;
                std::string int_port_type = "";
                if (current.type == TokenType::TOKEN_IDENTIFIER &&
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
                    advance();
                } else if (current.type == TokenType::TOKEN_WIRE) {
                    port_kind = NetKind::WIRE;
                    advance();
                } else if (current.type == TokenType::TOKEN_WAND) {
                    port_kind = NetKind::WAND;
                    advance();
                } else if (current.type == TokenType::TOKEN_WOR) {
                    port_kind = NetKind::WOR;
                    advance();
                } else if (current.type == TokenType::TOKEN_TRI) {
                    port_kind = NetKind::TRI;
                    advance();
                }
                if (current.type == TokenType::TOKEN_SIGNED) {
                    is_signed_port = true;
                    advance();
                }

                std::unique_ptr<ExpressionNode> msb = nullptr;
                std::unique_ptr<ExpressionNode> lsb = nullptr;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    msb = parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in port range");
                    lsb = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in port range");
                } else if (is_chandle_port || is_semaphore_port || is_mailbox_port || is_string_port) {
                    msb = std::make_unique<NumberNode>(63, 32);
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
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected port identifier");
                    PortNode port(pname, dir, msb ? msb->clone() : nullptr, lsb ? lsb->clone() : nullptr, is_signed_port, port_kind);
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
                    mod->ports.push_back(std::move(port));

                    if (match(TokenType::TOKEN_COMMA)) {
                        if (current.type == TokenType::TOKEN_INPUT || current.type == TokenType::TOKEN_OUTPUT || current.type == TokenType::TOKEN_INOUT ||
                            current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE || 
                            current.type == TokenType::TOKEN_WAND || current.type == TokenType::TOKEN_WOR || current.type == TokenType::TOKEN_TRI ||
                            current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_SIGNED ||
                            current.type == TokenType::TOKEN_INTERFACE ||
                            (current.type == TokenType::TOKEN_IDENTIFIER && (peek_token().type == TokenType::TOKEN_IDENTIFIER || peek_token().type == TokenType::TOKEN_DOT))) {
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
            if (is_prog && (current.type == TokenType::TOKEN_MODULE || current.type == TokenType::TOKEN_INTERFACE || current.type == TokenType::TOKEN_PROGRAM)) {
                throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: module, interface, or program declaration is illegal in program scope per IEEE 1800-2023 section 24.3");
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
                is_def_cb = true;
                advance();
            }
            if (current.type == TokenType::TOKEN_CLOCKING) {
                advance();
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
                            } else if (match(TokenType::TOKEN_OUTPUT)) {
                                cb_node->default_output_skew = parse_skew_spec();
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
                if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
                    current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_INTEGER ||
                    current.type == TokenType::TOKEN_SIGNED || current.type == TokenType::TOKEN_BIT ||
                    (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint" || current.text == "bit" || current.text == "unsigned"))) {
                    advance();
                    if (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "unsigned")) {
                        advance();
                    }
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
                    mod->parameters.push_back(ParameterDef(pname, std::move(val_expr), is_local));
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
                if (current.type == TokenType::TOKEN_SIGNED) {
                    is_signed_port = true;
                    advance();
                }
                bool is_chandle_port = (current.type == TokenType::TOKEN_CHANDLE || 
                                       (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "chandle"));
                bool is_semaphore_port = (current.type == TokenType::TOKEN_SEMAPHORE || 
                                         (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "semaphore"));
                bool is_mailbox_port = (current.type == TokenType::TOKEN_MAILBOX || 
                                       (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "mailbox"));
                bool is_string_port = (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "string");
                NetKind port_kind = (dir == "input" || dir == "inout") ? NetKind::WIRE : NetKind::WIRE;
                if (is_chandle_port || is_semaphore_port || is_mailbox_port || is_string_port) {
                    port_kind = NetKind::VAR;
                    advance();
                    if (is_mailbox_port && match(TokenType::TOKEN_HASH)) {
                        expect(TokenType::TOKEN_LPAREN, "Expected '(' in mailbox parameter");
                        while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) advance();
                        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing mailbox parameter");
                    }
                } else if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
                    current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_BIT) {
                    port_kind = NetKind::VAR;
                    if (current.type == TokenType::TOKEN_INTEGER) is_signed_port = true;
                    advance();
                } else if (current.type == TokenType::TOKEN_WIRE) {
                    port_kind = NetKind::WIRE;
                    advance();
                } else if (current.type == TokenType::TOKEN_WAND) {
                    port_kind = NetKind::WAND;
                    advance();
                } else if (current.type == TokenType::TOKEN_WOR) {
                    port_kind = NetKind::WOR;
                    advance();
                } else if (current.type == TokenType::TOKEN_TRI) {
                    port_kind = NetKind::TRI;
                    advance();
                }
                if (current.type == TokenType::TOKEN_SIGNED) {
                    is_signed_port = true;
                    advance();
                }

                std::unique_ptr<ExpressionNode> msb = nullptr;
                std::unique_ptr<ExpressionNode> lsb = nullptr;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    msb = parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in range");
                    lsb = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in range");
                } else if (is_chandle_port || is_semaphore_port || is_mailbox_port || is_string_port) {
                    msb = std::make_unique<NumberNode>(63, 32);
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
                        mod->parameters.push_back(std::move(pdef));
                    }
                }
                mod->typedefs.push_back(std::move(td));
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

            if (current.type == TokenType::TOKEN_IDENTIFIER && is_class(current.text)) {
                std::string class_type = parse_class_type_and_specialize();
                while (true) {
                    std::string signame = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected class handle identifier");
                    if (match(TokenType::TOKEN_LBRACKET)) {
                        while (current.type != TokenType::TOKEN_RBRACKET && current.type != TokenType::TOKEN_EOF) advance();
                        match(TokenType::TOKEN_RBRACKET);
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

            std::string mod_sig_type = "";
            if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_COLON_COLON) {
                std::string p_name = current.text;
                advance(); // consume package identifier
                advance(); // consume '::'
                std::string t_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected type identifier after '::'");
                mod_sig_type = p_name + "::" + t_name;
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && is_typedef(current.text)) {
                mod_sig_type = current.text;
                advance();
            }

            if (!mod_sig_type.empty()) {
                std::string type_name = mod_sig_type;
                auto enum_info = get_enum_typedef(type_name);
                auto struct_info = get_struct_typedef(type_name);

                uint32_t width = struct_info ? struct_info->total_width : (enum_info ? enum_info->base_width : 32);
                bool is_signed_sig = struct_info ? struct_info->is_signed : (enum_info ? enum_info->is_signed : false);

                auto msb = std::make_unique<NumberNode>(width > 0 ? width - 1 : 0, 32);
                auto lsb = std::make_unique<NumberNode>(0, 32);

                while (true) {
                    std::string signame = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier");
                    PortNode sig(signame, "internal", msb ? msb->clone() : nullptr, lsb ? lsb->clone() : nullptr, is_signed_sig, NetKind::VAR);
                    sig.type_name = type_name;

                    while (match(TokenType::TOKEN_LBRACKET)) {
                        if (match(TokenType::TOKEN_DOLLAR)) {
                            sig.is_queue = true;
                            if (match(TokenType::TOKEN_COLON)) {
                                sig.queue_bound_expr = parse_expression();
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
                        sig.default_value = init_expr->clone();
                        auto init_proc = std::make_unique<ProcessNode>(ProcessType::INITIAL);
                        init_proc->body.push_back(std::make_unique<BlockingAssignNode>(signame, std::move(init_expr)));
                        mod->processes.push_back(std::move(init_proc));
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
                        mod->signals.push_back(std::move(sig));
                    }
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after signal declaration");
                continue;
            }

            bool is_ident_int = (current.type == TokenType::TOKEN_IDENTIFIER && 
                                 (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint"));
            bool is_ident_unsigned = (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned");
            bool is_chandle_mod = (current.type == TokenType::TOKEN_CHANDLE || 
                                   (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "chandle"));
            bool is_semaphore_mod = (current.type == TokenType::TOKEN_SEMAPHORE || 
                                     (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "semaphore"));
            bool is_mailbox_mod = (current.type == TokenType::TOKEN_MAILBOX || 
                                   (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "mailbox"));
            bool is_ident_string_sig = (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "string");

            if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE || 
                current.type == TokenType::TOKEN_WAND  || current.type == TokenType::TOKEN_WOR  || 
                current.type == TokenType::TOKEN_TRI   ||
                current.type == TokenType::TOKEN_REG   || current.type == TokenType::TOKEN_INTEGER || 
                current.type == TokenType::TOKEN_BIT   || current.type == TokenType::TOKEN_REAL ||
                current.type == TokenType::TOKEN_SIGNED || is_ident_int || is_ident_string_sig || is_ident_unsigned || is_chandle_mod ||
                is_semaphore_mod || is_mailbox_mod) {
                
                bool explicit_signed = false;
                bool explicit_unsigned = false;
                if (current.type == TokenType::TOKEN_SIGNED) {
                    explicit_signed = true;
                    advance();
                } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned") {
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
                bool is_real_sig = (current.type == TokenType::TOKEN_REAL);

                bool is_bit_type = (current.type == TokenType::TOKEN_BIT);
                NetKind sig_kind = NetKind::WIRE;
                if (is_chandle_mod || is_semaphore_mod || is_mailbox_mod || is_ident_string_sig) {
                    sig_kind = NetKind::VAR;
                } else if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_BIT || is_int || is_real_sig) {
                    sig_kind = NetKind::VAR;
                } else if (current.type == TokenType::TOKEN_WAND) {
                    sig_kind = NetKind::WAND;
                } else if (current.type == TokenType::TOKEN_WOR) {
                    sig_kind = NetKind::WOR;
                } else if (current.type == TokenType::TOKEN_TRI) {
                    sig_kind = NetKind::TRI;
                } else if (current.type == TokenType::TOKEN_WIRE) {
                    sig_kind = NetKind::WIRE;
                }
                advance();
                if (is_mailbox_mod && match(TokenType::TOKEN_HASH)) {
                    expect(TokenType::TOKEN_LPAREN, "Expected '(' in mailbox parameter");
                    while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) advance();
                    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing mailbox parameter");
                }

                if (current.type == TokenType::TOKEN_SIGNED) {
                    explicit_signed = true;
                    advance();
                } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned") {
                    explicit_unsigned = true;
                    advance();
                }

                bool is_signed_sig = false;
                if (explicit_signed) is_signed_sig = true;
                else if (explicit_unsigned) is_signed_sig = false;
                else if (is_int) is_signed_sig = true;

                std::vector<AstDimension> packed_dims;
                std::unique_ptr<ExpressionNode> msb = nullptr;
                std::unique_ptr<ExpressionNode> lsb = nullptr;
                while (match(TokenType::TOKEN_LBRACKET)) {
                    auto l = parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in range");
                    auto r = parse_expression();
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
                    if (is_chandle_mod || is_semaphore_mod || is_mailbox_mod || is_ident_string_sig) {
                        msb = std::make_unique<NumberNode>(63, 32);
                        lsb = std::make_unique<NumberNode>(0, 32);
                    } else if (is_int) {
                        uint32_t iw = 32;
                        if (int_type_name == "byte") iw = 8;
                        else if (int_type_name == "shortint") iw = 16;
                        else if (int_type_name == "longint") iw = 64;
                        msb = std::make_unique<NumberNode>(iw - 1, 32);
                        lsb = std::make_unique<NumberNode>(0, 32);
                    } else if (is_real_sig) {
                        msb = std::make_unique<NumberNode>(63, 32);
                        lsb = std::make_unique<NumberNode>(0, 32);
                    }
                }
                while (true) {
                    std::string signame = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier");
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
                    if (is_real_sig) sig.type_name = "real";
                    if (!int_type_name.empty()) sig.type_name = int_type_name;

                    while (match(TokenType::TOKEN_LBRACKET)) {
                        if (match(TokenType::TOKEN_DOLLAR)) {
                            sig.is_queue = true;
                            if (match(TokenType::TOKEN_COLON)) {
                                sig.queue_bound_expr = parse_expression();
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
                        sig.default_value = init_expr->clone();
                        auto init_proc = std::make_unique<ProcessNode>(ProcessType::INITIAL);
                        init_proc->body.push_back(std::make_unique<BlockingAssignNode>(signame, std::move(init_expr)));
                        mod->processes.push_back(std::move(init_proc));
                    }

                    bool is_port = false;
                    for (auto& p : mod->ports) {
                        if (p.name == signame) {
                            p.is_signed = is_signed_sig;
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
                }
                std::string target = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected target identifier in assign");
                while (match(TokenType::TOKEN_DOT)) {
                    target += "." + current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in assign target");
                }
                std::unique_ptr<ExpressionNode> target_idx = nullptr;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    target_idx = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
                }
                expect(TokenType::TOKEN_EQUAL, "Expected '=' in assign statement");
                auto rhs = parse_expression();
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assign");
                auto ca = std::make_unique<ContinuousAssignNode>(target, std::move(rhs));
                ca->target_index_expr = std::move(target_idx);
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
            if (match(TokenType::TOKEN_VIRTUAL)) is_virt = true;
            // Function declaration parsed by parser_function.cpp
            if (match(TokenType::TOKEN_FUNCTION)) {
                auto fn = parse_function();
                if (fn) {
                    fn->is_virtual = is_virt;
                    mod->functions.push_back(std::move(fn));
                }
                continue;
            }

            if (match(TokenType::TOKEN_TASK)) {
                auto t = parse_task();
                if (t) {
                    t->is_virtual = is_virt;
                    mod->tasks.push_back(std::move(t));
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
                            mod->wildcard_imports.push_back(pkg_name);
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
                    expect(TokenType::TOKEN_LPAREN, "Expected '(' after '@'");
                    auto proc = std::make_unique<ProcessNode>(ProcessType::SEQUENTIAL);
                    while (true) {
                        EdgeType edge = EdgeType::LEVEL;
                        if (match(TokenType::TOKEN_POSEDGE)) edge = EdgeType::POSEDGE;
                        else if (match(TokenType::TOKEN_NEGEDGE)) edge = EdgeType::NEGEDGE;

                        std::string sig = current.text;
                        expect(TokenType::TOKEN_IDENTIFIER, "Expected signal name in sensitivity list");
                        while (match(TokenType::TOKEN_DOT)) {
                            sig += "." + current.text;
                            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in sensitivity list");
                        }
                        proc->sensitivity_list.push_back({edge, sig});

                        if (match(TokenType::TOKEN_OR) || match(TokenType::TOKEN_GATE_OR) || match(TokenType::TOKEN_COMMA)) continue;
                        break;
                    }
                    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing sensitivity list");
                    proc->body = parse_statement_or_block();
                    mod->processes.push_back(std::move(proc));
                } else {
                    auto proc = std::make_unique<ProcessNode>(ProcessType::ALWAYS);
                    proc->body = parse_statement_or_block();
                    mod->processes.push_back(std::move(proc));
                }
                continue;
            }

            if (current.type == TokenType::TOKEN_ASSERT) {
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
                if (current.type == TokenType::TOKEN_ASSERT) {
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
                            expect(TokenType::TOKEN_LPAREN, "Expected '(' in named parameter override");
                            auto pval = parse_expression();
                            expect(TokenType::TOKEN_RPAREN, "Expected ')' in named parameter override");
                            param_overrides.push_back({pname, std::move(pval)});
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

                if (is_prog) {
                    throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: module, interface, or program instantiation '" + iname + "' is illegal in program scope per IEEE 1800-2023 section 24.3");
                }

                auto inst = std::make_unique<InstantiationNode>(mtype, iname, positional);
                inst->param_overrides = std::move(param_overrides);
                inst->connections = std::move(conns);
                mod->instantiations.push_back(std::move(inst));
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
            if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
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
            return id;
        }
    }
    return "";
}

std::unique_ptr<TypedefNode> Parser::parse_typedef() {
    expect(TokenType::TOKEN_TYPEDEF, "Expected 'typedef'");

    if (match(TokenType::TOKEN_STRUCT)) {
        bool is_packed = false;
        if (match(TokenType::TOKEN_PACKED)) {
            is_packed = true;
        }
        bool is_signed = false;
        if (match(TokenType::TOKEN_SIGNED)) {
            is_signed = true;
        }

        expect(TokenType::TOKEN_LBRACE, "Expected '{' in struct declaration");
        auto struct_node = std::make_shared<StructTypeNode>();
        struct_node->is_packed = is_packed;
        struct_node->is_signed = is_signed;

        while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
            bool mem_signed = false;
            std::string mem_type = "logic";
            if (match(TokenType::TOKEN_SIGNED)) {
                mem_signed = true;
            }
            if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG ||
                current.type == TokenType::TOKEN_WIRE  || current.type == TokenType::TOKEN_INTEGER ||
                (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "bit"))) {
                mem_type = current.text;
                if (current.type == TokenType::TOKEN_INTEGER || current.text == "int" || current.text == "byte") {
                    mem_signed = true;
                }
                advance();
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && is_typedef(current.text)) {
                mem_type = current.text;
                advance();
            }
            if (match(TokenType::TOKEN_SIGNED)) {
                mem_signed = true;
            }

            uint32_t field_width = 1;
            if (mem_type == "int" || mem_type == "integer") field_width = 32;
            else if (mem_type == "byte") field_width = 8;
            else if (is_enum_typedef(mem_type)) field_width = typedef_map[mem_type]->base_width;
            else if (is_struct_typedef(mem_type)) field_width = struct_typedef_map[mem_type]->total_width;

            if (match(TokenType::TOKEN_LBRACKET)) {
                auto msb_e = parse_expression();
                expect(TokenType::TOKEN_COLON, "Expected ':' in struct member range");
                auto lsb_e = parse_expression();
                expect(TokenType::TOKEN_RBRACKET, "Expected ']' in struct member range");
                if (auto nm = dynamic_cast<NumberNode*>(msb_e.get())) {
                    if (auto nl = dynamic_cast<NumberNode*>(lsb_e.get())) {
                        field_width = (nm->aval >= nl->aval) ? (uint32_t)(nm->aval - nl->aval + 1) : (uint32_t)(nl->aval - nm->aval + 1);
                    }
                }
            }

            while (true) {
                std::string mem_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected member identifier in struct");
                StructMemberNode sm;
                sm.name = mem_name;
                sm.width = field_width;
                sm.is_signed = mem_signed;
                sm.type_name = mem_type;
                struct_node->members.push_back(sm);
                struct_node->total_width += field_width;

                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after struct member declaration");
        }
        expect(TokenType::TOKEN_RBRACE, "Expected '}' closing struct declaration");

        std::string type_name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected typedef type identifier");
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after typedef declaration");

        struct_typedef_map[type_name] = struct_node;
        return std::make_unique<TypedefNode>(type_name, struct_node);
    }

    expect(TokenType::TOKEN_ENUM, "Expected 'enum' or 'struct' in typedef");

    uint32_t width = 32;
    bool is_signed = true;
    std::string base_type_name = "int";

    // Optional base type
    if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG ||
        current.type == TokenType::TOKEN_WIRE  || current.type == TokenType::TOKEN_INTEGER ||
        current.type == TokenType::TOKEN_SIGNED ||
        (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "bit"))) {

        if (current.type == TokenType::TOKEN_SIGNED) {
            is_signed = true;
            advance();
        } else {
            is_signed = false;
        }

        if (current.type == TokenType::TOKEN_INTEGER || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "int")) {
            width = 32;
            is_signed = true;
            base_type_name = "int";
            advance();
        } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "byte") {
            width = 8;
            is_signed = true;
            base_type_name = "byte";
            advance();
        } else {
            base_type_name = current.text;
            width = 1;
            advance();
        }

        if (current.type == TokenType::TOKEN_SIGNED) {
            is_signed = true;
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
    while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
        std::string mname = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected enum member identifier");

        if (match(TokenType::TOKEN_EQUAL)) {
            auto val_expr = parse_expression();
            if (auto num = dynamic_cast<NumberNode*>(val_expr.get())) {
                current_val = num->aval;
            }
        }

        LogicVal mval(width, current_val, 0);
        enum_node->members.push_back({mname, mval});
        current_val++;

        if (match(TokenType::TOKEN_COMMA)) continue;
        break;
    }
    expect(TokenType::TOKEN_RBRACE, "Expected '}' closing enum member list");

    std::string type_name = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected typedef type identifier");
    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after typedef declaration");

    typedef_map[type_name] = enum_node;
    return std::make_unique<TypedefNode>(type_name, enum_node);
}

std::unique_ptr<ClassNode> Parser::parse_class(bool is_interface_class) {
    expect(TokenType::TOKEN_CLASS, "Expected 'class'");
    std::string class_name = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected class name");

    auto cls = std::make_unique<ClassNode>(class_name);
    cls->is_interface_class = is_interface_class;

    if (match(TokenType::TOKEN_HASH)) {
        expect(TokenType::TOKEN_LPAREN, "Expected '(' after '#' in class parameter list");
        cls->is_parameterized = true;
        while (current.type != TokenType::TOKEN_RPAREN && current.type != TokenType::TOKEN_EOF) {
            if (current.type == TokenType::TOKEN_PARAMETER || current.type == TokenType::TOKEN_LOCALPARAM) {
                advance();
            }
            if (current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_LOGIC ||
                current.type == TokenType::TOKEN_BIT || current.type == TokenType::TOKEN_REG ||
                current.type == TokenType::TOKEN_WIRE ||
                (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint"))) {
                advance();
            }
            std::string pname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter identifier");
            std::unique_ptr<ExpressionNode> pexpr = nullptr;
            if (match(TokenType::TOKEN_EQUAL)) {
                pexpr = parse_expression();
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
                cls->extended_interfaces.push_back(parent_iface);
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
            cls->base_class_name = base_class_name;
        }
        if (match(TokenType::TOKEN_IMPLEMENTS)) {
            while (true) {
                std::string iface_name = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected interface class name after 'implements'");
                cls->implemented_interfaces.push_back(iface_name);
                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
        }
    }
    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after class name");

    while (current.type != TokenType::TOKEN_ENDCLASS && current.type != TokenType::TOKEN_EOF) {
        if (current.type == TokenType::TOKEN_SPECIFY) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: 'specify' block is not permitted inside class per IEEE 1800-2023 section 32");
        }
        if (current.type == TokenType::TOKEN_SYS_SETUP || current.type == TokenType::TOKEN_SYS_HOLD ||
            current.type == TokenType::TOKEN_SYS_WIDTH || current.type == TokenType::TOKEN_SYS_RECOVERY) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: timing check system task '" + current.text + "' is not permitted inside class per IEEE 1800-2023 section 31.8 & 32.7");
        }
        bool is_pure = false;
        bool is_virt = false;
        bool is_stat = false;
        if (match(TokenType::TOKEN_PURE)) {
            is_pure = true;
            if (match(TokenType::TOKEN_VIRTUAL)) {
                is_virt = true;
            } else {
                throw std::runtime_error("[CAS-DTINPCIL] Expected 'virtual' after 'pure' per IEEE 1800-2023 section 8.26");
            }
        } else if (match(TokenType::TOKEN_VIRTUAL)) {
            is_virt = true;
            if (match(TokenType::TOKEN_PURE)) {
                is_pure = true;
            }
        } else if (match(TokenType::TOKEN_STATIC)) {
            is_stat = true;
        }

        if (current.type == TokenType::TOKEN_FUNCTION) {
            advance();
            if (is_interface_class && (!is_pure || !is_virt)) {
                throw std::runtime_error("[CAS-DTINPCIL] Interface class '" + class_name + "' can only contain pure virtual method declarations per IEEE 1800-2023 section 8.26");
            }
            auto fn = parse_function(is_pure);
            if (fn) {
                if (is_interface_class && fn->name == "new") {
                    throw std::runtime_error("[CAS-DTINPCIL] Interface class '" + class_name + "' cannot define constructor 'new' per IEEE 1800-2023 section 8.26");
                }
                fn->is_pure = is_pure;
                fn->is_virtual = is_virt;
                fn->is_static = is_stat || fn->is_static;
                cls->add_function(std::move(fn));
            }
            continue;
        } else if (current.type == TokenType::TOKEN_TASK) {
            advance();
            if (is_interface_class && (!is_pure || !is_virt)) {
                throw std::runtime_error("[CAS-DTINPCIL] Interface class '" + class_name + "' can only contain pure virtual method declarations per IEEE 1800-2023 section 8.26");
            }
            auto t = parse_task(is_pure);
            if (t) {
                t->is_pure = is_pure;
                t->is_virtual = is_virt;
                t->is_static = is_stat || t->is_static;
                cls->add_task(std::move(t));
            }
            continue;
        }

        if (is_interface_class) {
            throw std::runtime_error("[CAS-DTINPCIL] Interface class '" + class_name + "' can only contain pure virtual method declarations per IEEE 1800-2023 section 8.26");
        }
        if (current.type == TokenType::TOKEN_CONSTRAINT) {
            advance();
            std::string cname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected constraint name");
            expect(TokenType::TOKEN_LBRACE, "Expected '{' after constraint name");
            std::vector<std::unique_ptr<ExpressionNode>> exprs;
            while (current.type != TokenType::TOKEN_RBRACE && current.type != TokenType::TOKEN_EOF) {
                auto expr = parse_expression();
                if (expr) {
                    exprs.push_back(std::move(expr));
                }
                match(TokenType::TOKEN_SEMICOLON);
            }
            expect(TokenType::TOKEN_RBRACE, "Expected '}' closing constraint block");
            cls->add_constraint(std::make_unique<ConstraintBlockNode>(cname, std::move(exprs)));
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
        } else {
            bool is_rand = false;
            bool is_randc = false;
            if (match(TokenType::TOKEN_RAND)) {
                is_rand = true;
            } else if (match(TokenType::TOKEN_RANDC)) {
                is_randc = true;
            }

            if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_BIT ||
                current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_WIRE ||
                current.type == TokenType::TOKEN_INTEGER ||
                (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint"))) {
                bool is_s = (current.type == TokenType::TOKEN_INTEGER || (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint")));
                advance();
                if (match(TokenType::TOKEN_SIGNED)) is_s = true;
                uint32_t width = is_s ? 32 : 1;
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
                    if (match(TokenType::TOKEN_LBRACKET)) {
                        if (match(TokenType::TOKEN_RBRACKET)) {
                            is_dyn = true;
                        } else {
                            parse_expression();
                            if (match(TokenType::TOKEN_COLON)) {
                                parse_expression();
                            }
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
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
                    mvar.default_expr = std::move(def_val);
                    mvar.is_rand = is_rand;
                    mvar.is_randc = is_randc;
                    mvar.is_static = is_stat;
                    if (mvar_msb) mvar.msb_expr = mvar_msb->clone();
                    if (mvar_lsb) mvar.lsb_expr = mvar_lsb->clone();
                    cls->add_member_var(std::move(mvar));
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after member variable declaration");
            } else if (current.type == TokenType::TOKEN_IDENTIFIER && is_class(current.text)) {
                advance();
                while (true) {
                    std::string var_name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected member variable name");
                    ClassMemberVar mvar;
                    mvar.name = var_name;
                    mvar.width = 64;
                    mvar.is_signed = false;
                    mvar.is_rand = is_rand;
                    mvar.is_randc = is_randc;
                    mvar.is_static = is_stat;
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
        if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
    }
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

    auto it = class_map.find(base_name);
    ClassNode* base_cls = (it != class_map.end()) ? it->second.get() : nullptr;

    if (!base_cls) {
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
            pos_args.push_back(parse_expression());
        }
        if (match(TokenType::TOKEN_COMMA)) continue;
        break;
    }
    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing class specialization arguments");

    std::unordered_map<std::string, uint64_t> resolved;
    for (const auto& p : base_cls->parameters) {
        if (p.default_expr) resolved[p.name] = eval_const_expr(p.default_expr.get());
        else resolved[p.name] = 0;
    }
    for (size_t i = 0; i < pos_args.size() && i < base_cls->parameters.size(); ++i) {
        resolved[base_cls->parameters[i].name] = eval_const_expr(pos_args[i].get());
    }
    for (const auto& na : named_args) {
        resolved[na.first] = eval_const_expr(na.second.get());
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
                                auto low_expr = parse_expression();
                                expect(TokenType::TOKEN_COLON, "Expected ':' in bin range");
                                auto high_expr = parse_expression();
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
        expect(TokenType::TOKEN_RPAREN, "Expected ')' closing sequence clock");
    }

    auto seq_expr = parse_sequence_expression();
    if (auto concat = dynamic_cast<SequenceConcatNode*>(seq_expr.get())) {
        for (auto& item : concat->elements) {
            seq->items.push_back({item.delay, std::move(item.expr)});
        }
    } else {
        seq->items.push_back({0, std::move(seq_expr)});
    }

    match(TokenType::TOKEN_SEMICOLON);
    expect(TokenType::TOKEN_ENDSEQUENCE, "Expected 'endsequence'");
    if (match(TokenType::TOKEN_COLON)) {
        if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
    }
    return seq;
}

std::unique_ptr<AssertPropertyStatementNode> Parser::parse_assert_property(const std::string& label) {
    expect(TokenType::TOKEN_ASSERT, "Expected 'assert'");
    expect(TokenType::TOKEN_PROPERTY, "Expected 'property' after 'assert'");
    expect(TokenType::TOKEN_LPAREN, "Expected '(' after 'assert property'");

    auto assert_node = std::make_unique<AssertPropertyStatementNode>();
    assert_node->label = label;

    if (current.type == TokenType::TOKEN_IDENTIFIER && peek_token().type == TokenType::TOKEN_RPAREN) {
        assert_node->property_name = current.text;
        advance();
    } else {
        auto inlined = std::make_unique<PropertyNode>();
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
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing clock");
        }
        if (match(TokenType::TOKEN_DISABLE)) {
            expect(TokenType::TOKEN_IFF, "Expected 'iff'");
            expect(TokenType::TOKEN_LPAREN, "Expected '('");
            inlined->disable_expr = parse_expression();
            expect(TokenType::TOKEN_RPAREN, "Expected ')'");
        }
        auto first_expr = parse_sequence_expression();
        if (match(TokenType::TOKEN_NON_OVERLAP_IMP)) {
            inlined->imp_type = ImplicationType::NON_OVERLAPPING;
            inlined->antecedent = std::move(first_expr);
            inlined->consequent = parse_sequence_expression();
        } else if (match(TokenType::TOKEN_OVERLAP_IMP)) {
            inlined->imp_type = ImplicationType::OVERLAPPING;
            inlined->antecedent = std::move(first_expr);
            inlined->consequent = parse_sequence_expression();
        } else {
            inlined->imp_type = ImplicationType::NONE;
            inlined->antecedent = std::move(first_expr);
        }
        assert_node->inlined_property = std::move(inlined);
    }
    expect(TokenType::TOKEN_RPAREN, "Expected ')' closing assert property");

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
    pkg->defined_in_file = current.filename;
    pkg->defined_on_line = current.line;
    pkg->name = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected package name");
    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after package name");

    global_packages[pkg->name] = pkg;

    while (current.type != TokenType::TOKEN_ENDPACKAGE && current.type != TokenType::TOKEN_EOF) {
        if (current.type == TokenType::TOKEN_SPECIFY) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: 'specify' block is not permitted inside package per IEEE 1800-2023 section 32");
        }
        if (current.type == TokenType::TOKEN_SYS_SETUP || current.type == TokenType::TOKEN_SYS_HOLD ||
            current.type == TokenType::TOKEN_SYS_WIDTH || current.type == TokenType::TOKEN_SYS_RECOVERY) {
            throw std::runtime_error("[CAS-DTINPCIL] Dynamic type in non-procedural context: timing check system task '" + current.text + "' is not permitted inside package per IEEE 1800-2023 section 31.8 & 32.7");
        }
        if (current.type == TokenType::TOKEN_MODULE || current.type == TokenType::TOKEN_INTERFACE) {
            std::string loc = current.filename.empty() 
                ? ("line " + std::to_string(current.line)) 
                : (current.filename + ":" + std::to_string(current.line));
            throw std::runtime_error(loc + ": error: [CAS-DTINPCIL] Module or interface declarations and instantiations are illegal in package scope");
        }

        if (current.type == TokenType::TOKEN_PARAMETER || current.type == TokenType::TOKEN_LOCALPARAM) {
            advance();
            if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
                current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_INTEGER ||
                current.type == TokenType::TOKEN_BIT ||
                (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint"))) {
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

                uint64_t val = eval_const_expr_with_params(val_expr.get(), pkg->param_constants);
                pkg->param_constants[pname] = val;

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

        if (current.type == TokenType::TOKEN_TYPEDEF) {
            auto td = parse_typedef();
            if (td) {
                if (td->enum_type) {
                    pkg->enum_typedefs[td->type_name] = td->enum_type;
                    for (const auto& m : td->enum_type->members) {
                        pkg->enum_constant_map[m.first] = m.second;
                    }
                }
                if (td->struct_type) {
                    pkg->struct_typedefs[td->type_name] = td->struct_type;
                }
                pkg->typedefs.push_back(std::move(td));
            }
            continue;
        }

        if (match(TokenType::TOKEN_FUNCTION)) {
            auto fn = parse_function();
            if (fn) {
                pkg->function_map[fn->name] = fn.get();
                pkg->functions.push_back(std::move(fn));
            }
            continue;
        }

        if (match(TokenType::TOKEN_TASK)) {
            auto t = parse_task();
            if (t) {
                pkg->task_map[t->name] = t.get();
                pkg->tasks.push_back(std::move(t));
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
            }
            continue;
        }

        if (current.type == TokenType::TOKEN_CLASS) {
            auto cls = parse_class();
            if (cls) {
                std::shared_ptr<ClassNode> scls = std::move(cls);
                pkg->class_map[scls->name] = scls.get();
                pkg->classes.push_back(scls);
            }
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
            if (next.type == TokenType::TOKEN_IDENTIFIER || next.type == TokenType::TOKEN_HASH) {
                std::string loc = current.filename.empty() 
                    ? ("line " + std::to_string(current.line)) 
                    : (current.filename + ":" + std::to_string(current.line));
                throw std::runtime_error(loc + ": error: [CAS-DTINPCIL] Module or interface declarations and instantiations are illegal in package scope");
            }
            advance();
            continue;
        }

        advance();
    }

    expect(TokenType::TOKEN_ENDPACKAGE, "Expected 'endpackage'");
    if (match(TokenType::TOKEN_COLON)) {
        if (current.type == TokenType::TOKEN_IDENTIFIER) {
            advance();
        }
    }
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
    if (match(TokenType::TOKEN_GENVAR)) {
        // inline genvar
    }
    std::string gname = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected genvar identifier in for loop header");
    mod->genvars.insert(gname);
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
        std::string target = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected target identifier in assign");
        while (match(TokenType::TOKEN_DOT)) {
            target += "." + current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in assign target");
        }
        std::unique_ptr<ExpressionNode> target_idx = nullptr;
        if (match(TokenType::TOKEN_LBRACKET)) {
            target_idx = parse_expression();
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in assign target");
        }
        expect(TokenType::TOKEN_EQUAL, "Expected '=' in assign statement");
        auto rhs = parse_expression();
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after assign");
        auto ca = std::make_unique<ContinuousAssignNode>(target, std::move(rhs));
        ca->target_index_expr = std::move(target_idx);
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
            expect(TokenType::TOKEN_LPAREN, "Expected '(' after '@'");
            auto proc = std::make_unique<ProcessNode>(ProcessType::SEQUENTIAL);
            while (true) {
                EdgeType edge = EdgeType::LEVEL;
                if (match(TokenType::TOKEN_POSEDGE)) edge = EdgeType::POSEDGE;
                else if (match(TokenType::TOKEN_NEGEDGE)) edge = EdgeType::NEGEDGE;
                std::string sig = current.text;
                expect(TokenType::TOKEN_IDENTIFIER, "Expected signal name in sensitivity list");
                while (match(TokenType::TOKEN_DOT)) {
                    sig += "." + current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected identifier after '.' in sensitivity list");
                }
                proc->sensitivity_list.push_back({edge, sig});
                if (match(TokenType::TOKEN_OR) || match(TokenType::TOKEN_GATE_OR) || match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_RPAREN, "Expected ')' closing sensitivity list");
            proc->body = parse_statement_or_block();
            if (blk) blk->processes.push_back(std::move(proc));
            else mod->processes.push_back(std::move(proc));
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
    if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE || 
        current.type == TokenType::TOKEN_WAND  || current.type == TokenType::TOKEN_WOR  || 
        current.type == TokenType::TOKEN_TRI   || current.type == TokenType::TOKEN_REG  || 
        current.type == TokenType::TOKEN_INTEGER || current.type == TokenType::TOKEN_BIT ||
        (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte" || current.text == "shortint" || current.text == "longint"))) {
        
        NetKind pkind = NetKind::VAR;
        if (current.type == TokenType::TOKEN_WIRE) pkind = NetKind::WIRE;
        else if (current.type == TokenType::TOKEN_WAND) pkind = NetKind::WAND;
        else if (current.type == TokenType::TOKEN_WOR) pkind = NetKind::WOR;
        else if (current.type == TokenType::TOKEN_TRI) pkind = NetKind::TRI;
        std::string tname = current.text;
        advance();

        std::unique_ptr<ExpressionNode> msb = nullptr;
        std::unique_ptr<ExpressionNode> lsb = nullptr;
        if (match(TokenType::TOKEN_LBRACKET)) {
            msb = parse_expression();
            expect(TokenType::TOKEN_COLON, "Expected ':' in signal range");
            lsb = parse_expression();
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in signal range");
        }

        while (true) {
            std::string sname = current.text;
            expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier");
            PortNode pnode(sname, "internal", msb ? msb->clone() : nullptr, lsb ? lsb->clone() : nullptr, false, pkind);
            pnode.type_name = tname;
            while (match(TokenType::TOKEN_LBRACKET)) {
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
            if (blk) blk->signals.push_back(std::move(pnode));
            else mod->signals.push_back(std::move(pnode));
            if (match(TokenType::TOKEN_COMMA)) continue;
            break;
        }
        expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after signal declaration");
        return;
    }
    advance();
}
