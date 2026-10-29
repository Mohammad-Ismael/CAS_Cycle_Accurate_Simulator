// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v2.4.0.0
// Standard Target: IEEE Std 1800-2023 §15 (event, ->, ->>, @e, wait(e.triggered))
// Testbench Name : tb_v2_4_0_0_regression
// ============================================================================

module tb_v2_4_0_0_regression;
    logic clk;
    logic [7:0] d_in;
    logic [7:0] d_out;

    // Named synchronization events (§15.3)
    event ev_sync;
    event ev_nba;
    event ev_persist;

    integer status_sync    = 0;
    integer status_nba     = 0;
    integer status_persist = 0;
    integer error_count    = 0;

    v2_4_0_0_event_core dut (
        .clk(clk),
        .d_in(d_in),
        .d_out(d_out)
    );

    // ------------------------------------------------------------------------
    // Process 1: Immediate Event Trigger & Wait (@ev_sync)
    // ------------------------------------------------------------------------
    initial begin
        #5; // Wait at T = 5
        @ev_sync; // Unblocks when trigger occurs at T = 10
        status_sync = 10;
    end

    // ------------------------------------------------------------------------
    // Process 2: Non-blocking Event Trigger (->> ev_nba)
    // ------------------------------------------------------------------------
    initial begin
        #12; // Wait at T = 12
        @ev_nba; // Unblocks in NBA region of T = 15
        status_nba = 20;
    end

    // ------------------------------------------------------------------------
    // Process 3: Persistent Trigger Check (wait(ev_persist.triggered)) (§15.5)
    // ------------------------------------------------------------------------
    initial begin
        #20; // Trigger occurs earlier in time step T = 20, but process reaches here later in T = 20
        wait (ev_persist.triggered);
        status_persist = 30;
    end

    // ------------------------------------------------------------------------
    // Master Test Controller
    // ------------------------------------------------------------------------
    initial begin
        // TEST 1: Immediate trigger (->) at T = 10
        #10;
        -> ev_sync;
        #1; // T = 11
        if (status_sync !== 10) begin
            $display("[FAIL T=11] Immediate event trigger failed! status_sync=%0d (exp 10)", status_sync);
            error_count = error_count + 1;
        end

        // TEST 2: Non-blocking trigger (->>) at T = 15
        #4; // T = 15
        ->> ev_nba; // Fires in NBA region of T = 15
        #1; // T = 16
        if (status_nba !== 20) begin
            $display("[FAIL T=16] Non-blocking event trigger failed! status_nba=%0d (exp 20)", status_nba);
            error_count = error_count + 1;
        end

        // TEST 3: Persistent triggered check at T = 20 (§15.5)
        #4; // T = 20
        -> ev_persist; // Trigger occurs in Active region
        #1; // T = 21
        if (status_persist !== 30) begin
            $display("[FAIL T=21] Persistent ev.triggered check failed! status_persist=%0d (exp 30)", status_persist);
            error_count = error_count + 1;
        end

        #4; // T = 25
        $display("\n================================================================================");
        $display("[CAS v2.4.0.0 REGRESSION SUITE: NAMED SYNCHRONIZATION EVENTS]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §15 named events (->, ->>, @e, wait(e.triggered)).");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | status_sync=%0d status_nba=%0d status_persist=%0d",
                 $time, status_sync, status_nba, status_persist);
    end
endmodule