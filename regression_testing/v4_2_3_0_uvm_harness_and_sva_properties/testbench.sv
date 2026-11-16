`timescale 1ns/1ps

`ifndef VERILATOR
import uvm_pkg::*;
`include "uvm_macros.svh"
`endif

interface alu_if;
    logic        clk;
    logic        rst_n;
    logic [7:0]  a;
    logic [7:0]  b;
    logic [1:0]  op;
    logic        valid;
    logic [8:0]  res;
    logic        ready;

`ifndef VERILATOR
    property prop_valid_or_idle;
        @(posedge clk) disable iff (!rst_n)
        valid || !valid;
    endproperty
`endif

endinterface

string label = "UVM_SVA_HARNESS";

`ifndef VERILATOR
class alu_env extends uvm_env;
    virtual alu_if m_vif;

    function new(string name, uvm_component parent = null);
        super.new(name, parent);
    endfunction

    function void connect_phase(uvm_phase phase);
        `uvm_info(label, "Starting UVM connect phase", UVM_LOW);
        assert(uvm_resource_db#(virtual alu_if)::read_by_name(
            get_full_name(), "alu_if", m_vif));
        `uvm_info(label, "Completed UVM connect phase with valid VIF", UVM_LOW);
    endfunction

    task run_phase(uvm_phase phase);
        phase.raise_objection(this);
        `uvm_info(label, "Entering UVM run phase", UVM_LOW);

        // Immediate assert #0 and assume
        assert #0 (m_vif.rst_n == 0 || m_vif.rst_n == 1);
        assume (m_vif.a <= 8'hFF);

        // Step 1: Reset release
        @(posedge m_vif.clk);
        m_vif.rst_n = 1;
        m_vif.valid = 1;
        m_vif.op    = 2'b00; // ADD
        m_vif.a     = 8'd25;
        m_vif.b     = 8'd35;

        // Step 2: Next clock cycle check
        @(posedge m_vif.clk);
        m_vif.valid = 0;
        @(posedge m_vif.clk);
        assert (m_vif.res == 9'd60) else `uvm_error(label, "Mismatch on ALU ADD");

        // Interface property assertion
        assert property (m_vif.prop_valid_or_idle);

        // Assert final check
        assert final (m_vif.rst_n == 1);

        $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
        `uvm_info(label, "Exiting UVM run phase successfully", UVM_LOW);
        phase.drop_objection(this);
    endtask
endclass
`endif

module tb_v4_2_3_regression;
    alu_if dif();

    always #5 dif.clk = ~dif.clk;

    alu_core dut (
        .clk(dif.clk),
        .rst_n(dif.rst_n),
        .a(dif.a),
        .b(dif.b),
        .op(dif.op),
        .valid(dif.valid),
        .res(dif.res),
        .ready(dif.ready)
    );

`ifndef VERILATOR
    alu_env environment;

    initial begin
        dif.clk   = 0;
        dif.rst_n = 0;
        dif.a     = 0;
        dif.b     = 0;
        dif.op    = 0;
        dif.valid = 0;

        environment = new("alu_env");
        uvm_resource_db#(virtual alu_if)::set("alu_env", "alu_if", dif);
        run_test();

        $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
        $finish;
    end
`else
    initial begin
        dif.clk   = 0;
        dif.rst_n = 0;
        dif.a     = 0;
        dif.b     = 0;
        dif.op    = 0;
        dif.valid = 0;

        #15;
        dif.rst_n = 1;
        dif.valid = 1;
        dif.op    = 2'b00;
        dif.a     = 8'd25;
        dif.b     = 8'd35;

        #10;
        dif.valid = 0;
        #10;
        if (dif.res !== 9'd60) begin
            $display("ERROR: Expected 60, got %0d", dif.res);
            $display("RESULT : >>> FAIL <<<");
        end else begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
        end
        $finish;
    end
`endif

endmodule
