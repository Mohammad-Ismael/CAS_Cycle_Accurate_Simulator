`timescale 1ns/1ps
/* verilator lint_off MULTIDRIVEN */

//==============================================================================
// Module: v3_87_0_0_assign_in_expression
// Standard: IEEE 1800-2023 SystemVerilog (§11.3.6 assign-in-expression, §6.8 2-state defaults)
// Description:
//   Verification module exercising:
//   1. Uninitialized 2-state variables defaulting to 0 (§6.8 Table 6-7)
//   2. Chained parenthesized assignments a = (b = (c = 42)) (§11.3.6)
//   3. Compound assignments inside expressions (+=, -=, <<=, >>=)
//   4. Prefix and postfix increment/decrement (++k, k++, --k, k--) inside expressions
//   5. Standalone prefix and postfix statement execution (++cnt, cnt++, --cnt, cnt--)
//==============================================================================

module v3_87_0_0_assign_in_expression (
    input  logic        clk,
    input  logic        rst_n,

    // 2-state default zero check
    output logic [31:0] def_int_val,
    output logic [7:0]  def_bit_val,
    output logic [7:0]  def_byte_val,
    output logic [15:0] def_shortint_val,
    output logic [63:0] def_longint_val,

    // Chained assignments
    output logic [31:0] chain_a,
    output logic [31:0] chain_b,
    output logic [31:0] chain_c,

    // Compound assignments inside expressions
    output logic [31:0] comp_r1,
    output logic [31:0] comp_r2,
    output logic [31:0] comp_r3,
    output logic [31:0] comp_x,
    output logic [31:0] comp_y,
    output logic [31:0] comp_z,
    output logic [31:0] shift_s1,
    output logic [31:0] shift_s2,
    output logic [31:0] shift_s,

    // Prefix / Postfix inc/dec in expressions
    output logic [31:0] inc_p1,
    output logic [31:0] inc_p2,
    output logic [31:0] inc_p3,
    output logic [31:0] inc_p4,
    output logic [31:0] inc_k,

    // Standalone inc/dec
    output logic [31:0] stmt_cnt
);

    // Module-level uninitialized 2-state nets
    int      uninit_int;
    bit [7:0] uninit_bit;
    byte     uninit_byte;
    shortint uninit_shortint;
    longint  uninit_longint;

    always_comb begin
        int a, b, c;
        int x, y, z;
        int r1, r2, r3;
        int s, s1, s2;
        int k, p1, p2, p3, p4;
        int cnt;

        // 1. Assign uninitialized 2-state defaults to outputs
        def_int_val      = uninit_int;
        def_bit_val      = uninit_bit;
        def_byte_val     = uninit_byte;
        def_shortint_val = uninit_shortint;
        def_longint_val  = uninit_longint;

        // 2. Chained parenthesized assignments
        a = (b = (c = 42));
        chain_a = a;
        chain_b = b;
        chain_c = c;

        // 3. Compound assignments inside expressions
        x = 10;
        r1 = (x += 5);  // x becomes 15, r1 = 15
        r2 = (x -= 3);  // x becomes 12, r2 = 12
        y = 10;
        z = 4;
        r3 = ((y += (z += 2) + 1)); // z becomes 6; y += 7 -> 17; r3 = 17
        comp_r1 = r1;
        comp_r2 = r2;
        comp_r3 = r3;
        comp_x  = x;
        comp_y  = y;
        comp_z  = z;

        // Shift compound assignments inside expressions
        s = 4;
        s1 = (s <<= 2); // s becomes 16, s1 = 16
        s2 = (s >>= 1); // s becomes 8,  s2 = 8
        shift_s1 = s1;
        shift_s2 = s2;
        shift_s  = s;

        // 4. Prefix and postfix inc/dec in expressions
        k = 10;
        p1 = (++k); // k becomes 11, p1 = 11
        p2 = (k++); // p2 = 11, k becomes 12
        p3 = (--k); // k becomes 11, p3 = 11
        p4 = (k--); // p4 = 11, k becomes 10
        inc_p1 = p1;
        inc_p2 = p2;
        inc_p3 = p3;
        inc_p4 = p4;
        inc_k  = k;

        // 5. Standalone inc/dec statements
        cnt = 5;
        ++cnt; // 6
        cnt++; // 7
        --cnt; // 6
        cnt--; // 5
        stmt_cnt = cnt;
    end

endmodule
