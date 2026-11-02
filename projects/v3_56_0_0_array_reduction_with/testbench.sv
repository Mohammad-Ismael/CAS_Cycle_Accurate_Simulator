`timescale 1ns/1ps

// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.56.0.0
// Standard Target: IEEE Std 1800-2023 §7.12 (Array Reduction with 'with' Clause)
// Testbench Name : tb_v3_56_0_regression
// ============================================================================

module tb_v3_56_0_regression;
    import v3_56_0_0_pkg::*;

    logic        clk;
    logic        rst_n;
    logic [31:0] input_sample;
    logic [31:0] total_sum_out;
    logic [31:0] filtered_sum_out;
    logic [31:0] parity_xor_out;
    logic        calc_valid;

    int pass_count  = 0;
    int error_count = 0;

    v3_56_0_0_metric_calculator dut (
        .clk             (clk),
        .rst_n           (rst_n),
        .input_sample    (input_sample),
        .total_sum_out   (total_sum_out),
        .filtered_sum_out(filtered_sum_out),
        .parity_xor_out  (parity_xor_out),
        .calc_valid      (calc_valid)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        int test_arr[];
        int raw_sum;
        int weighted_sum;
        int count_gt_15;
        int bit_xor;

        rst_n        = 0;
        input_sample = 32'd0;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Procedural evaluation of sum(), xor(), and with clauses
        // test_arr = '{10, 25, 5, 30}
        //   raw sum      = 10 + 25 + 5 + 30 = 70
        //   weighted sum = (10*2) + (25*2) + (5*2) + (30*2) = 140
        //   count > 15   = 0 + 1 + 0 + 1 = 2
        //   xor parity   = 10 ^ 25 ^ 5 ^ 30 = 10 (00001010 ^ 00011001 ^ 00000101 ^ 00011110)
        //                  10 ^ 25 = 19
        //                  19 ^ 5  = 22
        //                  22 ^ 30 = 8
        // --------------------------------------------------------------------
        test_arr = '{10, 25, 5, 30};

        raw_sum      = test_arr.sum();
        weighted_sum = test_arr.sum() with ( item * 2 );
        count_gt_15  = test_arr.sum() with ( int'(item > 15) );
        bit_xor      = test_arr.xor();

        if (raw_sum == 70 && weighted_sum == 140 && count_gt_15 == 2 && bit_xor == 8) begin
            $display("[PASS T=%0t] Procedural array reduction with 'with' clause verified: sum=%0d weight=%0d cnt=%0d xor=%0d",
                     $time, raw_sum, weighted_sum, count_gt_15, bit_xor);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Procedural array reduction failed! sum=%0d weight=%0d cnt=%0d xor=%0d",
                     $time, raw_sum, weighted_sum, count_gt_15, bit_xor);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: Fixed unpacked array reduction with iterator
        // --------------------------------------------------------------------
        begin
            int fixed_nums[3] = '{4, 7, 2};
            int fixed_prod = fixed_nums.product(); // 4 * 7 * 2 = 56
            int fixed_cond = fixed_nums.sum() with ( item >= 4 ? item : 0 ); // 4 + 7 = 11

            if (fixed_prod == 56 && fixed_cond == 11) begin
                $display("[PASS T=%0t] Fixed unpacked array reduction verified: prod=%0d cond_sum=%0d",
                         $time, fixed_prod, fixed_cond);
                pass_count++;
            end else begin
                $display("[FAIL T=%0t] Fixed unpacked array reduction failed! prod=%0d cond_sum=%0d",
                         $time, fixed_prod, fixed_cond);
                error_count++;
            end
        end

        // --------------------------------------------------------------------
        // Test 3: Sequential DUT metric calculator execution
        // Feed 4 samples: 10, 25, 5, 30
        // Expected outputs at cycle 4:
        //   total_sum    = 70
        //   filtered_sum = 25 + 30 = 55 (only items > 20)
        //   parity_xor   = 8
        //   calc_valid   = 1
        // --------------------------------------------------------------------
        @(negedge clk); input_sample = 32'd10;
        @(negedge clk); input_sample = 32'd25;
        @(negedge clk); input_sample = 32'd5;
        @(negedge clk); input_sample = 32'd30;

        @(posedge clk);
        #1;
        if (total_sum_out === 32'd70 &&
            filtered_sum_out === 32'd55 &&
            parity_xor_out === 32'd8 &&
            calc_valid === 1'b1) begin
            $display("[PASS T=%0t] DUT metric calculator verified: total=%0d filtered=%0d xor=%0d valid=%b",
                     $time, total_sum_out, filtered_sum_out, parity_xor_out, calc_valid);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] DUT metric calculator failed! total=%0d filtered=%0d xor=%0d valid=%b",
                     $time, total_sum_out, filtered_sum_out, parity_xor_out, calc_valid);
            error_count++;
        end

        @(negedge clk);

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.56.0.0 REGRESSION SUITE: ARRAY REDUCTION WITH CLAUSES §7.12]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §7.12 array reduction methods with 'with' clauses.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule