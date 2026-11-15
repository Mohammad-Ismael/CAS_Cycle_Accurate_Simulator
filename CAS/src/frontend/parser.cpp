#include "../../include/frontend/parser.hpp"
#include <stdexcept>
#include <fstream>
#include <sstream>

std::unordered_map<std::string, std::shared_ptr<PackageNode>> Parser::global_packages;
std::unordered_map<std::string, std::shared_ptr<ConfigNode>> Parser::global_configs;
std::unordered_map<std::string, std::shared_ptr<UdpDefinitionNode>> Parser::global_udp_defs;
std::vector<std::string> Parser::global_wildcard_imports;
std::unordered_map<std::string, std::pair<std::string, std::string>> Parser::global_explicit_imports;
std::unordered_map<std::string, std::shared_ptr<ClassNode>> Parser::class_map;
std::vector<std::shared_ptr<ClassNode>> Parser::global_classes;
std::unordered_map<std::string, std::shared_ptr<LetDeclNode>> Parser::global_let_decls;
std::unordered_set<std::string> Parser::global_forward_classes;
std::unordered_set<std::string> Parser::global_forward_interface_classes;

Parser::Parser(Lexer& lex) : lexer(lex) {
    advance();
}

void Parser::advance() {
    if (has_peek) {
        current = peeked;
        has_peek = false;
    } else {
        current = lexer.next_token();
    }
}

Token Parser::peek_token() {
    if (!has_peek) {
        peeked = lexer.next_token();
        has_peek = true;
    }
    return peeked;
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
        std::string loc = current.filename.empty() 
            ? ("line " + std::to_string(current.line)) 
            : (current.filename + ":" + std::to_string(current.line));
        throw std::runtime_error(loc + ": error: " + err + " (found: '" + current.text + "')");
    }
    advance();
}

void Parser::parse_assoc_key_type(std::string& key_type, uint32_t& key_width) {
    if (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG ||
        current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_BIT ||
        (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "bit")) {
        key_type = current.text;
        advance();
        key_width = 1;
        if (match(TokenType::TOKEN_LBRACKET)) {
            auto msb_e = parse_expression();
            expect(TokenType::TOKEN_COLON, "Expected ':' in range");
            auto lsb_e = parse_expression();
            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in range");
            if (auto nm = dynamic_cast<NumberNode*>(msb_e.get())) {
                if (auto nl = dynamic_cast<NumberNode*>(lsb_e.get())) {
                    key_width = (nm->aval >= nl->aval) ? (uint32_t)(nm->aval - nl->aval + 1) : (uint32_t)(nl->aval - nm->aval + 1);
                }
            }
        }
    } else if (current.type == TokenType::TOKEN_INTEGER || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "int")) {
        key_type = current.text;
        key_width = 32;
        advance();
    } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "byte") {
        key_type = "byte";
        key_width = 8;
        advance();
    } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "shortint") {
        key_type = "shortint";
        key_width = 16;
        advance();
    } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "longint") {
        key_type = "longint";
        key_width = 64;
        advance();
    } else if (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "string") {
        key_type = "string";
        key_width = 0;
        advance();
    } else if (current.type == TokenType::TOKEN_STAR) {
        key_type = "*";
        key_width = 64;
        advance();
    } else if (current.type == TokenType::TOKEN_IDENTIFIER && is_type_identifier(current.text)) {
        key_type = current.text;
        auto en_def = get_enum_typedef(key_type);
        auto st_def = get_struct_typedef(key_type);
        if (en_def) {
            key_width = en_def->base_width;
        } else if (st_def) {
            key_width = st_def->total_width;
        } else {
            key_width = 64;
        }
        advance();
    } else {
        throw std::runtime_error("Unknown associative array index type: " + current.text);
    }
}

