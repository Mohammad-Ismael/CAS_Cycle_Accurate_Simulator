// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v2.1.0.0
// Standard Target: IEEE Std 1800-2023 §13.4.2 (automatic functions)
//                  and concurrent re-entrant tasks (§13.5.1)
// Testbench Name : tb_v2_1_0_0_regression
// ============================================================================

module tb_v2_1_0_0_regression;
    logic [3:0]  n_in;
    logic [31:0] fact_out;
    integer      error_count = 0;

    // DUT instantiation
    v2_1_0_0_automatic_core dut (
        .n_in(n_in),
        .fact_out(fact_out)
    );

    // Shared automatic task called concurrently by two processes
    task automatic concurrent_worker(
        input  integer id,
        input  integer delay_ticks,
        input  integer seed_val,
        output integer result
    );
        integer local_accum; // Must be dynamically allocated per invocation frame
        begin
            local_accum = seed_val;
            #delay_ticks;
            local_accum = local_accum + (id * 10);
            #delay_ticks;
            result = local_accum;
        end
    endtask

    integer res_p1 = 0;
    integer res_p2 = 0;

    // Process 1: calls worker with id=1, seed=5, delay=4
    initial begin
        concurrent_worker(1, 4, 5, res_p1);
    end

    // Process 2: calls worker concurrently with id=2, seed=100, delay=2
    initial begin
        concurrent_worker(2, 2, 100, res_p2);
    end

    // Main verification process
    initial begin
        // --------------------------------------------------------------------
        // TEST 1: Recursive automatic function evaluations
        // --------------------------------------------------------------------
        n_in = 4'd0; #1;
        if (fact_out !== 32'd1) begin
            $display("[FAIL T=1] Factorial(0) failed! fact_out=%0d (exp 1)", fact_out);
            error_count = error_count + 1;
        end

        n_in = 4'd1; #1;
        if (fact_out !== 32'd1) begin
            $display("[FAIL T=2] Factorial(1) failed! fact_out=%0d (exp 1)", fact_out);
            error_count = error_count + 1;
        end

        n_in = 4'd4; #1; // 4! = 24
        if (fact_out !== 32'd24) begin
            $display("[FAIL T=3] Factorial(4) failed! fact_out=%0d (exp 24)", fact_out);
            error_count = error_count + 1;
        end

        n_in = 4'd6; #1; // 6! = 720
        if (fact_out !== 32'd720) begin
            $display("[FAIL T=4] Factorial(6) failed! fact_out=%0d (exp 720)", fact_out);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Concurrent re-entrant task verification
        // --------------------------------------------------------------------
        // Process 2 finishes at T=4: seed=100 + (2*10) = 120
        #1; // T=5
        if (res_p2 !== 120) begin
            $display("[FAIL T=5] Concurrent worker P2 failed! res_p2=%0d (exp 120)", res_p2);
            error_count = error_count + 1;
        end

        // Process 1 finishes at T=8: seed=5 + (1*10) = 15
        #4; // T=9
        if (res_p1 !== 15) begin
            $display("[FAIL T=9] Concurrent worker P1 failed! res_p1=%0d (exp 15)", res_p1);
            error_count = error_count + 1;
        end

        #2; // T=11
        $display("\n================================================================================");
        $display("[CAS v2.1.0.0 REGRESSION SUITE: AUTOMATIC FUNCTIONS AND RE-ENTRANT TASKS]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §13.4 & §13.5 automatic recursion and stack frames.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | n_in=%0d fact_out=%0d | res_p1=%0d res_p2=%0d",
                 $time, n_in, fact_out, res_p1, res_p2);
    end

endmodule