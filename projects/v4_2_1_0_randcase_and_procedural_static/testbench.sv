`timescale 1ns/1ps

module tb_v4_2_1_regression;
    import v4_2_1_0_pkg::*;

    logic [31:0] mode;
    logic [31:0] out_det;
    logic [31:0] out_calls;

    randcase_static_dut dut (
        .mode(mode),
        .out_det(out_det),
        .out_calls(out_calls)
    );

    task automatic test_static_task(output int c);
        static int task_counter = 50;
        task_counter += 10;
        c = task_counter;
    endtask

    int errors = 0;

    initial begin
        int res;
        int t_cnt;

        // 1. Test deterministic randcase in package function
        mode = 0;
        #1;
        if (out_det !== 200) begin
            $display("ERROR: Expected out_det=200 for mode 0, got %0d", out_det);
            errors++;
        end

        mode = 1;
        #1;
        if (out_det !== 555) begin
            $display("ERROR: Expected out_det=555 for mode 1, got %0d", out_det);
            errors++;
        end

        // 2. Test in-line procedural randcase
        res = 0;
        randcase
            0: res = 1;
            50: res = 42;
            0: res = 99;
        endcase
        if (res !== 42) begin
            $display("ERROR: Expected in-line randcase res=42, got %0d", res);
            errors++;
        end

        // 3. Test static variable persistence in package function across calls
        for (int i = 1; i <= 5; i++) begin
            int c = count_static_calls();
            if (c !== i) begin
                $display("ERROR: Expected static call_count=%0d, got %0d", i, c);
                errors++;
            end
        end

        // 4. Test static variable persistence in automatic task
        test_static_task(t_cnt);
        if (t_cnt !== 60) begin
            $display("ERROR: Expected task_counter=60, got %0d", t_cnt);
            errors++;
        end

        test_static_task(t_cnt);
        if (t_cnt !== 70) begin
            $display("ERROR: Expected task_counter=70, got %0d", t_cnt);
            errors++;
        end

        // 5. Final assertion
        if (errors == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
        end else begin
            $display("RESULT : >>> FAIL (%0d errors) <<<", errors);
        end
        $finish;
    end

endmodule
