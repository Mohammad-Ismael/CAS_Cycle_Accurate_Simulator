// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.3.0.0
// Standard Target: IEEE Std 1800-2023 §7.8 (Associative Arrays & Methods)
// Module Name    : v3_3_0_0_assoc_core
// ============================================================================

module v3_3_0_0_assoc_core (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [15:0] addr_in,
    input  logic [31:0] data_in,
    input  logic        we,
    output logic [31:0] data_out
);
    assign data_out = (we) ? data_in : 32'h00000000;
endmodule