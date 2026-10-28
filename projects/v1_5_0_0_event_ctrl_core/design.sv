// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v1.5.0.0
// Standard Target: IEEE Std 1800-2023 §9.4 (Procedural Timing & Event Control)
// Module Name    : v1_5_0_0_event_ctrl_core
// ============================================================================

module v1_5_0_0_event_ctrl_core (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [7:0] d_in,
    output logic [7:0] q_sync,
    output logic [7:0] q_piped
);

    // Standard sequential logic for reference
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            q_sync  <= 8'h00;
            q_piped <= 8'h00;
        end else begin
            q_sync  <= d_in;
            q_piped <= q_sync;
        end
    end

endmodule