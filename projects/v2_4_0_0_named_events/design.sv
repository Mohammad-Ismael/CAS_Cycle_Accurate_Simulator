// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v2.4.0.0
// Standard Target: IEEE Std 1800-2023 §15 (Clocking & Inter-process Named Events)
// Module Name    : v2_4_0_0_event_core
// ============================================================================

module v2_4_0_0_event_core (
    input  logic       clk,
    input  logic [7:0] d_in,
    output logic [7:0] d_out
);
    assign d_out = d_in;
endmodule