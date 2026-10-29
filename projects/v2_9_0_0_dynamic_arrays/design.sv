// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v2.9.0.0
// Standard Target: IEEE Std 1800-2023 §7.5 (Dynamic Arrays)
// Module Name    : v2_9_0_0_dyn_array_core
// ============================================================================

module v2_9_0_0_dyn_array_core (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [7:0] d_in,
    output logic [7:0] d_out
);
    assign d_out = d_in;
endmodule