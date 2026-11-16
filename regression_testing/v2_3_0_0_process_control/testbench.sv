// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v2.3.0.0
// Standard Target: IEEE Std 1800-2023 §9.6.2 (disable fork) & §9.6.3 (wait fork)
// Testbench Name : tb_v2_3_0_0_regression
// ============================================================================

module tb_v2_3_0_0_regression;
    logic       clk;
    logic [7:0] d_in;
    logic [7:0] d_out;

    integer     flag_fast = 0;
    integer     flag_slow = 0;
    integer     flag_wait = 0;
    integer     error_count = 0;

    v2_3_0_0_proc_ctrl_core dut (
        .clk(clk),
        .d_in(d_in),
        .d_out(d_out)
    );

    initial begin
        // --------------------------------------------------------------------
        // TEST 1: 'disable fork' (§9.6.2)
        // Spawn two parallel tasks in join_any:
        // - Fast worker: finishes at T = 4
        // - Slow worker: planned for T = 12
        // When fast worker wins join_any at T = 4, issue 'disable fork'.
        // Slow worker must be killed immediately and never reach T = 12.
        // --------------------------------------------------------------------
        fork
            begin
                #4;
                flag_fast = 1;
            end
            begin
                #12;
                flag_slow = 1; // Should never execute if disabled
            end
        join_any
        disable fork; // Terminates the remaining slow worker thread

        #10; // Advance past T = 12 (now at T = 14)
        if ($time !== 14 || flag_fast !== 1 || flag_slow !== 0) begin
            $display("[FAIL T=14] disable fork failed! flag_fast=%0d flag_slow=%0d (exp fast=1, slow=0)",
                     flag_fast, flag_slow);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: 'wait fork' (§9.6.3)
        // Spawn two background processes via join_none at T = 14:
        // - Worker A: takes 3 time units (finishes at T = 17)
        // - Worker B: takes 6 time units (finishes at T = 20)
        // Parent does not block at join_none, does some work, then calls 'wait fork'.
        // 'wait fork' must block parent until T = 20 when Worker B completes.
        // --------------------------------------------------------------------
        fork
            begin
                #3;
                flag_wait = flag_wait + 10; // T = 17
            end
            begin
                #6;
                flag_wait = flag_wait + 20; // T = 20
            end
        join_none

        // Parent does local work at T = 14
        #1; // T = 15
        flag_wait = flag_wait + 1; // flag_wait = 1

        // Wait until all children finish
        wait fork; // Must unblock strictly at T = 20

        if ($time !== 20 || flag_wait !== 31) begin
            $display("[FAIL T=20] wait fork failed! time=%0t flag_wait=%0d (exp T=20, flag_wait=31)",
                     $time, flag_wait);
            error_count = error_count + 1;
        end

        #5; // T = 25
        $display("\n================================================================================");
        $display("[CAS v2.3.0.0 REGRESSION SUITE: PROCESS CONTROL (DISABLE FORK / WAIT FORK)]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §9.6.2 and §9.6.3 process controls.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | flag_fast=%0d flag_slow=%0d flag_wait=%0d",
                 $time, flag_fast, flag_slow, flag_wait);
    end
endmodule