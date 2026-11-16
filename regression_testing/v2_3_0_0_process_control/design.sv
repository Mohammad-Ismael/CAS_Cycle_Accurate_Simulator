// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v2.3.0.0
// Standard Target: IEEE Std 1800-2023 §9.6.2 & §9.6.3 (disable fork, wait fork)
// Module Name    : v2_3_0_0_proc_ctrl_core
// ============================================================================

module v2_3_0_0_proc_ctrl_core (
    input  logic       clk,
    input  logic [7:0] d_in,
    output logic [7:0] d_out
);
    assign d_out = d_in;
endmodule