void Parser::ensure_package_loaded(const std::string& pkg_name) {
    if (global_packages.find(pkg_name) != global_packages.end()) return;
    if (pkg_name == "uvm_pkg") {
        std::string content = "";
        static const std::vector<std::string> search_paths = {
            "/app/CAS/include/std_sv/uvm_pkg.sv",
            "CAS/include/std_sv/uvm_pkg.sv",
            "../include/std_sv/uvm_pkg.sv",
            "include/std_sv/uvm_pkg.sv"
        };
        for (const auto& sp : search_paths) {
            std::ifstream f(sp);
            if (f.is_open()) {
                std::stringstream ss;
                ss << f.rdbuf();
                content = ss.str();
                break;
            }
        }
        if (content.empty()) {
            content = R"(
package uvm_pkg;

typedef enum int {
    UVM_NONE   = 0,
    UVM_LOW    = 100,
    UVM_MEDIUM = 200,
    UVM_HIGH   = 300,
    UVM_FULL   = 400,
    UVM_DEBUG  = 500
} uvm_verbosity;

typedef enum int {
    UVM_PASSIVE = 0,
    UVM_ACTIVE  = 1
} uvm_active_passive_enum;

class uvm_object;
    string m_name;
    function new(string name = "");
        m_name = name;
    endfunction
    virtual function string get_name();
        return m_name;
    endfunction
    virtual function string get_full_name();
        return m_name;
    endfunction
    virtual function string get_type_name();
        return m_name;
    endfunction
endclass

class uvm_sequence_item extends uvm_object;
    function new(string name = "uvm_sequence_item");
        super.new(name);
    endfunction
endclass

class uvm_sequence extends uvm_sequence_item;
    function new(string name = "uvm_sequence");
        super.new(name);
    endfunction
    virtual task pre_body(); endtask
    virtual task pre_do(bit is_item); endtask
    virtual function void mid_do(uvm_sequence_item this_item); endfunction
    virtual task body(); endtask
    virtual function void post_do(uvm_sequence_item this_item); endfunction
    virtual task post_body(); endtask
endclass

class uvm_phase extends uvm_object;
    int objection_count = 0;
    function new(string name = "run");
        super.new(name);
    endfunction
    function void raise_objection(uvm_object obj = null, string description = "", int count = 1);
        objection_count += count;
    endfunction
    function void drop_objection(uvm_object obj = null, string description = "", int count = 1);
        objection_count -= count;
    endfunction
endclass

class uvm_component extends uvm_object;
    uvm_component m_parent;
    static uvm_component m_all_components[$];
    static uvm_component m_top_env;

    function new(string name = "", uvm_component parent = null);
        super.new(name);
        m_parent = parent;
        m_all_components.push_back(this);
        m_top_env = this;
    endfunction

    virtual function void build_phase(uvm_phase phase);
    endfunction

    virtual function void connect_phase(uvm_phase phase);
    endfunction

    virtual task run_phase(uvm_phase phase);
    endtask
endclass

class uvm_test extends uvm_component;
    function new(string name = "", uvm_component parent = null);
        super.new(name, parent);
    endfunction
endclass

class uvm_env extends uvm_component;
    function new(string name = "", uvm_component parent = null);
        super.new(name, parent);
    endfunction
endclass

class uvm_agent extends uvm_component;
    function new(string name = "", uvm_component parent = null);
        super.new(name, parent);
    endfunction
endclass

class uvm_driver #(type REQ = uvm_sequence_item, type RSP = REQ) extends uvm_component;
    function new(string name = "", uvm_component parent = null);
        super.new(name, parent);
    endfunction
endclass

class uvm_sequencer #(type REQ = uvm_sequence_item, type RSP = REQ) extends uvm_component;
    function new(string name = "", uvm_component parent = null);
        super.new(name, parent);
    endfunction
endclass

class uvm_monitor extends uvm_component;
    function new(string name = "", uvm_component parent = null);
        super.new(name, parent);
    endfunction
endclass

class uvm_scoreboard extends uvm_component;
    function new(string name = "", uvm_component parent = null);
        super.new(name, parent);
    endfunction
endclass

class uvm_analysis_port #(type T = int) extends uvm_object;
    function new(string name = "", uvm_component parent = null);
        super.new(name);
    endfunction
    function void write(T t); endfunction
endclass

class uvm_resource_db #(type T = int);
    static T m_db[string];

    static function void set(string scope, string name, T val, uvm_object accessor = null);
        m_db[{scope, "::", name}] = val;
    endfunction

    static function bit read_by_name(string scope, string name, inout T val, uvm_object accessor = null);
        string key = {scope, "::", name};
        if (m_db.exists(key)) begin
            val = m_db[key];
            return 1;
        end
        return 0;
    endfunction
endclass

task automatic run_test(string test_name = "");
    uvm_phase ph = new("run");
    uvm_component top = uvm_component::m_top_env;
    if (top != null) begin
        top.build_phase(ph);
        top.connect_phase(ph);
        top.run_phase(ph);
    end
    $finish;
endtask

endpackage
)";
        }
        Lexer lex("uvm_pkg.sv", content, "", nullptr);
        Parser p(lex);
        p.parse_packages_only();
    }
}

ClassNode* Parser::find_class(const std::string& name) const {
    if (name.rfind("uvm_", 0) == 0) {
        ensure_package_loaded("uvm_pkg");
    }
    if (current_parsing_package) {
        auto it = current_parsing_package->class_map.find(name);
        if (it != current_parsing_package->class_map.end()) return it->second;
    }
    if (current_parsing_module) {
        auto it = current_parsing_module->class_map.find(name);
        if (it != current_parsing_module->class_map.end()) return it->second;
    }
    auto it = class_map.find(name);
    if (it != class_map.end()) return it->second.get();
    for (const auto& c : global_classes) {
        if (c->name == name) return c.get();
    }
    for (const auto& p : global_packages) {
        auto pit = p.second->class_map.find(name);
        if (pit != p.second->class_map.end()) return pit->second;
    }
    return nullptr;
}

