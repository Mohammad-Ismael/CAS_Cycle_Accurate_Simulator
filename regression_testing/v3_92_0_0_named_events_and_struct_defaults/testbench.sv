// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.92.0.0
// Standard Target: IEEE Std 1800-2023 §7.2.2 & §9.4.2
// Testbench Name : tb_v3_92_0_regression
// ============================================================================

module tb_v3_92_0_regression;
    logic        clk;
    logic        rst_n;
    logic [3:0]  out_s_lo;
    logic [3:0]  out_s_hi;
    int          out_step_count;
    int          out_bare_count;
    integer      error_count = 0;

    v3_92_0_0_events_struct_defaults_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .out_s_lo(out_s_lo),
        .out_s_hi(out_s_hi),
        .out_step_count(out_step_count),
        .out_bare_count(out_bare_count)
    );

    // Direct event check in testbench
    event tb_ev;
    int   tb_ev_count = 0;

    always @(tb_ev) begin
        tb_ev_count = tb_ev_count + 1;
    end

    // Clock generator (period = 10ns, edges at 5, 15, 25, 35...)
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n = 0;

        // Check T=0 default struct value
        #1;
        if (out_s_lo !== 4'h5) begin
            $display("[FAIL T=%0t] Struct member default lo is %h, expected 5", $time, out_s_lo);
            error_count = error_count + 1;
        end
        if (out_s_hi !== 4'h0) begin
            $display("[FAIL T=%0t] Struct member hi uninitialized is %h, expected 0", $time, out_s_hi);
            error_count = error_count + 1;
        end
        if (out_step_count !== 0 || out_bare_count !== 0) begin
            $display("[FAIL T=%0t] Event counts not 0 at start", $time);
            error_count = error_count + 1;
        end

        // Release reset at T=3
        #2;
        rst_n = 1;

        // Trigger dut.ev_step via task and verify active region scheduling
        #2; // T=5
        dut.trigger_step_event();
        // The awakened process runs in the active region; check before delay
        if (out_step_count !== 0) begin
            $display("[FAIL T=%0t] out_step_count incremented prematurely", $time);
            error_count = error_count + 1;
        end
        #3; // T=8 (next delay)
        if (out_step_count !== 1) begin
            $display("[FAIL T=%0t] out_step_count is %0d, expected 1", $time, out_step_count);
            error_count = error_count + 1;
        end
        if (out_s_hi !== 4'h1) begin
            $display("[FAIL T=%0t] out_s_hi is %h, expected 1", $time, out_s_hi);
            error_count = error_count + 1;
        end
        if (out_s_lo !== 4'h5) begin
            $display("[FAIL T=%0t] out_s_lo mutated to %h, expected 5", $time, out_s_lo);
            error_count = error_count + 1;
        end

        // Trigger dut.ev_bare via task with #0 non-blocking/delta delay
        #2; // T=10
        #0 dut.trigger_bare_event();
        #6; // T=16 (after posedge T=15)
        if (out_bare_count !== 1) begin
            $display("[FAIL T=%0t] out_bare_count is %0d, expected 1", $time, out_bare_count);
            error_count = error_count + 1;
        end
        if (out_s_hi !== 4'h2) begin
            $display("[FAIL T=%0t] out_s_hi is %h, expected 2", $time, out_s_hi);
            error_count = error_count + 1;
        end

        // Check active-region stratified scheduling with tb_ev
        #1; // T=17
        ->tb_ev;
        // The awakened process runs in the active region; check before delay
        if (tb_ev_count !== 0) begin
            $display("[FAIL T=%0t] tb_ev_count incremented prematurely", $time);
            error_count = error_count + 1;
        end
        #3; // T=20
        if (tb_ev_count !== 1) begin
            $display("[FAIL T=%0t] tb_ev_count is %0d, expected 1", $time, tb_ev_count);
            error_count = error_count + 1;
        end

        // Check non-blocking / #0 trigger on tb_ev
        #2; // T=22
        #0 ->tb_ev;
        #5; // T=27
        if (tb_ev_count !== 2) begin
            $display("[FAIL T=%0t] tb_ev_count after #0 is %0d, expected 2", $time, tb_ev_count);
            error_count = error_count + 1;
        end

        // Final verification summary
        #10; // T=37
        $display("\n================================================================================");
        $display("[CAS v3.92.0.0 REGRESSION SUITE: NAMED EVENTS & STRUCT MEMBER DEFAULTS]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800 §7.2.2 struct member default values");
            $display("         and §9.4.2 named events & active-region event control scheduling.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule
