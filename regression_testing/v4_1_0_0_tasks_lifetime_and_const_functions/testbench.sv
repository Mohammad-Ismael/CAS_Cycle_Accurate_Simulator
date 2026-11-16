`timescale 1ns/1ps

module tb_v4_1_0_regression;

    logic        clk;
    logic [31:0] const_out;
    int          error_count = 0;

    tasks_lifetime_and_const_functions_dut dut (
        .clk(clk),
        .const_out(const_out)
    );

    // 2. Task automatic: local variables re-initialized each call (IEEE 1800-2023 13.3.1)
    task automatic test_auto(output int res);
        int a = 10;
        a++;
        res = a;
    endtask

    // 3. Task static: local variables retain value across calls (IEEE 1800-2023 13.3.1)
    task static test_static(output int res);
        int a = 100;
        a++;
        res = a;
    endtask

    initial begin
        int auto1, auto2, auto3;
        int static1, static2, static3;

        #1;
        // Test 1: Constant function in localparam
        if (const_out != 32'd22) begin
            $display("[FAIL] const_out expected 22, got %0d", const_out);
            error_count++;
        end

        // Test 2: Task automatic re-initialization
        test_auto(auto1);
        test_auto(auto2);
        test_auto(auto3);
        if (auto1 != 11 || auto2 != 11 || auto3 != 11) begin
            $display("[FAIL] task automatic failed: %0d, %0d, %0d (exp 11, 11, 11)", auto1, auto2, auto3);
            error_count++;
        end

        // Test 3: Task static retention
        test_static(static1);
        test_static(static2);
        test_static(static3);
        if (static1 != 101 || static2 != 102 || static3 != 103) begin
            $display("[FAIL] task static failed: %0d, %0d, %0d (exp 101, 102, 103)", static1, static2, static3);
            error_count++;
        end

        #5;
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
        end else begin
            $display("RESULT : >>> FAIL (%0d errors) <<<", error_count);
        end
        $finish;
    end

endmodule
