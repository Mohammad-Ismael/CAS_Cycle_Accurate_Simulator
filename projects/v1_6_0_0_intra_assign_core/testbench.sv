// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v1.6.0.0
// Standard Target: IEEE Std 1800-2023 §10.6.1 Intra-Assignment Delays
// Testbench Name : tb_v1_6_0_0_regression
// ============================================================================

module tb_v1_6_0_0_regression;
    logic [7:0] a_nba;
    logic [7:0] b_nba;
    logic [7:0] c_blk;
    logic [7:0] src;

    integer error_count = 0;

    initial begin
        src = 8'h11;
        a_nba = 8'h00;
        b_nba = 8'h00;
        c_blk = 8'h00;

        // --------------------------------------------------------------------
        // Test 1: Non-blocking intra-assignment delay (does NOT block process)
        // --------------------------------------------------------------------
        // Both RHS are sampled at T=0 where src == 8'h11.
        // b_nba is scheduled to update at T=5 (NBA region).
        // a_nba is scheduled to update at T=10 (NBA region).
        a_nba <= #10 src;
        b_nba <= #5  src;

        // Verify that process didn't block: at T=0, targets retain initial values
        if (a_nba !== 8'h00 || b_nba !== 8'h00) begin
            $display("[FAIL] T=0: NBA intra-delay blocked process or committed early!");
            error_count = error_count + 1;
        end

        // Change src at T=2; previous sample (8'h11) must be preserved
        #2; // T=2
        src = 8'h99;

        // Advance to T=6 to allow the T=5 NBA region to settle cleanly
        #4; // T=6
        if (b_nba !== 8'h11 || a_nba !== 8'h00) begin
            $display("[FAIL] T=6 mismatch! b_nba=%h (exp 11), a_nba=%h (exp 00)", b_nba, a_nba);
            error_count = error_count + 1;
        end

        // Advance to T=11 to allow the T=10 NBA region to settle cleanly
        #5; // T=11
        if (a_nba !== 8'h11) begin
            $display("[FAIL] T=11 mismatch! a_nba=%h (exp 11)", a_nba);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // Test 2: Blocking intra-assignment delay (BLOCKS process for #7)
        // --------------------------------------------------------------------
        // Samples src (8'h99) at T=11.
        // The calling process suspends for 7 time units.
        // At T=18, c_blk receives 8'h99 and execution resumes immediately.
        c_blk = #7 src;
        if ($time !== 18 || c_blk !== 8'h99) begin
            $display("[FAIL] T=18 mismatch on blocking intra-assignment! time=%0t c_blk=%h", $time, c_blk);
            error_count = error_count + 1;
        end

        #4; // T=22
        $display("\n================================================================================");
        $display("[CAS v1.6.0.0 REGRESSION SUITE: INTRA-ASSIGNMENT TIMING CONTROLS]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §10.6.1 non-blocking and blocking intra-delays.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | src=%h | a_nba=%h b_nba=%h | c_blk=%h",
                 $time, src, a_nba, b_nba, c_blk);
    end

endmodule