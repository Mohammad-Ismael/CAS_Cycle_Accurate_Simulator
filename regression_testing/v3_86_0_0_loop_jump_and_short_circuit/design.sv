`timescale 1ns/1ps
/* verilator lint_off MULTIDRIVEN */

//==============================================================================
// Module: v3_86_0_0_loop_jump_and_short_circuit
// Standard: IEEE 1800-2023 SystemVerilog (§12.8 break/continue, §11.3.5 short-circuit)
// Description:
//   Verification module exercising:
//   1. for-loop break (exiting without step increment)
//   2. for-loop continue (skipping loop body, executing step increment)
//   3. while-loop break and continue
//   4. Logical short-circuiting of && and || without calling side-effect functions
//==============================================================================

module v3_86_0_0_loop_jump_and_short_circuit (
    input  logic        clk,
    input  logic        rst_n,
    output logic [31:0] for_break_idx,
    output logic [31:0] for_break_sum,
    output logic [31:0] for_cont_sum,
    output logic [31:0] while_break_idx,
    output logic [31:0] while_cont_sum,
    output logic [31:0] side_effect_count,
    output logic        sc_res1,
    output logic        sc_res2,
    output logic        sc_res3,
    output logic        sc_res4
);

    int se_counter;

    function int bump_counter(int val);
        se_counter++;
        return val;
    endfunction

    always_comb begin
        int i;
        int j;
        int w;
        int wc;
        int sum_b;
        int sum_c;
        int sum_wc;

        // --- 1. For loop with break ---
        sum_b = 0;
        for (i = 0; i < 20; i++) begin
            if (i == 10) break;
            sum_b += i;
        end
        for_break_idx = i;     // Should be 10 (step i++ does not execute on break)
        for_break_sum = sum_b; // Sum of 0..9 = 45

        // --- 2. For loop with continue ---
        sum_c = 0;
        for (j = 0; j < 10; j++) begin
            if ((j % 2) != 0) continue; // Skip odd numbers: accumulate 0, 2, 4, 6, 8
            sum_c += j;
        end
        for_cont_sum = sum_c; // 0 + 2 + 4 + 6 + 8 = 20

        // --- 3. While loop with break and continue ---
        w = 0;
        while (w < 100) begin
            if (w == 15) break;
            w++;
        end
        while_break_idx = w; // Should be 15

        wc = 0;
        sum_wc = 0;
        while (wc < 10) begin
            wc++;
            if ((wc % 2) != 0) continue; // Accumulate even values of wc (2, 4, 6, 8, 10)
            sum_wc += wc;
        end
        while_cont_sum = sum_wc; // 2 + 4 + 6 + 8 + 10 = 30

        // --- 4. Short-circuit evaluation ---
        se_counter = 0;

        // 0 && bump_counter(): bump_counter MUST NOT be called
        sc_res1 = (1'b0 && (bump_counter(1) != 0));

        // 1 || bump_counter(): bump_counter MUST NOT be called
        sc_res2 = (1'b1 || (bump_counter(1) != 0));

        // 1 && bump_counter(): bump_counter MUST be called (counter becomes 1)
        sc_res3 = (1'b1 && (bump_counter(1) != 0));

        // 0 || bump_counter(): bump_counter MUST be called (counter becomes 2)
        sc_res4 = (1'b0 || (bump_counter(1) != 0));

        side_effect_count = se_counter; // Should be exactly 2
    end

endmodule
