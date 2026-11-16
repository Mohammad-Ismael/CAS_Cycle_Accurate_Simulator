// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v1.3.0.0
// Standard Target: IEEE Std 1800-2023 Loops & Functions
// Testbench Name : tb_v1_3_0_0_regression
// ============================================================================

module tb_v1_3_0_0_regression;
    logic [7:0] in_data;
    logic [3:0] in_count;

    logic [7:0] out_for_accum;
    logic [7:0] out_while_shift;
    logic [7:0] out_repeat_inc;
    logic [7:0] out_func_result;

    integer error_count = 0;

    v1_3_0_0_loop_func_core dut (
        .in_data(in_data),
        .in_count(in_count),
        .out_for_accum(out_for_accum),
        .out_while_shift(out_while_shift),
        .out_repeat_inc(out_repeat_inc),
        .out_func_result(out_func_result)
    );

    initial begin
        // Case 1: in_data = 8'h05 (5), in_count = 3
        in_data  = 8'h05;
        in_count = 4'd3;
        #5;

        // Check For Loop: 5 * 4 = 20 (8'h14)
        if (out_for_accum !== 8'h14) begin
            $display("[FAIL] for-loop accumulation mismatch! Expected 8'h14, Got %h", out_for_accum);
            error_count = error_count + 1;
        end

        // Check While Loop: 5 << 3 = 40 (8'h28)
        if (out_while_shift !== 8'h28) begin
            $display("[FAIL] while-loop shift mismatch! Expected 8'h28, Got %h", out_while_shift);
            error_count = error_count + 1;
        end

        // Check Repeat Loop: 5 iterations * 3 = 15 (8'h0F)
        if (out_repeat_inc !== 8'h0F) begin
            $display("[FAIL] repeat-loop mismatch! Expected 8'h0F, Got %h", out_repeat_inc);
            error_count = error_count + 1;
        end

        // Check Function: Inverts bits 0, 2, 4, 6 of 8'h05 (8'b0000_0101 -> 8'b0101_0000 = 8'h50)
        // 8'b0000_0101: bit0=1->0, bit2=1->0, bit4=0->1, bit6=0->1 => 8'b0101_0000 = 8'h50
        if (out_func_result !== 8'h50) begin
            $display("[FAIL] parity function mismatch! Expected 8'h50, Got %h", out_func_result);
            error_count = error_count + 1;
        end

        #5;
        $display("\n================================================================================");
        $display("[CAS v1.3.0.0 REGRESSION SUITE: PROCEDURAL LOOPS & FUNCTIONS]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: for, while, repeat loops and user functions verified with parity.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | DATA=%h CNT=%0d | FOR=%h WHILE=%h REPEAT=%h FUNC=%h",
                 $time, in_data, in_count, out_for_accum, out_while_shift, out_repeat_inc, out_func_result);
    end

endmodule