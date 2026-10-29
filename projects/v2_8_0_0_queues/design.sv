// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v2.8.0.0
// Standard Target: IEEE Std 1800-2023 §7.10 (Queues & Standard Methods)
// Module Name    : v2_8_0_0_queue_core
// ============================================================================

module v2_8_0_0_queue_core (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [7:0] d_in,
    output logic [7:0] d_out
);
    assign d_out = d_in;
endmodule