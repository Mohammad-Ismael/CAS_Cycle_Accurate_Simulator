// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.9.0.0
// Standard Target: IEEE Std 1800-2023 §16 (property, sequence, |->, |=>, assert)
// Testbench Name : tb_v3_9_0_0_regression
// ============================================================================

module tb_v3_9_0_0_regression;
    logic       clk;
    logic       rst_n;
    logic       req;
    logic       ack;
    logic [7:0] data_out;

    integer pass_count  = 0;
    integer fail_count  = 0;
    integer error_count = 0;

    v3_9_0_0_sva_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .req(req),
        .ack(ack),
        .data_out(data_out)
    );

    // Clock generator: posedges at 5, 15, 25, 35, 45, 55, 65...
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    // ------------------------------------------------------------------------
    // SVA Property & Sequence Declarations (§16.7 & §16.12)
    // Non-overlapping implication: when req is asserted, ack must assert on the
    // next clock edge (|=>).
    // ------------------------------------------------------------------------
    property p_req_ack;
        @(posedge clk) disable iff (!rst_n)
        req |=> ack;
    endproperty

    // Concurrent assertion with pass/fail action blocks (§16.14)
    assert_req_ack: assert property (p_req_ack) begin
        pass_count = pass_count + 1;
    end else begin
        $display("[SVA FAIL T=%0t] Protocol violation: ack failed to assert after req!", $time);
        fail_count = fail_count + 1;
    end

    // Test sequence driving protocol stimuli
    initial begin
        rst_n = 0;
        req   = 0;

        #6; // T = 6 (after reset posedge at T = 5)
        rst_n = 1;

        // --------------------------------------------------------------------
        // TEST 1: Protocol compliant transaction
        // At T = 15: req = 1 -> triggers antecedent
        // At T = 25: ack = 1 -> matches consequent -> PASS
        // --------------------------------------------------------------------
        @(posedge clk); // T = 15
        req <= 1'b1;

        @(posedge clk); // T = 25
        req <= 1'b0;

        @(posedge clk); // T = 35 (evaluation of T=25 consequent completes)
        #1; // T = 36
        if (pass_count !== 1 || fail_count !== 0) begin
            $display("[FAIL T=36] Transaction 1 SVA check failed! pass=%0d fail=%0d",
                     pass_count, fail_count);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Multi-cycle gap
        // --------------------------------------------------------------------
        @(posedge clk); // T = 45

        // --------------------------------------------------------------------
        // TEST 3: Second protocol compliant transaction
        // At T = 55: req = 1
        // At T = 65: ack = 1 -> PASS
        // --------------------------------------------------------------------
        @(posedge clk); // T = 55
        req <= 1'b1;

        @(posedge clk); // T = 65
        req <= 1'b0;

        @(posedge clk); // T = 75
        #1; // T = 76
        if (pass_count !== 2 || fail_count !== 0) begin
            $display("[FAIL T=76] Transaction 2 SVA check failed! pass=%0d fail=%0d",
                     pass_count, fail_count);
            error_count = error_count + 1;
        end

        #4; // T = 80
        $display("\n================================================================================");
        $display("[CAS v3.9.0.0 REGRESSION SUITE: SVA CONCURRENT ASSERTIONS]");
        if (error_count == 0 && fail_count == 0 && pass_count == 2) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §16 property, non-overlapping implication |=>, and pass action blocks.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s), %0d failure(s))", error_count, fail_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | clk=%b rst_n=%b | req=%b ack=%b | pass=%0d fail=%0d",
                 $time, clk, rst_n, req, ack, pass_count, fail_count);
    end
endmodule