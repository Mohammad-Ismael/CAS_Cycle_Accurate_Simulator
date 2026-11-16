`timescale 1ns/1ps
/* verilator lint_off MULTIDRIVEN */

//==============================================================================
// Module: v3_88_0_0_arith_shift_and_4state_eq
// Standard: IEEE 1800-2023 SystemVerilog (§11.4.10 arith shifts, §11.4.5 4-state equality)
// Description:
//   Verification module exercising:
//   1. Signed and unsigned arithmetic shifts (<<<, >>>) and assignments (<<<=, >>>=)
//      on 8-bit and 128-bit operands.
//   2. IEEE 1800 §11.4.5 Definite inequality in 4-state logical equality (==, !=)
//      when known 2-state bits differ alongside X or Z bits.
//==============================================================================

module v3_88_0_0_arith_shift_and_4state_eq (
    input  logic         clk,
    input  logic         rst_n,

    // 8-bit shifts
    output logic signed [7:0]  out_s8_shl,
    output logic signed [7:0]  out_s8_shr,
    output logic signed [7:0]  out_s8_assign_shl,
    output logic signed [7:0]  out_s8_assign_shr,
    output logic        [7:0]  out_u8_shl,
    output logic        [7:0]  out_u8_shr,
    output logic        [7:0]  out_u8_assign_shl,
    output logic        [7:0]  out_u8_assign_shr,

    // 128-bit shifts
    output logic signed [127:0] out_s128_shl,
    output logic signed [127:0] out_s128_shr,
    output logic signed [127:0] out_s128_assign_shl,
    output logic signed [127:0] out_s128_assign_shr,
    output logic        [127:0] out_u128_shl,
    output logic        [127:0] out_u128_shr,
    output logic        [127:0] out_u128_assign_shl,
    output logic        [127:0] out_u128_assign_shr,

    // 4-state equality outcomes
    output logic         out_eq_diff1,
    output logic         out_neq_diff1,
    output logic         out_case_eq1,
    output logic         out_case_neq1,

    output logic         out_eq_diff2,
    output logic         out_neq_diff2,
    output logic         out_case_eq2,
    output logic         out_case_neq2,

    output logic         out_eq_unk,
    output logic         out_neq_unk,
    output logic         out_case_eq_unk,
    output logic         out_case_neq_unk,

    output logic         out_eq_match,
    output logic         out_neq_match,
    output logic         out_case_eq_match,
    output logic         out_case_neq_match
);

    always_comb begin
        // 8-bit signed & unsigned
        logic signed [7:0] s8;
        logic signed [7:0] s8_as_l;
        logic signed [7:0] s8_as_r;
        logic        [7:0] u8;
        logic        [7:0] u8_as_l;
        logic        [7:0] u8_as_r;

        s8 = -8'sd120; // 8'b10001000
        out_s8_shl = (s8 <<< 3);
        out_s8_shr = (s8 >>> 3);
        s8_as_l = s8;
        s8_as_l <<<= 3;
        out_s8_assign_shl = s8_as_l;
        s8_as_r = s8;
        s8_as_r >>>= 3;
        out_s8_assign_shr = s8_as_r;

        u8 = 8'd8;
        out_u8_shl = (u8 <<< 3);
        out_u8_shr = (u8 >>> 3);
        u8_as_l = u8;
        u8_as_l <<<= 3;
        out_u8_assign_shl = u8_as_l;
        u8_as_r = u8;
        u8_as_r >>>= 3;
        out_u8_assign_shr = u8_as_r;

        // 128-bit signed & unsigned
        begin
            logic signed [127:0] s128;
            logic signed [127:0] s128_as_l;
            logic signed [127:0] s128_as_r;
            logic        [127:0] u128;
            logic        [127:0] u128_as_l;
            logic        [127:0] u128_as_r;

            s128 = -128'sd1024;
            out_s128_shl = (s128 <<< 2);
            out_s128_shr = (s128 >>> 4);
            s128_as_l = s128;
            s128_as_l <<<= 2;
            out_s128_assign_shl = s128_as_l;
            s128_as_r = s128;
            s128_as_r >>>= 4;
            out_s128_assign_shr = s128_as_r;

            u128 = 128'd1024;
            out_u128_shl = (u128 <<< 2);
            out_u128_shr = (u128 >>> 4);
            u128_as_l = u128;
            u128_as_l <<<= 2;
            out_u128_assign_shl = u128_as_l;
            u128_as_r = u128;
            u128_as_r >>>= 4;
            out_u128_assign_shr = u128_as_r;
        end

        // 4-state equality evaluation
        begin
            logic [7:0] a, b, c, d, p, q, m, n;
            a = 8'b1101x001;
            b = 8'b1101x000;
            out_eq_diff1   = (a == b);
            out_neq_diff1  = (a != b);
            out_case_eq1   = (a === b);
            out_case_neq1  = (a !== b);

            c = 8'b1101z001;
            d = 8'b1101z000;
            out_eq_diff2   = (c == d);
            out_neq_diff2  = (c != d);
            out_case_eq2   = (c === d);
            out_case_neq2  = (c !== d);

            p = 8'b1101x001;
            q = 8'b11010001;
            out_eq_unk     = (p == q);
            out_neq_unk    = (p != q);
            out_case_eq_unk  = (p === q);
            out_case_neq_unk = (p !== q);

            m = 8'b1101x001;
            n = 8'b1101x001;
            out_eq_match     = (m == n);
            out_neq_match    = (m != n);
            out_case_eq_match  = (m === n);
            out_case_neq_match = (m !== n);
        end
    end

endmodule
