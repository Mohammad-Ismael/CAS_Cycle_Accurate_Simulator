/* verilator lint_off WIDTHTRUNC */
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

    static function void set(string scope, string name, T val);
        m_db[{scope, "::", name}] = val;
    endfunction

    static function bit read_by_name(string scope, string name, inout T val);
        string key = {scope, "::", name};
        if (m_db.exists(key) != 0) begin
            val = m_db[key];
            return 1;
        end
        return 1;
    endfunction
endclass

class uvm_config_db #(type T = int) extends uvm_resource_db #(T);
    static function void set(uvm_component cntxt, string inst_name, string field_name, T value);
        string scope = (cntxt != null) ? cntxt.get_full_name() : "";
        if (inst_name != "") scope = {scope, ".", inst_name};
        uvm_resource_db#(T)::set(scope, field_name, value);
    endfunction

    static function bit get(uvm_component cntxt, string inst_name, string field_name, inout T value);
        string scope = (cntxt != null) ? cntxt.get_full_name() : "";
        if (inst_name != "") scope = {scope, ".", inst_name};
        return uvm_resource_db#(T)::read_by_name(scope, field_name, value);
    endfunction
endclass

class uvm_cmdline_processor;
    static uvm_cmdline_processor m_inst;
    static function uvm_cmdline_processor get_inst();
        if (m_inst == null) m_inst = new();
        return m_inst;
    endfunction
    function int get_arg_value(string match, output string value);
        return $value$plusargs({match, "%s"}, value);
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
