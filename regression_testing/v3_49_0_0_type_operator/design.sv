`timescale 1ns/1ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.49.0.0
// Standard Target: IEEE Std 1800-2023 §6.23 (Type Operator 'type(...)')
// Design Module: Dynamic Type Validator & Shadow Register Pipeline
// ============================================================================

package v3_49_0_0_pkg;

    typedef enum logic [1:0] {
        MODE_A = 2'b00,
        MODE_B = 2'b01
    } mode_e;

    typedef struct packed {
        logic [7:0] high_byte;
        logic [7:0] low_byte;
    } pair_s;

endpackage

module v3_49_0_0_type_checker (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  raw_in,
    output logic        type_match_out,
    output logic [7:0]  shadow_out
);
    import v3_49_0_0_pkg::*;

    mode_e current_mode;
    mode_e alt_mode;
    pair_s data_pair;

    // Type operator used in variable declaration (§6.23)
    type(raw_in) shadow_reg;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            type_match_out <= 1'b0;
            shadow_out     <= 8'h00;
            shadow_reg     <= 8'h00;
        end else begin
            shadow_reg <= raw_in;
            shadow_out <= shadow_reg;

            // Type comparison expressions (§6.23)
            // Identical enums match; enum and struct do not match
            if (type(current_mode) == type(alt_mode) &&
                type(current_mode) != type(data_pair)) begin
                type_match_out <= 1'b1;
            end else begin
                type_match_out <= 1'b0;
            end
        end
    end

endmodule