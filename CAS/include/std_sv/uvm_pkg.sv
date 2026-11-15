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
