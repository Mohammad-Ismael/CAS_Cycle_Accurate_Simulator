`timescale 1ns/1ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.39.0.0
// Standard Target: IEEE Std 1800-2023 §14.13 & §14.16 (Clocking Skew & #1step)
// Design Module: Synchronous Pipeline Register with Registered Feedback
// ============================================================================

module v3_39_0_0_sync_pipe (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [15:0] d_in,
    input  logic        valid_in,
    output logic [15:0] d_out,
    output logic        valid_out
);

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            d_out     <= 16'h0000;
            valid_out <= 1'b0;
        end else begin
            d_out     <= d_in;
            valid_out <= valid_in;
        end
    end

endmodule