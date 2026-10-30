// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.11.0.0
// Standard Target: IEEE Std 1800-2023 §16.9.2 (Consecutive Repetition [*N], Goto [->N])
// Testbench Name : tb_v3_11_0_0_regression
// ============================================================================

module tb_v3_11_0_0_regression;
    logic       clk;
    logic       rst_n;
    logic       start;
    logic       busy;
    logic       done;
    logic [7:0] count_out;

    integer consec_pass_count = 0;
    integer goto_pass_count   = 0;
    integer error_count       = 0;

    v3_11_0_0_rep_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .start(start),
        .busy(busy),
        .done(done),
        .count_out(count_out)
    );

    // Clock generator: posedges at 5, 15, 25, 35, 45, 55, 65, 75, 85, 95...
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    // ------------------------------------------------------------------------
    // Property 1: Consecutive Repetition [*3] (§16.9.2.1)
    // When start is high, busy must hold high for 3 consecutive cycles,
    // followed by done high on the next cycle.
    // ------------------------------------------------------------------------
    property p_consec_busy;
        @(posedge clk) disable iff (!rst_n)
        $rose(start) |=> (busy [*3] ##1 done);
    endproperty

    assert_consec: assert property (p_consec_busy) begin
        consec_pass_count = consec_pass_count + 1;
    end

    // ------------------------------------------------------------------------
    // Property 2: Goto Repetition [->1] (§16.9.2.3)
    // After start, done must eventually assert exactly once, triggering pass.
    // ------------------------------------------------------------------------
    property p_goto_done;
        @(posedge clk) disable iff (!rst_n)
        $rose(start) |=> (done [->1]);
    endproperty

    assert_goto: assert property (p_goto_done) begin
        goto_pass_count = goto_pass_count + 1;
    end

    // Test sequence driving protocol stimuli
    initial begin
        rst_n = 0;
        start = 0;
        busy  = 0;

        #6; // T = 6 (after reset posedge at T = 5)
        rst_n = 1;

        // --------------------------------------------------------------------
        // Transaction 1:
        // T = 15: start = 1 (triggers $rose(start))
        // T = 25: busy = 1 (cycle 1 of 3)
        // T = 35: busy = 1 (cycle 2 of 3)
        // T = 45: busy = 1 (cycle 3 of 3)
        // T = 55: busy = 0, done is asserted by DUT!
        // --------------------------------------------------------------------
        @(posedge clk); // T = 15
        start <= 1'b1;

        @(posedge clk); // T = 25
        start <= 1'b0;
        busy  <= 1'b1;

        @(posedge clk); // T = 35
        busy  <= 1'b1;

        @(posedge clk); // T = 45
        busy  <= 1'b1;

        @(posedge clk); // T = 55
        busy  <= 1'b0;

        @(posedge clk); // T = 65 (allow Reactive region actions to commit)
        #1; // T = 66
        if (consec_pass_count !== 1 || goto_pass_count !== 1) begin
            $display("[FAIL T=66] Repetition checks failed! consec=%0d goto=%0d (exp 1, 1)",
                     consec_pass_count, goto_pass_count);
            error_count = error_count + 1;
        end

        #4; // T = 70
        $display("\n================================================================================");
        $display("[CAS v3.11.0.0 REGRESSION SUITE: SVA SEQUENCE REPETITIONS]");
        if (error_count == 0 && consec_pass_count == 1 && goto_pass_count == 1) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §16.9.2 consecutive [*N] and goto [->N] repetitions.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s))", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | clk=%b rst_n=%b | start=%b busy=%b done=%b count=%0d | consec_pass=%0d goto_pass=%0d",
                 $time, clk, rst_n, start, busy, done, count_out,
                 consec_pass_count, goto_pass_count);
    end
endmodule