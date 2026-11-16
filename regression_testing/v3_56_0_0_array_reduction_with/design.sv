`timescale 1ns/1ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.56.0.0
// Standard Target: IEEE Std 1800-2023 §7.12 (Array Reduction with 'with' Clause)
// Design Module: Hardware Packet Checksum & Scoreboard Metric Calculator
// ============================================================================

package v3_56_0_0_pkg;
    typedef int data_dyn_t[];
endpackage

module v3_56_0_0_metric_calculator (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [31:0] input_sample,
    output logic [31:0] total_sum_out,
    output logic [31:0] filtered_sum_out,
    output logic [31:0] parity_xor_out,
    output logic        calc_valid
);
    import v3_56_0_0_pkg::*;

    data_dyn_t history_buffer;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            history_buffer   = new[0];
            total_sum_out    <= 32'd0;
            filtered_sum_out <= 32'd0;
            parity_xor_out   <= 32'd0;
            calc_valid       <= 1'b0;
        end else begin
            // Append incoming sample
            history_buffer = {history_buffer, int'(input_sample)};

            // Compute array reduction metrics per §7.12:
            // 1. Raw sum of elements
            total_sum_out <= history_buffer.sum();

            // 2. Conditional reduction with 'with' clause: sum only samples > 20
            filtered_sum_out <= history_buffer.sum() with ( item > 20 ? item : 0 );

            // 3. Bitwise XOR parity reduction across all buffer entries
            parity_xor_out <= history_buffer.xor();

            calc_valid <= (history_buffer.size() >= 4);
        end
    end

endmodule