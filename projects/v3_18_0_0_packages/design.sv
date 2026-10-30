// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.18.0.0
// Standard Target: IEEE Std 1800-2023 §26 (SystemVerilog Packages)
// Package & Module Definition
// ============================================================================

// ----------------------------------------------------------------------------
// Package Declaration (§26.2)
// ----------------------------------------------------------------------------
package bus_proto_pkg;
    parameter int DEFAULT_BURST = 4;
    parameter int ADDR_MASK     = 32'h0000FFFF;

    typedef enum logic [1:0] {
        CMD_READ  = 2'b00,
        CMD_WRITE = 2'b01,
        CMD_RESET = 2'b10,
        CMD_IDLE  = 2'b11
    } bus_cmd_e;

    typedef struct packed {
        bus_cmd_e    cmd;
        logic [13:0] reserved;
        logic [15:0] addr;
    } bus_hdr_s;

    function automatic logic [31:0] compute_tag(input bus_cmd_e cmd, input logic [15:0] addr);
        return {14'h0000, cmd, addr};
    endfunction
endpackage

// ----------------------------------------------------------------------------
// Design Core using Wildcard Package Import (§26.3)
// ----------------------------------------------------------------------------
module v3_18_0_0_package_core (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [1:0]  cmd_in,
    input  logic [15:0] addr_in,
    output logic [31:0] tag_out,
    output logic        valid_out
);
    import bus_proto_pkg::*; // Wildcard import

    always_comb begin
        if (!rst_n) begin
            tag_out   = 32'h00000000;
            valid_out = 1'b0;
        end else begin
            bus_cmd_e cmd_val;
            cmd_val   = bus_cmd_e'(cmd_in);
            tag_out   = compute_tag(cmd_val, addr_in);
            valid_out = (cmd_val != CMD_IDLE);
        end
    end
endmodule