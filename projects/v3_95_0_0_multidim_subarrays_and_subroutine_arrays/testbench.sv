// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.95.0.0
// Testbench Name : tb_v3_95_0_regression
// ============================================================================

`timescale 1ns/1ps

module tb_v3_95_0_regression;
    logic        clk;
    logic        rst_n;
    logic [31:0] out_word_copy;
    int          out_sub_0;
    int          out_sub_1;
    int          out_sub_2;
    int          out_sub_3;
    int          out_task_sum;
    int          out_func_sum;

    v3_95_0_0_multidim_subarrays_and_subroutine_arrays_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .out_word_copy(out_word_copy),
        .out_sub_0(out_sub_0),
        .out_sub_1(out_sub_1),
        .out_sub_2(out_sub_2),
        .out_sub_3(out_sub_3),
        .out_task_sum(out_task_sum),
        .out_func_sum(out_func_sum)
    );

    int errors = 0;

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n = 0;
        #1;

        if (out_word_copy !== 32'hDEADBEEF) begin
            $display("[FAIL] out_word_copy = %h, expected DEADBEEF", out_word_copy);
            errors = errors + 1;
        end else begin
            $display("[PASS] out_word_copy = %h", out_word_copy);
        end

        if (out_sub_0 !== 5 || out_sub_1 !== 6 || out_sub_2 !== 7 || out_sub_3 !== 8) begin
            $display("[FAIL] subarray = [%0d, %0d, %0d, %0d], expected [5, 6, 7, 8]",
                     out_sub_0, out_sub_1, out_sub_2, out_sub_3);
            errors = errors + 1;
        end else begin
            $display("[PASS] subarray = [%0d, %0d, %0d, %0d]",
                     out_sub_0, out_sub_1, out_sub_2, out_sub_3);
        end

        if (out_task_sum !== 60) begin
            $display("[FAIL] out_task_sum = %0d, expected 60", out_task_sum);
            errors = errors + 1;
        end else begin
            $display("[PASS] out_task_sum = %0d", out_task_sum);
        end

        if (out_func_sum !== 60) begin
            $display("[FAIL] out_func_sum = %0d, expected 60", out_func_sum);
            errors = errors + 1;
        end else begin
            $display("[PASS] out_func_sum = %0d", out_func_sum);
        end

        #10;
        if (errors == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
        end else begin
            $display("RESULT : >>> FAIL (%0d errors) <<<", errors);
        end
        $finish;
    end
endmodule
