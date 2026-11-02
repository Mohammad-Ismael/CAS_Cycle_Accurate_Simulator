`timescale 1ns/1ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.44.0.0
// Standard Target: IEEE Std 1800-2023 §6.24.1 (Static Cast Operators)
// Design Module: Arithmetic Normalizer and Enum Coercion Pipeline
// ============================================================================

package v3_44_0_0_pkg;
    typedef enum logic [1:0] {
        OP_IDLE = 2'b00,
        OP_ADD  = 2'b01,
        OP_SUB  = 2'b10,
        OP_MULT = 2'b11
    } opcode_e;

    typedef struct packed {
        logic [7:0] high_byte;
        logic [7:0] low_byte;
    } word_st;

    typedef logic [1:0] op_raw_t;
endpackage

module v3_44_0_0_normalizer (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  raw_val,
    input  logic [1:0]  raw_op,
    output logic [15:0] sized_out,
    output logic [15:0] signed_out,
    output logic [1:0]  enum_out
);
    import v3_44_0_0_pkg::*;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            sized_out  <= 16'h0000;
            signed_out <= 16'h0000;
            enum_out   <= 2'b00;
        end else begin
            // 1. Size cast: explicit zero-extension to 16 bits
            sized_out  <= 16'(raw_val * 8'd2);

            // 2. Sign cast: sign-extend 8-bit signed value into 16-bit register
            signed_out <= 16'(signed'(raw_val));

            // 3. Enum static cast and typedef type cast (§6.24.1)
            enum_out   <= op_raw_t'(opcode_e'(raw_op));
        end
    end

endmodule