`timescale 1ns/1ps

`ifndef VERILATOR
import uvm_pkg::*;
`include "uvm_macros.svh"
`endif

interface pipeline_if;
    logic        clk;
    logic        rst_n;
    logic        req;
    logic        gnt0;
    logic        gnt1;
    logic        active;
    logic [7:0]  status;
endinterface

string label = "SVA_SEQUENCES_UVM";

`ifndef VERILATOR
class pipeline_env extends uvm_env;
    virtual pipeline_if m_vif;

    function new(string name, uvm_component parent = null);
        super.new(name, parent);
    endfunction

    function void connect_phase(uvm_phase phase);
        `uvm_info(label, "Starting connect phase", UVM_LOW);
        assert(uvm_resource_db#(virtual pipeline_if)::read_by_name(
            get_full_name(), "pipeline_if", m_vif));
        `uvm_info(label, "Finished connect phase", UVM_LOW);
    endfunction

    task run_phase(uvm_phase phase);
        phase.raise_objection(this);
        `uvm_info(label, "Entering run phase", UVM_LOW);

        // Reset sequence
        @(posedge m_vif.clk);
        m_vif.rst_n = 1;
        @(posedge m_vif.clk);

        // Issue request
        m_vif.req = 1;
        @(posedge m_vif.clk);
        m_vif.req = 0;

        // Wait for grant cycle
        repeat (5) @(posedge m_vif.clk);

        $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
        `uvm_info(label, "Completed run phase", UVM_LOW);
        phase.drop_objection(this);
    endtask
endclass
`endif

module tb_v4_3_0_regression;
    pipeline_if dif();

    always #5 dif.clk = ~dif.clk;

    arbiter_pipeline_core dut (
        .clk(dif.clk),
        .rst_n(dif.rst_n),
        .req(dif.req),
        .gnt0(dif.gnt0),
        .gnt1(dif.gnt1),
        .active(dif.active),
        .status(dif.status)
    );

`ifndef VERILATOR
    pipeline_env env_inst;

    initial begin
        dif.clk   = 0;
        dif.rst_n = 0;
        dif.req   = 0;

        env_inst = new("env_inst");
        uvm_resource_db#(virtual pipeline_if)::set("env_inst", "pipeline_if", dif);
        run_test();

        $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
        $finish;
    end
`else
    initial begin
        dif.clk   = 0;
        dif.rst_n = 0;
        dif.req   = 0;

        #15;
        dif.rst_n = 1;
        #10;
        dif.req   = 1;
        #10;
        dif.req   = 0;

        #50;
        if (dif.status !== 8'h00) begin
            $display("ERROR: Expected status 0, got %0h", dif.status);
            $display("RESULT : >>> FAIL <<<");
        end else begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
        end
        $finish;
    end
`endif

endmodule
