// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v1.9.0.0
// Standard Target: IEEE Std 1800-2023 §10.6.2 (Event-Based Intra-Assignments)
// Module Name    : v1_9_0_0_intra_event_core
// ============================================================================

module v1_9_0_0_intra_event_core (
    input  logic       clk,
    input  logic [7:0] d_in,
    output logic [7:0] q_nba,
    output logic [7:0] q_blk
);
    // Procedural test process with intra-assignment event controls
    always begin
        // NBA with intra-assignment event control:
        // Evaluates (d_in + 8'h01) immediately, then defers commitment until posedge clk
        q_nba <= @(posedge clk) (d_in + 8'h01);

        // Blocking with intra-assignment event control:
        // Evaluates (d_in + 8'h10) immediately, suspends this thread until posedge clk,
        // then assigns q_blk immediately in that active slot
        q_blk = @(posedge clk) (d_in + 8'h10);
    end
endmodule