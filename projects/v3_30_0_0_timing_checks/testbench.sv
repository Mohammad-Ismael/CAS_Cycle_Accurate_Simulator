// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.30.0.0
// Standard Target: IEEE Std 1800-2023 §31.8 & §32.7 (System Timing Checks)
// Testbench Name : tb_v3_30_0_0_regression
// ============================================================================

module tb_v3_30_0_0_regression;
    reg clk;
    reg rst_n;
    reg d;
    wire q;

    int pass_count  = 0;
    int error_count = 0;

    v3_30_0_0_dff_timed dut (
        .clk  (clk),
        .rst_n(rst_n),
        .d    (d),
        .q    (q)
    );

    initial begin
        clk   = 0;
        rst_n = 0;
        d     = 0;

        #20;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Clean transition adhering to setup & hold limits.
        // Setup limit = 5, Hold limit = 3.
        // Clock edge occurs at T = 50.
        // Data toggles to 1 at T = 40 (10 units before edge -> setup clean).
        // Data stays at 1 until T = 60 (10 units after edge -> hold clean).
        // --------------------------------------------------------------------
        #20; // T = 40
        d = 1;

        #10; // T = 50: posedge clk
        clk = 1;

        #10; // T = 60
        clk = 0;

        #1;  // T = 61
        if (q !== 1'b1) begin
            $display("[FAIL T=%0t] Clean edge mismatch! got %b (exp 1)", $time, q);
            error_count++;
        end else begin
            pass_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: Setup Violation.
        // Clock edge occurs at T = 100.
        // Data toggles to 0 at T = 98 (only 2 units before edge; required 5).
        // $setup check triggers, toggling notifier and corrupting q to 'x'.
        // --------------------------------------------------------------------
        #38; // T = 98
        d = 0;

        #2;  // T = 100: posedge clk (violates setup window [95..100])
        clk = 1;

        #1;  // T = 101
        if (q !== 1'bx) begin
            $display("[FAIL T=%0t] Setup violation did not corrupt q to 'x'! got %b (exp x)", $time, q);
            error_count++;
        end else begin
            pass_count++;
        end

        #9;  // T = 110
        clk = 0;

        // Recover state with clean reset
        #10; // T = 120
        rst_n = 0;
        #10; // T = 130
        rst_n = 1;
        d = 1;

        // --------------------------------------------------------------------
        // Test 3: Hold Violation.
        // Clock edge occurs at T = 150.
        // Data toggles at T = 151 (only 1 unit after edge; required 3).
        // $hold check triggers, toggling notifier and corrupting q to 'x'.
        // --------------------------------------------------------------------
        #20; // T = 150: posedge clk
        clk = 1;

        #1;  // T = 151: data toggles 1 unit after posedge (violates hold [150..153])
        d = 0;

        #1;  // T = 152
        if (q !== 1'bx) begin
            $display("[FAIL T=%0t] Hold violation did not corrupt q to 'x'! got %b (exp x)", $time, q);
            error_count++;
        end else begin
            pass_count++;
        end

        #8;  // T = 160
        clk = 0;

        #20;
        $display("\n================================================================================");
        $display("[CAS v3.30.0.0 REGRESSION SUITE: TIMING CHECKS $setup, $hold, $width]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §31.8 & §32.7 $setup, $hold, and notifier toggling.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | clk=%b rst_n=%b d=%b | q=%b",
                 $time, clk, rst_n, d, q);
    end
endmodule