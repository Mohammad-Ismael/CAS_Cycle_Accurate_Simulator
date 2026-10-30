#include "../../include/frontend/parser.hpp"
#include <stdexcept>

std::vector<std::unique_ptr<ModuleNode>> Parser::parse_all() {
    std::vector<std::unique_ptr<ModuleNode>> modules;

    while (current.type != TokenType::TOKEN_EOF) {
        if (current.type == TokenType::TOKEN_PARAMETER || current.type == TokenType::TOKEN_LOCALPARAM) {
            advance();
            
            if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
                current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_INTEGER ||
                (current.type == TokenType::TOKEN_IDENTIFIER && (current.text == "int" || current.text == "byte"))) {
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

        if (current.type == TokenType::TOKEN_TYPEDEF) {
            auto td = parse_typedef();
            if (td) global_typedefs.push_back(std::move(td));
            continue;
        }

        if (current.type == TokenType::TOKEN_CLASS) {
            auto cls = parse_class();
            if (cls) {
                std::shared_ptr<ClassNode> scls = std::move(cls);
                class_map[scls->name] = scls;
                global_classes.push_back(scls);
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

        bool is_iface = false;
        if (current.type == TokenType::TOKEN_INTERFACE) {
            is_iface = true;
            advance();
        } else {
            expect(TokenType::TOKEN_MODULE, "Expected 'module', 'interface', 'parameter', or 'localparam' keyword");
        }
        auto mod = is_iface ? std::make_unique<InterfaceNode>() : std::make_unique<ModuleNode>();
        mod->defined_in_file = current.filename;
        mod->defined_on_line = current.line;
        mod->name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, is_iface ? "Expected interface name" : "Expected module name");
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
                NetKind port_kind = (dir == "input" || dir == "inout") ? NetKind::WIRE : NetKind::WIRE;
                if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
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
                }

                while (true) {
                    std::string pname = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected port identifier");
                    PortNode port(pname, dir, msb ? msb->clone() : nullptr, lsb ? lsb->clone() : nullptr, is_signed_port, port_kind);
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

        TokenType end_token = is_iface ? TokenType::TOKEN_ENDINTERFACE : TokenType::TOKEN_ENDMODULE;
        while (current.type != end_token && current.type != TokenType::TOKEN_EOF) {
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
                advance();
                if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
                    current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_INTEGER) {
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
                bool is_signed_port = false;
                if (current.type == TokenType::TOKEN_SIGNED) {
                    is_signed_port = true;
                    advance();
                }
                NetKind port_kind = (dir == "input" || dir == "inout") ? NetKind::WIRE : NetKind::WIRE;
                if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
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
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        mod->ports.emplace_back(pname, dir, msb ? msb->clone() : nullptr, lsb ? lsb->clone() : nullptr, is_signed_port, port_kind);
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
                std::string class_type = current.text;
                advance();
                while (true) {
                    std::string signame = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected class handle identifier");
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

            if (current.type == TokenType::TOKEN_IDENTIFIER && is_typedef(current.text)) {
                std::string type_name = current.text;
                auto enum_info = is_enum_typedef(type_name) ? typedef_map[type_name] : nullptr;
                auto struct_info = is_struct_typedef(type_name) ? struct_typedef_map[type_name] : nullptr;
                advance();

                uint32_t width = struct_info ? struct_info->total_width : (enum_info ? enum_info->base_width : 32);
                bool is_signed_sig = struct_info ? struct_info->is_signed : (enum_info ? enum_info->is_signed : false);

                auto msb = std::make_unique<NumberNode>(width > 0 ? width - 1 : 0, 32);
                auto lsb = std::make_unique<NumberNode>(0, 32);

                while (true) {
                    std::string signame = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier");
                    PortNode sig(signame, "internal", msb ? msb->clone() : nullptr, lsb ? lsb->clone() : nullptr, is_signed_sig, NetKind::VAR);
                    sig.type_name = type_name;

                    if (match(TokenType::TOKEN_LBRACKET)) {
                        if (match(TokenType::TOKEN_DOLLAR)) {
                            sig.is_queue = true;
                            if (match(TokenType::TOKEN_COLON)) {
                                sig.queue_bound_expr = parse_expression();
                            }
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                        } else if (match(TokenType::TOKEN_RBRACKET)) {
                            sig.is_dynamic_array = true;
                        } else if (is_assoc_key_token(current)) {
                            sig.is_associative_array = true;
                            parse_assoc_key_type(sig.assoc_key_type, sig.assoc_key_width);
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in associative array dimension");
                        } else {
                            sig.is_array = true;
                            sig.bound1_expr = parse_expression();
                            if (match(TokenType::TOKEN_COLON)) {
                                sig.bound2_expr = parse_expression();
                            }
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                        }
                    }

                    if (match(TokenType::TOKEN_EQUAL)) {
                        auto init_expr = parse_expression();
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

            if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_WIRE || 
                current.type == TokenType::TOKEN_WAND  || current.type == TokenType::TOKEN_WOR  || 
                current.type == TokenType::TOKEN_TRI   ||
                current.type == TokenType::TOKEN_REG   || current.type == TokenType::TOKEN_INTEGER || 
                current.type == TokenType::TOKEN_BIT   || current.type == TokenType::TOKEN_REAL ||
                current.type == TokenType::TOKEN_SIGNED) {
                
                bool is_signed_sig = false;
                if (current.type == TokenType::TOKEN_SIGNED) {
                    is_signed_sig = true;
                    advance();
                }
                bool is_int = (current.type == TokenType::TOKEN_INTEGER);
                if (is_int) is_signed_sig = true;
                bool is_real_sig = (current.type == TokenType::TOKEN_REAL);

                bool is_bit_type = (current.type == TokenType::TOKEN_BIT);
                NetKind sig_kind = NetKind::WIRE;
                if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || current.type == TokenType::TOKEN_BIT || is_int || is_real_sig) {
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

                if (current.type == TokenType::TOKEN_SIGNED) {
                    is_signed_sig = true;
                    advance();
                }

                std::unique_ptr<ExpressionNode> msb = nullptr;
                std::unique_ptr<ExpressionNode> lsb = nullptr;
                if (match(TokenType::TOKEN_LBRACKET)) {
                    msb = parse_expression();
                    expect(TokenType::TOKEN_COLON, "Expected ':' in range");
                    lsb = parse_expression();
                    expect(TokenType::TOKEN_RBRACKET, "Expected ']' in range");
                } else if (is_int) {
                    msb = std::make_unique<NumberNode>(31, 32);
                    lsb = std::make_unique<NumberNode>(0, 32);
                } else if (is_real_sig) {
                    msb = std::make_unique<NumberNode>(63, 32);
                    lsb = std::make_unique<NumberNode>(0, 32);
                }
                while (true) {
                    std::string signame = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected signal identifier");
                    PortNode sig(signame, "internal", msb ? msb->clone() : nullptr, lsb ? lsb->clone() : nullptr, is_signed_sig, sig_kind);
                    if (is_bit_type) sig.type_name = "bit";
                    if (is_real_sig) sig.type_name = "real";

                    if (match(TokenType::TOKEN_LBRACKET)) {
                        if (match(TokenType::TOKEN_DOLLAR)) {
                            sig.is_queue = true;
                            if (match(TokenType::TOKEN_COLON)) {
                                sig.queue_bound_expr = parse_expression();
                            }
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                        } else if (match(TokenType::TOKEN_RBRACKET)) {
                            sig.is_dynamic_array = true;
                        } else if (is_assoc_key_token(current)) {
                            sig.is_associative_array = true;
                            parse_assoc_key_type(sig.assoc_key_type, sig.assoc_key_width);
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in associative array dimension");
                        } else {
                            sig.is_array = true;
                            sig.bound1_expr = parse_expression();
                            if (match(TokenType::TOKEN_COLON)) {
                                sig.bound2_expr = parse_expression();
                            }
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in dimension");
                        }
                    }

                    if (match(TokenType::TOKEN_EQUAL)) {
                        auto init_expr = parse_expression();
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

            if (match(TokenType::TOKEN_INITIAL)) {
                auto proc = std::make_unique<ProcessNode>(ProcessType::INITIAL);
                proc->body = parse_statement_or_block();
                mod->processes.push_back(std::move(proc));
                continue;
            }

            if (match(TokenType::TOKEN_ALWAYS_COMB)) {
                auto proc = std::make_unique<ProcessNode>(ProcessType::COMBINATIONAL);
                proc->is_always_comb = true;
                proc->body = parse_statement_or_block();
                mod->processes.push_back(std::move(proc));
                continue;
            }

            if (match(TokenType::TOKEN_ALWAYS_LATCH)) {
                auto proc = std::make_unique<ProcessNode>(ProcessType::COMBINATIONAL);
                proc->is_always_latch = true;
                proc->body = parse_statement_or_block();
                mod->processes.push_back(std::move(proc));
                continue;
            }

            if (match(TokenType::TOKEN_ALWAYS_FF)) {
                auto proc = parse_always_ff();
                proc->is_always_ff = true;
                mod->processes.push_back(std::move(proc));
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
        } else {
            expect(TokenType::TOKEN_ENDMODULE, "Expected 'endmodule'");
        }
        if (match(TokenType::TOKEN_COLON)) {
            if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
        }
        match(TokenType::TOKEN_SEMICOLON);
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

std::unique_ptr<ClassNode> Parser::parse_class() {
    expect(TokenType::TOKEN_CLASS, "Expected 'class'");
    std::string class_name = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected class name");
    std::string base_class_name = "";
    if (match(TokenType::TOKEN_EXTENDS)) {
        base_class_name = current.text;
        expect(TokenType::TOKEN_IDENTIFIER, "Expected base class name after 'extends'");
    }
    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after class name");

    auto cls = std::make_unique<ClassNode>(class_name, base_class_name);

    while (current.type != TokenType::TOKEN_ENDCLASS && current.type != TokenType::TOKEN_EOF) {
        bool is_virt = false;
        if (match(TokenType::TOKEN_VIRTUAL)) {
            is_virt = true;
        }
        if (current.type == TokenType::TOKEN_FUNCTION) {
            advance();
            auto fn = parse_function();
            if (fn) {
                fn->is_virtual = is_virt;
                cls->add_function(std::move(fn));
            }
        } else if (current.type == TokenType::TOKEN_TASK) {
            advance();
            auto t = parse_task();
            if (t) {
                t->is_virtual = is_virt;
                cls->add_task(std::move(t));
            }
        } else if (current.type == TokenType::TOKEN_CONSTRAINT) {
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
                }
                while (true) {
                    std::string var_name = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected member variable name");
                    std::unique_ptr<ExpressionNode> def_val = nullptr;
                    if (match(TokenType::TOKEN_EQUAL)) {
                        def_val = parse_expression();
                    }
                    ClassMemberVar mvar;
                    mvar.name = var_name;
                    mvar.width = width;
                    mvar.is_signed = is_s;
                    mvar.default_expr = std::move(def_val);
                    mvar.is_rand = is_rand;
                    mvar.is_randc = is_randc;
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


std::unique_ptr<CovergroupNode> Parser::parse_covergroup() {
    expect(TokenType::TOKEN_COVERGROUP, "Expected 'covergroup'");
    std::string cg_name = current.text;
    expect(TokenType::TOKEN_IDENTIFIER, "Expected covergroup name");

    if (match(TokenType::TOKEN_LPAREN)) {
        int depth = 1;
        while (depth > 0 && current.type != TokenType::TOKEN_EOF) {
            if (current.type == TokenType::TOKEN_LPAREN) depth++;
            else if (current.type == TokenType::TOKEN_RPAREN) depth--;
            advance();
        }
    }
    if (match(TokenType::TOKEN_AT)) {
        if (match(TokenType::TOKEN_LPAREN)) {
            int depth = 1;
            while (depth > 0 && current.type != TokenType::TOKEN_EOF) {
                if (current.type == TokenType::TOKEN_LPAREN) depth++;
                else if (current.type == TokenType::TOKEN_RPAREN) depth--;
                advance();
            }
        }
    }
    expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after covergroup header");

    auto cg = std::make_unique<CovergroupNode>(cg_name);

    while (current.type != TokenType::TOKEN_ENDGROUP && current.type != TokenType::TOKEN_EOF) {
        std::string label = "";
        if (current.type == TokenType::TOKEN_IDENTIFIER) {
            Token next_tok = peek_token();
            if (next_tok.type == TokenType::TOKEN_COLON) {
                label = current.text;
                advance();
                advance();
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
