`timescale 1ns/1ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.53.0.0
// Standard Target: IEEE Std 1800-2023 §20.7 ($dimensions & $unpacked_dimensions)
// Design Module: Multidimensional Buffer Descriptor & Dimension Introspector
// ============================================================================

package v3_53_0_0_pkg;

    // Type 1: 1D packed vector (1 total, 0 unpacked)
    typedef logic [15:0] word16_t;

    // Type 2: 2D packed matrix (2 total, 0 unpacked)
    typedef logic [3:0][7:0] byte_mat_t;

    // Type 3: Multidimensional unpacked buffer of packed vectors
    // 2 unpacked dimensions [4][8] + 1 packed dimension [15:0] = 3 total, 2 unpacked
    typedef word16_t frame_buf_t [4][8];

endpackage

module v3_53_0_0_dim_checker (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  ctrl_in,
    output logic [3:0]  total_dims_out,
    output logic [3:0]  unpacked_dims_out,
    output logic        dim_match_valid
);
    import v3_53_0_0_pkg::*;

    word16_t    single_word;
    byte_mat_t  matrix_reg;
    frame_buf_t buffer_mem;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            total_dims_out    <= 4'd0;
            unpacked_dims_out <= 4'd0;
            dim_match_valid   <= 1'b0;
        end else begin
            // 1. Runtime reporting of buffer_mem dimensions (3 total, 2 unpacked)
            total_dims_out    <= 4'($dimensions(buffer_mem));
            unpacked_dims_out <= 4'($unpacked_dimensions(buffer_mem));

            // 2. Validate dimensional query integrity across all registered array types
            if ($dimensions(single_word)          == 1 &&
                $unpacked_dimensions(single_word) == 0 &&
                $dimensions(matrix_reg)           == 2 &&
                $unpacked_dimensions(matrix_reg)  == 0 &&
                $dimensions(buffer_mem)           == 3 &&
                $unpacked_dimensions(buffer_mem)  == 2 &&
                $dimensions(ctrl_in)              == 1 &&
                $unpacked_dimensions(ctrl_in)     == 0) begin
                dim_match_valid <= 1'b1;
            end else begin
                dim_match_valid <= 1'b0;
            end
        end
    end

endmodule