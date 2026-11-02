`timescale 1ns/1ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.54.0.0
// Standard Target: IEEE Std 1800-2023 §20.7 (Array Bound Queries: $left, $right, $low, $high, $increment)
// Design Module: Generic Memory Descriptor & Dimension Bounds Scanner
// ============================================================================

package v3_54_0_0_pkg;

    // Descending packed vector: [7:0] -> left=7, right=0, low=0, high=7, inc=1 (since 7 >= 0)
    typedef logic [7:0] byte_desc_t;

    // Ascending packed vector: [0:15] -> left=0, right=15, low=0, high=15, inc=-1 (since 0 < 15)
    typedef logic [0:15] word_asc_t;

    // Unpacked buffer with ascending unpacked dim [0:3] of descending packed vectors [7:0]
    // Dim 1 (unpacked [0:3]): left=0, right=3, low=0, high=3, inc=-1 (0 < 3)
    // Dim 2 (packed   [7:0]): left=7, right=0, low=0, high=7, inc=1  (7 >= 0)
    typedef byte_desc_t packet_buf_t [0:3];

endpackage

module v3_54_0_0_bounds_scanner (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  ctrl_in,
    output logic [31:0] dim1_left,
    output logic [31:0] dim1_right,
    output logic [31:0] dim1_low,
    output logic [31:0] dim1_high,
    output logic signed [31:0] dim1_inc,
    output logic [31:0] dim2_left,
    output logic [31:0] dim2_right,
    output logic [31:0] dim2_low,
    output logic [31:0] dim2_high,
    output logic signed [31:0] dim2_inc,
    output logic        bounds_valid
);
    import v3_54_0_0_pkg::*;

    packet_buf_t buffer_reg;
    word_asc_t   asc_reg;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            dim1_left    <= '0;
            dim1_right   <= '0;
            dim1_low     <= '0;
            dim1_high    <= '0;
            dim1_inc     <= '0;
            dim2_left    <= '0;
            dim2_right   <= '0;
            dim2_low     <= '0;
            dim2_high    <= '0;
            dim2_inc     <= '0;
            bounds_valid <= 1'b0;
        end else begin
            // Introspect Dim 1 (unpacked [0:3]) of buffer_reg
            dim1_left  <= $left(buffer_reg, 1);
            dim1_right <= $right(buffer_reg, 1);
            dim1_low   <= $low(buffer_reg, 1);
            dim1_high  <= $high(buffer_reg, 1);
            dim1_inc   <= $increment(buffer_reg, 1);

            // Introspect Dim 2 (packed [7:0]) of buffer_reg
            dim2_left  <= $left(buffer_reg, 2);
            dim2_right <= $right(buffer_reg, 2);
            dim2_low   <= $low(buffer_reg, 2);
            dim2_high  <= $high(buffer_reg, 2);
            dim2_inc   <= $increment(buffer_reg, 2);

            // Cross-validate ascending packed vector: [0:15]
            // Per IEEE 1800 §20.7: $left < $right (0 < 15) -> $increment is -1
            if ($left(asc_reg)      == 0  &&
                $right(asc_reg)     == 15 &&
                $low(asc_reg)       == 0  &&
                $high(asc_reg)      == 15 &&
                $increment(asc_reg) == -1) begin
                bounds_valid <= 1'b1;
            end else begin
                bounds_valid <= 1'b0;
            end
        end
    end

endmodule