bool Parser::is_forward_class(const std::string& name) const {
    auto colon_pos = name.find("::");
    if (colon_pos != std::string::npos) {
        std::string pkg = name.substr(0, colon_pos);
        std::string cls = name.substr(colon_pos + 2);
        auto pit = global_packages.find(pkg);
        if (pit != global_packages.end()) {
            if (pit->second->forward_classes.count(cls) || pit->second->forward_interface_classes.count(cls)) return true;
        }
        return false;
    }

    if (current_parsing_package) {
        if (current_parsing_package->forward_classes.count(name) || current_parsing_package->forward_interface_classes.count(name)) return true;
    }
    if (current_parsing_module) {
        if (current_parsing_module->forward_classes.count(name) || current_parsing_module->forward_interface_classes.count(name)) return true;
        for (const auto& w : current_parsing_module->wildcard_imports) {
            auto pit = global_packages.find(w);
            if (pit != global_packages.end()) {
                if (pit->second->forward_classes.count(name) || pit->second->forward_interface_classes.count(name)) return true;
            }
        }
    }
    if (global_forward_classes.count(name) || global_forward_interface_classes.count(name)) return true;
    for (const auto& w : global_wildcard_imports) {
        auto pit = global_packages.find(w);
        if (pit != global_packages.end()) {
            if (pit->second->forward_classes.count(name) || pit->second->forward_interface_classes.count(name)) return true;
        }
    }
    for (const auto& p : global_packages) {
        if (p.second->forward_classes.count(name) || p.second->forward_interface_classes.count(name)) return true;
    }
    return false;
}

bool Parser::is_known_class(const std::string& name) const {
    if (find_class(name) != nullptr) return true;
    if (is_forward_class(name)) return true;
    return false;
}

void Parser::parse_packages_only() {
    while (current.type != TokenType::TOKEN_EOF) {
        if (match(TokenType::TOKEN_SEMICOLON)) continue;
        if (current.type == TokenType::TOKEN_PACKAGE) {
            auto pkg = parse_package();
            if (pkg) {
                global_packages[pkg->name] = pkg;
            }
        } else if (current.type == TokenType::TOKEN_CONFIG) {
            auto cfg = parse_config();
            if (cfg) {
                global_configs[cfg->name] = cfg;
            }
        } else if (current.type == TokenType::TOKEN_PRIMITIVE) {
            auto udp = parse_udp();
            if (udp) {
                global_udp_defs[udp->name] = udp;
            }
        } else if (current.type == TokenType::TOKEN_TYPEDEF) {
            auto td = parse_typedef();
            if (td) {
                if (td->struct_type) {
                    global_struct_typedefs[td->type_name] = td->struct_type;
                }
                if (td->enum_type) {
                    global_enum_typedefs[td->type_name] = td->enum_type;
                }
                global_typedefs.push_back(std::move(td));
            }
        } else if (current.type == TokenType::TOKEN_MODULE) {
            while (current.type != TokenType::TOKEN_ENDMODULE && current.type != TokenType::TOKEN_EOF) {
                advance();
            }
            if (current.type == TokenType::TOKEN_ENDMODULE) {
                advance();
                if (match(TokenType::TOKEN_COLON)) {
                    if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
                }
            }
        } else if (current.type == TokenType::TOKEN_PROGRAM) {
            while (current.type != TokenType::TOKEN_ENDPROGRAM && current.type != TokenType::TOKEN_EOF) {
                advance();
            }
            if (current.type == TokenType::TOKEN_ENDPROGRAM) {
                advance();
                if (match(TokenType::TOKEN_COLON)) {
                    if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
                }
            }
        } else if (current.type == TokenType::TOKEN_INTERFACE && peek_token().type != TokenType::TOKEN_CLASS) {
            while (current.type != TokenType::TOKEN_ENDINTERFACE && current.type != TokenType::TOKEN_EOF) {
                advance();
            }
            if (current.type == TokenType::TOKEN_ENDINTERFACE) {
                advance();
                if (match(TokenType::TOKEN_COLON)) {
                    if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
                }
            }
        } else if (current.type == TokenType::TOKEN_VIRTUAL && peek_token().type == TokenType::TOKEN_CLASS) {
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
        } else if (current.type == TokenType::TOKEN_INTERFACE && peek_token().type == TokenType::TOKEN_CLASS) {
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
        } else if (current.type == TokenType::TOKEN_CLASS) {
            auto cls = parse_class(false);
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
        } else {
            advance();
        }
    }
}
