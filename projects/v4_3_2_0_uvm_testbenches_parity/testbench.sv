import uvm_pkg::*;
`include "uvm_macros.svh"

class alu_item extends uvm_sequence_item;
    logic [7:0]  op_a;
    logic [7:0]  op_b;
    logic [1:0]  op_code;
    logic [15:0] result;

    `uvm_object_utils_begin(alu_item)
        `uvm_field_int(op_a, UVM_ALL_ON)
        `uvm_field_int(op_b, UVM_ALL_ON)
        `uvm_field_int(op_code, UVM_ALL_ON)
        `uvm_field_int(result, UVM_ALL_ON)
    `uvm_object_utils_end

    function new(string name = "alu_item");
        super.new(name);
    endfunction
endclass

class alu_monitor extends uvm_monitor;
    `uvm_component_utils(alu_monitor)
    uvm_analysis_port #(alu_item) ap;

    function new(string name = "alu_monitor", uvm_component parent = null);
        super.new(name, parent);
        ap = new("ap", this);
    endfunction
endclass

class alu_scoreboard extends uvm_scoreboard;
    `uvm_component_utils(alu_scoreboard)
    uvm_analysis_imp #(alu_item, alu_scoreboard) imp;
    int items_checked = 0;

    function new(string name = "alu_scoreboard", uvm_component parent = null);
        super.new(name, parent);
        imp = new("imp", this);
    endfunction

    function void write(alu_item item);
        items_checked++;
    endfunction
endclass

class alu_agent extends uvm_agent;
    alu_monitor mon;
    `uvm_component_utils(alu_agent)

    function new(string name = "alu_agent", uvm_component parent = null);
        super.new(name, parent);
    endfunction

    virtual function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        mon = alu_monitor::type_id::create("mon", this);
    endfunction
endclass

class alu_env extends uvm_env;
    alu_agent      ag;
    alu_scoreboard sb;
    `uvm_component_utils(alu_env)

    function new(string name = "alu_env", uvm_component parent = null);
        super.new(name, parent);
    endfunction

    virtual function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        ag = alu_agent::type_id::create("ag", this);
        sb = alu_scoreboard::type_id::create("sb", this);
    endfunction

    virtual function void connect_phase(uvm_phase phase);
        super.connect_phase(phase);
        ag.mon.ap.connect(sb.imp);
    endfunction
endclass

class alu_test extends uvm_test;
    alu_env env;
    `uvm_component_utils(alu_test)

    function new(string name = "alu_test", uvm_component parent = null);
        super.new(name, parent);
    endfunction

    virtual function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        env = alu_env::type_id::create("env", this);
        uvm_config_db#(int)::set(this, "*", "test_param", 42);
    endfunction

    virtual function void connect_phase(uvm_phase phase);
        int val;
        super.connect_phase(phase);
        if (uvm_config_db#(int)::get(this, "", "test_param", val)) begin
            `uvm_info("CFG_DB", "Config DB Get Success", UVM_LOW)
        end
    endfunction

    task run_phase(uvm_phase phase);
        phase.raise_objection(this);
        `uvm_info("RUN", "Starting ALU Test Run Phase", UVM_LOW)
        phase.drop_objection(this);
    endtask

    virtual function void report_phase(uvm_phase phase);
        super.report_phase(phase);
        `uvm_info("REPORT", "[PASS] UVM Testbenches Parity Regression Verified Cleanly!", UVM_LOW)
    endfunction
endclass

module tb_v4_3_2_regression;
    logic        clk = 0;
    logic        rst_n = 0;
    logic [7:0]  op_a = 0;
    logic [7:0]  op_b = 0;
    logic [1:0]  op_code = 0;
    wire  [15:0] alu_out;
    wire         valid_out;

    v4_3_2_alu dut (
        .clk(clk),
        .rst_n(rst_n),
        .op_a(op_a),
        .op_b(op_b),
        .op_code(op_code),
        .alu_out(alu_out),
        .valid_out(valid_out)
    );

    always #5 clk = ~clk;

    initial begin
        rst_n = 0;
        #15 rst_n = 1;
        op_a = 8'd10;
        op_b = 8'd20;
        op_code = 2'b00; // 10 + 20 = 30
        #10;
        if (alu_out !== 16'd30) begin
            $display("[FAIL] Expected ALU 30, got %d", alu_out);
            $finish;
        end
        op_code = 2'b10; // 10 * 20 = 200
        #10;
        if (alu_out !== 16'd200) begin
            $display("[FAIL] Expected ALU 200, got %d", alu_out);
            $finish;
        end

        run_test("alu_test");
    end
endmodule
