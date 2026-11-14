`timescale 1ns/1ps

//==============================================================================
// Module: v3_85_0_0_wide_vector_logic_ops
// Standard: IEEE 1800-2023 SystemVerilog
// Description:
//   Verification of multi-word wide vector logic operations (>64-bit: 142-bit & 320-bit):
//   - Bitwise operations: OR, AND, XOR, NOT
//   - Unary reductions: |v, &v, ^v
//   - Ternary conditional: sel ? a : b
//   - 2D packed array slice indexing & procedural compound OR assignment
//==============================================================================

module v3_85_0_0_wide_vector_logic_ops (
    input  logic         clk,
    input  logic         rst_n,
    // 142-bit vector inputs
    input  logic [141:0] a142,
    input  logic [141:0] b142,
    input  logic         sel142,
    output logic [141:0] or142,
    output logic [141:0] and142,
    output logic [141:0] xor142,
    output logic [141:0] not142,
    output logic [141:0] mux142,
    output logic         red_or142,
    output logic         red_and142,
    output logic         red_xor142,

    // 320-bit vector inputs (5 x 64-bit words)
    input  logic [319:0] a320,
    input  logic [319:0] b320,
    input  logic         sel320,
    output logic [319:0] or320,
    output logic [319:0] and320,
    output logic [319:0] xor320,
    output logic [319:0] not320,
    output logic [319:0] mux320,
    output logic         red_or320,
    output logic         red_and320,
    output logic         red_xor320,

    // 2D packed array compound OR bypass structure: sel_data[k] |= mask[k][l] ? hold[l] : '0;
    input  logic [1:0][1:0]   mask,
    input  logic [1:0][141:0] hold,
    output logic [1:0][141:0] sel_data
);

    // Continuous wide bitwise assignments
    assign or142   = a142 | b142;
    assign and142  = a142 & b142;
    assign xor142  = a142 ^ b142;
    assign not142  = ~a142;
    assign mux142  = sel142 ? a142 : b142;
    assign red_or142  = |a142;
    assign red_and142 = &a142;
    assign red_xor142 = ^a142;

    assign or320   = a320 | b320;
    assign and320  = a320 & b320;
    assign xor320  = a320 ^ b320;
    assign not320  = ~a320;
    assign mux320  = sel320 ? a320 : b320;
    assign red_or320  = |a320;
    assign red_and320 = &a320;
    assign red_xor320 = ^a320;

    // Procedural compound OR assignment on 2D packed array slices
    always_comb begin
        sel_data = '0;
        for (int k = 0; k < 2; k++) begin
            for (int l = 0; l < 2; l++) begin
                sel_data[k] |= mask[k][l] ? hold[l] : 142'h0;
            end
        end
    end

endmodule
