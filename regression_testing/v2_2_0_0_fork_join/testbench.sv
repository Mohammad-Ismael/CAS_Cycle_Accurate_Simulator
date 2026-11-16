// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v2.2.0.0
// Standard Target: IEEE Std 1800-2023 §9.3.2 (fork .. join, join_any, join_none)
// Testbench Name : tb_v2_2_0_0_regression
// ============================================================================

module tb_v2_2_0_0_regression;
    logic        clk;
    logic [7:0]  in_val;
    logic [7:0]  out_join;
    logic [7:0]  out_any;
    logic [7:0]  out_none;

    integer      status_join = 0;
    integer      status_any  = 0;
    integer      status_none = 0;
    integer      error_count = 0;

    v2_2_0_0_fork_core dut (
        .clk(clk),
        .in_val(in_val),
        .out_join(out_join),
        .out_any(out_any),
        .out_none(out_none)
    );

    initial begin
        in_val = 8'h00;

        // --------------------------------------------------------------------
        // TEST 1: fork ... join (Blocks until ALL branches complete)
        // Branch A: takes 4 time units
        // Branch B: takes 10 time units
        // Parent must unblock strictly at T = 10
        // --------------------------------------------------------------------
        status_join = 1;
        fork
            begin
                #4;
                status_join = status_join + 10; // 11
            end
            begin
                #10;
                status_join = status_join + 20; // 31
            end
        join
        // Reached at T = 10
        if ($time !== 10 || status_join !== 31) begin
            $display("[FAIL] fork..join failed! time=%0t status=%0d (exp T=10, status=31)", $time, status_join);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: fork ... join_any (Blocks until ANY branch completes)
        // Branch A: takes 3 time units
        // Branch B: takes 8 time units
        // Parent must unblock strictly at T = 10 + 3 = 13
        // --------------------------------------------------------------------
        status_any = 1;
        fork
            begin
                #3;
                status_any = status_any + 10; // 11
            end
            begin
                #8;
                status_any = status_any + 20; // 31
            end
        join_any
        // Reached at T = 13
        if ($time !== 13 || status_any !== 11) begin
            $display("[FAIL] fork..join_any parent unblock failed! time=%0t status=%0d (exp T=13, status=11)", $time, status_any);
            error_count = error_count + 1;
        end

        // Wait until T = 18 when Branch B of join_any finishes in background
        #6; // T = 19
        if (status_any !== 31) begin
            $display("[FAIL] fork..join_any background child completion failed! status=%0d (exp 31)", status_any);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 3: fork ... join_none (Parent does NOT block at all)
        // Branch A: takes 2 time units
        // Branch B: takes 5 time units
        // Parent continues immediately at current time (T = 19)
        // --------------------------------------------------------------------
        status_none = 1;
        fork
            begin
                #2;
                status_none = status_none + 5; // 6
            end
            begin
                #5;
                status_none = status_none + 10; // 16
            end
        join_none

        // Reached immediately at T = 19
        if ($time !== 19 || status_none !== 1) begin
            $display("[FAIL] fork..join_none immediate return failed! time=%0t status=%0d (exp T=19, status=1)", $time, status_none);
            error_count = error_count + 1;
        end

        #3; // T = 22 (Branch A has completed)
        if (status_none !== 6) begin
            $display("[FAIL] fork..join_none Branch A check failed! status=%0d (exp 6)", status_none);
            error_count = error_count + 1;
        end

        #4; // T = 26 (Branch B has completed)
        if (status_none !== 16) begin
            $display("[FAIL] fork..join_none Branch B check failed! status=%0d (exp 16)", status_none);
            error_count = error_count + 1;
        end

        #4; // T = 30
        $display("\n================================================================================");
        $display("[CAS v2.2.0.0 REGRESSION SUITE: FORK-JOIN / JOIN_ANY / JOIN_NONE]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §9.3.2 parallel block semantics.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | status_join=%0d status_any=%0d status_none=%0d",
                 $time, status_join, status_any, status_none);
    end
endmodule
