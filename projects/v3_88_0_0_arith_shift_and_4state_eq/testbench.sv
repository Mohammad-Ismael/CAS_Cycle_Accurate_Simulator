`timescale 1ns/1ps

//==============================================================================
// Testbench: tb_v3_88_0_regression
// Standard: IEEE 1800-2023 SystemVerilog (§11.4.10 arith shifts, §11.4.5 4-state equality)
//==============================================================================

module tb_v3_88_0_regression;

    logic         clk;
    logic         rst_n;

    logic signed [7:0]  out_s8_shl;
    logic signed [7:0]  out_s8_shr;
    logic signed [7:0]  out_s8_assign_shl;
    logic signed [7:0]  out_s8_assign_shr;
    logic        [7:0]  out_u8_shl;
    logic        [7:0]  out_u8_shr;
    logic        [7:0]  out_u8_assign_shl;
    logic        [7:0]  out_u8_assign_shr;

    logic signed [127:0] out_s128_shl;
    logic signed [127:0] out_s128_shr;
    logic signed [127:0] out_s128_assign_shl;
    logic signed [127:0] out_s128_assign_shr;
    logic        [127:0] out_u128_shl;
    logic        [127:0] out_u128_shr;
    logic        [127:0] out_u128_assign_shl;
    logic        [127:0] out_u128_assign_shr;

    logic         out_eq_diff1;
    logic         out_neq_diff1;
    logic         out_case_eq1;
    logic         out_case_neq1;

    logic         out_eq_diff2;
    logic         out_neq_diff2;
    logic         out_case_eq2;
    logic         out_case_neq2;

    logic         out_eq_unk;
    logic         out_neq_unk;
    logic         out_case_eq_unk;
    logic         out_case_neq_unk;

    logic         out_eq_match;
    logic         out_neq_match;
    logic         out_case_eq_match;
    logic         out_case_neq_match;

    integer errors = 0;

    v3_88_0_0_arith_shift_and_4state_eq dut (.*);

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n = 0;
        #10;
        rst_n = 1;
        #10;

        // 1. Verify 8-bit signed arithmetic shifts
        if (out_s8_shl !== 8'sd64) begin
            $display("[FAIL T=%0t] out_s8_shl mismatch: exp=64 got=%0d", $time, out_s8_shl);
            errors++;
        end else begin
            $display("[PASS T=%0t] out_s8_shl = %0d (-120 <<< 3 == 64)", $time, out_s8_shl);
        end

        if (out_s8_shr !== -8'sd15) begin
            $display("[FAIL T=%0t] out_s8_shr mismatch: exp=-15 got=%0d", $time, out_s8_shr);
            errors++;
        end else begin
            $display("[PASS T=%0t] out_s8_shr = %0d (-120 >>> 3 == -15)", $time, out_s8_shr);
        end

        if (out_s8_assign_shl !== 8'sd64) begin
            $display("[FAIL T=%0t] out_s8_assign_shl mismatch: exp=64 got=%0d", $time, out_s8_assign_shl);
            errors++;
        end else begin
            $display("[PASS T=%0t] out_s8_assign_shl = %0d (<<<= 3)", $time, out_s8_assign_shl);
        end

        if (out_s8_assign_shr !== -8'sd15) begin
            $display("[FAIL T=%0t] out_s8_assign_shr mismatch: exp=-15 got=%0d", $time, out_s8_assign_shr);
            errors++;
        end else begin
            $display("[PASS T=%0t] out_s8_assign_shr = %0d (>>>= 3)", $time, out_s8_assign_shr);
        end

        // 2. Verify 8-bit unsigned shifts
        if (out_u8_shl !== 8'd64 || out_u8_shr !== 8'd1) begin
            $display("[FAIL T=%0t] out_u8 shifts mismatch: shl=%0d shr=%0d", $time, out_u8_shl, out_u8_shr);
            errors++;
        end else begin
            $display("[PASS T=%0t] out_u8 shifts (8<<<3==64, 8>>>3==1) verified", $time);
        end

        if (out_u8_assign_shl !== 8'd64 || out_u8_assign_shr !== 8'd1) begin
            $display("[FAIL T=%0t] out_u8 assign shifts mismatch: shl=%0d shr=%0d", $time, out_u8_assign_shl, out_u8_assign_shr);
            errors++;
        end else begin
            $display("[PASS T=%0t] out_u8 assign shifts (<<<=3, >>>=3) verified", $time);
        end

        // 3. Verify 128-bit signed & unsigned shifts
        if (out_s128_shl !== -128'sd4096 || out_s128_shr !== -128'sd64) begin
            $display("[FAIL T=%0t] out_s128 shifts mismatch", $time);
            errors++;
        end else begin
            $display("[PASS T=%0t] out_s128 signed shifts (-1024<<<2==-4096, -1024>>>4==-64) verified", $time);
        end

        if (out_s128_assign_shl !== -128'sd4096 || out_s128_assign_shr !== -128'sd64) begin
            $display("[FAIL T=%0t] out_s128 assign shifts mismatch", $time);
            errors++;
        end else begin
            $display("[PASS T=%0t] out_s128 assign signed shifts (<<<=2, >>>=4) verified", $time);
        end

        if (out_u128_shl !== 128'd4096 || out_u128_shr !== 128'd64) begin
            $display("[FAIL T=%0t] out_u128 shifts mismatch", $time);
            errors++;
        end else begin
            $display("[PASS T=%0t] out_u128 unsigned shifts (1024<<<2==4096, 1024>>>4==64) verified", $time);
        end

        if (out_u128_assign_shl !== 128'd4096 || out_u128_assign_shr !== 128'd64) begin
            $display("[FAIL T=%0t] out_u128 assign shifts mismatch", $time);
            errors++;
        end else begin
            $display("[PASS T=%0t] out_u128 assign unsigned shifts (<<<=2, >>>=4) verified", $time);
        end

        // 4. Verify 4-state definite inequality (§11.4.5)
        if (out_eq_diff1 !== 1'b0 || out_neq_diff1 !== 1'b1) begin
            $display("[FAIL T=%0t] 4-state definite inequality (diff1 with X) failed: eq=%b neq=%b", $time, out_eq_diff1, out_neq_diff1);
            errors++;
        end else begin
            $display("[PASS T=%0t] 4-state definite inequality (1101x001 == 1101x000 -> 0, != -> 1) verified", $time);
        end

        if (out_case_eq1 !== 1'b0 || out_case_neq1 !== 1'b1) begin
            $display("[FAIL T=%0t] case equality (diff1) failed: ===%b !==%b", $time, out_case_eq1, out_case_neq1);
            errors++;
        end else begin
            $display("[PASS T=%0t] case equality (1101x001 === 1101x000 -> 0, !== -> 1) verified", $time);
        end

        if (out_eq_diff2 !== 1'b0 || out_neq_diff2 !== 1'b1) begin
            $display("[FAIL T=%0t] 4-state definite inequality (diff2 with Z) failed: eq=%b neq=%b", $time, out_eq_diff2, out_neq_diff2);
            errors++;
        end else begin
            $display("[PASS T=%0t] 4-state definite inequality (1101z001 == 1101z000 -> 0, != -> 1) verified", $time);
        end

        if (out_case_eq2 !== 1'b0 || out_case_neq2 !== 1'b1) begin
            $display("[FAIL T=%0t] case equality (diff2) failed: ===%b !==%b", $time, out_case_eq2, out_case_neq2);
            errors++;
        end else begin
            $display("[PASS T=%0t] case equality (1101z001 === 1101z000 -> 0, !== -> 1) verified", $time);
        end

        // 5. Verify 4-state unknown equality
        if (out_eq_unk !== 1'bx || out_neq_unk !== 1'bx) begin
            $display("[FAIL T=%0t] 4-state unknown equality failed: eq=%b neq=%b", $time, out_eq_unk, out_neq_unk);
            errors++;
        end else begin
            $display("[PASS T=%0t] 4-state unknown equality (1101x001 == 11010001 -> x, != -> x) verified", $time);
        end

        if (out_case_eq_unk !== 1'b0 || out_case_neq_unk !== 1'b1) begin
            $display("[FAIL T=%0t] case equality (unk) failed: ===%b !==%b", $time, out_case_eq_unk, out_case_neq_unk);
            errors++;
        end else begin
            $display("[PASS T=%0t] case equality (1101x001 === 11010001 -> 0, !== -> 1) verified", $time);
        end

        // 6. Verify 4-state exact match
        if (out_eq_match !== 1'bx || out_neq_match !== 1'bx) begin
            $display("[FAIL T=%0t] 4-state matching with X failed: eq=%b neq=%b", $time, out_eq_match, out_neq_match);
            errors++;
        end else begin
            $display("[PASS T=%0t] 4-state matching with X (1101x001 == 1101x001 -> x, != -> x) verified", $time);
        end

        if (out_case_eq_match !== 1'b1 || out_case_neq_match !== 1'b0) begin
            $display("[FAIL T=%0t] case equality (match) failed: ===%b !==%b", $time, out_case_eq_match, out_case_neq_match);
            errors++;
        end else begin
            $display("[PASS T=%0t] case equality (1101x001 === 1101x001 -> 1, !== -> 0) verified", $time);
        end

        #10;
        if (errors == 0) begin
            $display("\n================================================================================");
            $display("[CAS v3.88.0.0 REGRESSION SUITE: ARITH SHIFT AND 4-STATE EQUALITY]");
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800 arithmetic shifts, compound shift assigns, and §11.4.5.");
            $display("================================================================================\n");
        end else begin
            $display("\n[FATAL] v3_88_0_0_arith_shift_and_4state_eq failed with %0d errors!\n", errors);
        end

        $finish;
    end

endmodule
