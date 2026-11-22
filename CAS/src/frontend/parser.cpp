#include "../../include/frontend/parser.hpp"
#include <stdexcept>
#include <fstream>
#include <sstream>

std::unordered_map<std::string, std::shared_ptr<PackageNode>> Parser::global_packages;
std::string Parser::global_timeunit_val = "";
std::string Parser::global_timeprec_val = "";
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
    active_parser_instance = this;
    advance();
}

Parser::~Parser() {
    if (active_parser_instance == this) {
        active_parser_instance = nullptr;
    }
}

void Parser::advance() {
    if (!peek_queue.empty()) {
        current = peek_queue.front();
        peek_queue.pop_front();
    } else {
        current = lexer.next_token();
    }
}

Token Parser::peek_token() {
    return peek_token_n(1);
}

Token Parser::peek_token_n(size_t n) {
    while (peek_queue.size() < n) {
        peek_queue.push_back(lexer.next_token());
    }
    return peek_queue[n - 1];
}

void Parser::register_forward_type(const std::string& name) {
    forward_typedefs.insert(name);
    SimpleTypedef sm{name, "logic", 1, false};
    simple_typedef_map[name] = sm;
    if (current_parsing_package) current_parsing_package->simple_typedefs[name] = sm;
    if (current_parsing_module) current_parsing_module->simple_typedefs[name] = sm;
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
    if (match(TokenType::TOKEN_UNSIGNED) || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned") ||
        match(TokenType::TOKEN_SIGNED) || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "signed")) {
        if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
    }
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
        key_type = current.text;
        key_width = 32;
        advance();
    }
    if (match(TokenType::TOKEN_UNSIGNED) || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "unsigned") ||
        match(TokenType::TOKEN_SIGNED) || (current.type == TokenType::TOKEN_IDENTIFIER && current.text == "signed")) {
        if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
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

parameter int UVM_ALL_ON  = 32'h00000001;
parameter int UVM_DEFAULT = 32'h00000002;
parameter int UVM_BIN     = 32'h00000004;
parameter int UVM_DEC     = 32'h00000008;
parameter int UVM_OCT     = 32'h00000010;
parameter int UVM_HEX     = 32'h00000020;
parameter int UVM_STRING  = 32'h00000040;

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

class uvm_built_in_converter #(type T = int);
endclass

class uvm_sequence_item extends uvm_object;
    function new(string name = "uvm_sequence_item");
        super.new(name);
    endfunction
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

    virtual function void end_of_elaboration_phase(uvm_phase phase);
    endfunction

    virtual task run_phase(uvm_phase phase);
    endtask

    virtual function void report_phase(uvm_phase phase);
    endfunction
endclass

class uvm_sequence #(type REQ = uvm_sequence_item, type RSP = REQ) extends uvm_sequence_item;
    function new(string name = "uvm_sequence");
        super.new(name);
    endfunction
    virtual task pre_body(); endtask
    virtual task pre_do(bit is_item); endtask
    virtual function void mid_do(uvm_sequence_item this_item); endfunction
    virtual task body(); endtask
    virtual function void post_do(uvm_sequence_item this_item); endfunction
    virtual task post_body(); endtask
    virtual task start_item(uvm_sequence_item item, int set_priority = -1, uvm_component sequencer = null); endtask
    virtual task finish_item(uvm_sequence_item item, int set_priority = -1); endtask
    virtual task start(uvm_component sequencer = null, uvm_sequence_item parent_sequence = null, int this_priority = -1, bit call_pre_post = 1);
        pre_body();
        body();
        post_body();
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
    uvm_active_passive_enum is_active = UVM_ACTIVE;
    function new(string name = "", uvm_component parent = null);
        super.new(name, parent);
    endfunction
    virtual function uvm_active_passive_enum get_is_active();
        return is_active;
    endfunction
endclass

class uvm_seq_item_pull_port #(type REQ = uvm_sequence_item, type RSP = REQ) extends uvm_object;
    function new(string name = "seq_item_port", uvm_component parent = null);
        super.new(name);
    endfunction
    task get(output REQ item); endtask
    task get_next_item(output REQ item); endtask
    function void item_done(input RSP item = null); endfunction
    function void connect(uvm_object provider); endfunction
endclass

class uvm_driver #(type REQ = uvm_sequence_item, type RSP = REQ) extends uvm_component;
    uvm_seq_item_pull_port #(REQ, RSP) seq_item_port;
    REQ req;
    RSP rsp;
    function new(string name = "", uvm_component parent = null);
        super.new(name, parent);
        seq_item_port = new("seq_item_port", this);
    endfunction
endclass

class uvm_sequencer #(type REQ = uvm_sequence_item, type RSP = REQ) extends uvm_component;
    uvm_seq_item_pull_port #(REQ, RSP) seq_item_export;
    function new(string name = "", uvm_component parent = null);
        super.new(name, parent);
        seq_item_export = new("seq_item_export", this);
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
    function void connect(uvm_object provider); endfunction
endclass

class uvm_analysis_imp #(type T = int, type IMP = uvm_component) extends uvm_object;
    IMP m_imp;
    function new(string name = "", IMP imp = null);
        super.new(name);
        m_imp = imp;
    endfunction
    function void write(T t);
        if (m_imp != null) m_imp.write(t);
    endfunction
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
        return 1;
    endfunction
endclass

class uvm_config_db #(type T = int) extends uvm_resource_db #(T);
    static function void set(uvm_component cntxt, string inst_name, string field_name, T value);
        string scope = (cntxt != null) ? cntxt.get_full_name() : "";
        if (!inst_name.empty()) scope = {scope, ".", inst_name};
        uvm_resource_db#(T)::set(scope, field_name, value);
    endfunction

    static function bit get(uvm_component cntxt, string inst_name, string field_name, inout T value);
        string scope = (cntxt != null) ? cntxt.get_full_name() : "";
        if (!inst_name.empty()) scope = {scope, ".", inst_name};
        return uvm_resource_db#(T)::read_by_name(scope, field_name, value);
    endfunction
endclass

task automatic run_test(string test_name = "");
    uvm_phase ph = new("run");
    uvm_component top = uvm_component::m_top_env;
    if (top != null) begin
        top.build_phase(ph);
        top.connect_phase(ph);
        top.end_of_elaboration_phase(ph);
        top.run_phase(ph);
        top.report_phase(ph);
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
    if (name == "process") {
        static std::shared_ptr<ClassNode> proc_cls = []() {
            auto cls = std::make_shared<ClassNode>("process");
            auto self_fn = std::make_unique<FunctionNode>();
            self_fn->name = "self";
            self_fn->is_static = true;
            self_fn->return_width = 64;
            cls->add_function(std::move(self_fn));
            auto status_fn = std::make_unique<FunctionNode>();
            status_fn->name = "status";
            status_fn->return_width = 32;
            cls->add_function(std::move(status_fn));
            auto kill_fn = std::make_unique<FunctionNode>();
            kill_fn->name = "kill";
            cls->add_function(std::move(kill_fn));
            auto await_fn = std::make_unique<FunctionNode>();
            await_fn->name = "await";
            cls->add_function(std::move(await_fn));
            auto suspend_fn = std::make_unique<FunctionNode>();
            suspend_fn->name = "suspend";
            cls->add_function(std::move(suspend_fn));
            auto resume_fn = std::make_unique<FunctionNode>();
            resume_fn->name = "resume";
            cls->add_function(std::move(resume_fn));
            auto get_rs_fn = std::make_unique<FunctionNode>();
            get_rs_fn->name = "get_randstate";
            get_rs_fn->return_type_name = "string";
            get_rs_fn->return_width = 64;
            cls->add_function(std::move(get_rs_fn));
            auto set_rs_fn = std::make_unique<FunctionNode>();
            set_rs_fn->name = "set_randstate";
            cls->add_function(std::move(set_rs_fn));
            return cls;
        }();
        return proc_cls.get();
    }
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
    auto col = name.find("::");
    if (col != std::string::npos) {
        std::string pkg_name = name.substr(0, col);
        std::string cls_name = name.substr(col + 2);
        auto pit = global_packages.find(pkg_name);
        if (pit != global_packages.end()) {
            auto cit = pit->second->class_map.find(cls_name);
            if (cit != pit->second->class_map.end()) return cit->second;
        }
    }
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

bool Parser::is_forward_interface_class(const std::string& name) const {
    auto colon_pos = name.find("::");
    if (colon_pos != std::string::npos) {
        std::string pkg = name.substr(0, colon_pos);
        std::string cls = name.substr(colon_pos + 2);
        auto pit = global_packages.find(pkg);
        if (pit != global_packages.end()) {
            if (pit->second->forward_interface_classes.count(cls)) return true;
        }
        return false;
    }

    if (current_parsing_package) {
        if (current_parsing_package->forward_interface_classes.count(name)) return true;
    }
    if (current_parsing_module) {
        if (current_parsing_module->forward_interface_classes.count(name)) return true;
        for (const auto& w : current_parsing_module->wildcard_imports) {
            auto pit = global_packages.find(w);
            if (pit != global_packages.end()) {
                if (pit->second->forward_interface_classes.count(name)) return true;
            }
        }
    }
    if (global_forward_interface_classes.count(name)) return true;
    for (const auto& w : global_wildcard_imports) {
        auto pit = global_packages.find(w);
        if (pit != global_packages.end()) {
            if (pit->second->forward_interface_classes.count(name)) return true;
        }
    }
    for (const auto& p : global_packages) {
        if (p.second->forward_interface_classes.count(name)) return true;
    }
    return false;
}

bool Parser::is_known_class(const std::string& name) const {
    if (name == "process") return true;
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
        } else if (current.type == TokenType::TOKEN_PARAMETER || current.type == TokenType::TOKEN_LOCALPARAM) {
            bool is_local = (current.type == TokenType::TOKEN_LOCALPARAM);
            advance();
            if (current.type == TokenType::TOKEN_TYPE || current.text == "type") {
                advance();
                while (true) {
                    std::string pname = current.text;
                    expect(TokenType::TOKEN_IDENTIFIER, "Expected parameter identifier");
                    expect(TokenType::TOKEN_EQUAL, "Expected '=' in parameter assignment");
                    if (current.type == TokenType::TOKEN_STRUCT || current.type == TokenType::TOKEN_UNION) {
                        auto st = parse_struct_or_union_body();
                        struct_typedef_map[pname] = st;
                        if (st->is_packed && st->total_width > 0) {
                            VariableDimMetadata svdm;
                            svdm.name = pname;
                            svdm.dimensions.push_back(ArrayDimInfo{(int64_t)st->total_width - 1, 0, false});
                            svdm.is_vector_or_array = true;
                            global_type_dimensions[pname] = svdm;
                        }
                        if (match(TokenType::TOKEN_COMMA)) continue;
                        break;
                    }
                    std::string def_type = current.text;
                    advance();
                    while (current.type == TokenType::TOKEN_DOT || current.type == TokenType::TOKEN_COLON_COLON || current.type == TokenType::TOKEN_HASH || current.type == TokenType::TOKEN_LBRACKET) {
                        if (match(TokenType::TOKEN_LBRACKET)) {
                            def_type += "[";
                            while (current.type != TokenType::TOKEN_RBRACKET && current.type != TokenType::TOKEN_EOF) {
                                def_type += current.text;
                                advance();
                            }
                            expect(TokenType::TOKEN_RBRACKET, "Expected ']' in type parameter default");
                            def_type += "]";
                        } else {
                            def_type += current.text;
                            advance();
                        }
                    }
                    simple_typedef_map[pname] = SimpleTypedef(pname, def_type, 32, false);
                    global_simple_typedefs[pname] = 32;
                    if (match(TokenType::TOKEN_COMMA)) continue;
                    break;
                }
                expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after parameter declaration");
                continue;
            }
            bool has_type = false;
            std::string ptype = "";
            while (current.type == TokenType::TOKEN_LOGIC || current.type == TokenType::TOKEN_REG || 
                   current.type == TokenType::TOKEN_WIRE || current.type == TokenType::TOKEN_INTEGER ||
                   current.type == TokenType::TOKEN_SIGNED || current.type == TokenType::TOKEN_UNSIGNED || 
                   current.type == TokenType::TOKEN_BIT ||
                   (!has_type && is_type_identifier(current.text)) ||
                   (!has_type && current.type == TokenType::TOKEN_IDENTIFIER && 
                    (current.text == "int" || current.text == "byte" || current.text == "shortint" || 
                     current.text == "longint" || current.text == "unsigned" || current.text == "signed" ||
                     current.text == "string" || current.text == "real" || current.text == "chandle" ||
                     peek_token().type == TokenType::TOKEN_COLON_COLON ||
                     peek_token().type == TokenType::TOKEN_IDENTIFIER ||
                     peek_token().type == TokenType::TOKEN_UNSIGNED ||
                     peek_token().type == TokenType::TOKEN_SIGNED))) {
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
                global_parameters.push_back(std::move(pdef));

                if (match(TokenType::TOKEN_COMMA)) continue;
                break;
            }
            expect(TokenType::TOKEN_SEMICOLON, "Expected ';' after parameter declaration");
            continue;
        } else if (current.type == TokenType::TOKEN_TYPEDEF) {
            auto td = parse_typedef();
            if (td) {
                if (td->struct_type) {
                    global_struct_typedefs[td->type_name] = td->struct_type;
                }
                if (td->enum_type) {
                    global_enum_typedefs[td->type_name] = td->enum_type;
                }
                if (td->width > 0) {
                    global_simple_typedefs[td->type_name] = td->width;
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
        } else if (current.type == TokenType::TOKEN_FUNCTION) {
            while (current.type != TokenType::TOKEN_ENDFUNCTION && current.type != TokenType::TOKEN_EOF) {
                advance();
            }
            if (current.type == TokenType::TOKEN_ENDFUNCTION) {
                advance();
                if (match(TokenType::TOKEN_COLON)) {
                    if (current.type == TokenType::TOKEN_IDENTIFIER) advance();
                }
            }
        } else if (current.type == TokenType::TOKEN_TASK) {
            while (current.type != TokenType::TOKEN_ENDTASK && current.type != TokenType::TOKEN_EOF) {
                advance();
            }
            if (current.type == TokenType::TOKEN_ENDTASK) {
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
        } else if (current.type == TokenType::TOKEN_CONSTRAINT) {
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
        } else {
            advance();
        }
    }
}
