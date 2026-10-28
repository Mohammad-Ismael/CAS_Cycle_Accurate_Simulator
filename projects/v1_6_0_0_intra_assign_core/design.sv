// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v1.6.0.0
// Standard Target: IEEE Std 1800-2023 §10.6.1 (Intra-Assignment Timing Controls)
// Module Name    : v1_6_0_0_intra_assign_core
// ============================================================================

module v1_6_0_0_intra_assign_core (
    input  logic [7:0] d_in,
    output logic [7:0] q_delayed_nba
);
    // Non-blocking assignment with intra-assignment transport delay
    // RHS sampled immediately, target updated after #10
    always_comb begin
        q_delayed_nba <= #10 d_in;
    end
endmodule