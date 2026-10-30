// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.22.0.0
// Standard Target: IEEE Std 1800-2023 §16.11 (SVA Subroutine Calls on Match)
// Testbench Name : tb_v3_22_0_0_regression
// ============================================================================

module tb_v3_22_0_0_regression;
    logic        clk;
    logic        rst_n;
    logic        req;
    logic [15:0] tx_data;
    logic        ack;
    logic [15:0] rx_data;

    int callback_hits = 0;
    logic [15:0] last_logged_tx = 0;
    logic [15:0] last_logged_rx = 0;
    int pass_count    = 0;
    int error_count   = 0;

    v3_22_0_0_handshake_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .req(req),
        .tx_data(tx_data),
        .ack(ack),
        .rx_data(rx_data)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    // ------------------------------------------------------------------------
    // User subroutine invoked directly on sequence match (§16.11)
    // ------------------------------------------------------------------------
    function void log_transaction(input logic [15:0] captured_tx, input logic [15:0] captured_rx);
        callback_hits++;
        last_logged_tx = captured_tx;
        last_logged_rx = captured_rx;
    endfunction

    // ------------------------------------------------------------------------
    // SVA Property with local variable and match subroutine action (§16.11)
    // ------------------------------------------------------------------------
    property p_handshake_match;
        logic [15:0] in_val;
        @(posedge clk) disable iff (!rst_n)
        (req, in_val = tx_data) |-> 
            ##1 (ack && (rx_data == (in_val ^ 16'h5A5A)), log_transaction(in_val, rx_data));
    endproperty

    assert_hs: assert property (p_handshake_match) begin
        pass_count++;
    end else begin
        $display("[FAIL T=%0t] Assertion assert_hs failed!", $time);
        error_count++;
    end

    initial begin
        rst_n   = 0;
        req     = 0;
        tx_data = 16'h0000;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Transaction 1: req with tx_data = 16'h1234 at posedge T=25
        // Expect match & subroutine call at posedge T=35
        // rx_data = 16'h1234 ^ 16'h5A5A = 16'h486E
        // --------------------------------------------------------------------
        @(negedge clk);
        req     = 1'b1;
        tx_data = 16'h1234;

        @(negedge clk);
        req     = 1'b0;
        tx_data = 16'h0000;

        // --------------------------------------------------------------------
        // Transaction 2: req with tx_data = 16'hABCD at posedge T=45
        // Expect match & subroutine call at posedge T=55
        // rx_data = 16'hABCD ^ 16'h5A5A = 16'hF197
        // --------------------------------------------------------------------
        @(negedge clk);
        req     = 1'b1;
        tx_data = 16'hABCD;

        @(negedge clk);
        req     = 1'b0;
        tx_data = 16'h0000;

        repeat (4) @(posedge clk);
        #1;

        $display("\n================================================================================");
        $display("[CAS v3.22.0.0 REGRESSION SUITE: SVA MATCH SUBROUTINE CALLS]");
        if (error_count == 0 && pass_count == 2 && callback_hits == 2 && last_logged_rx == 16'hF197) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §16.11 sequence match subroutine calls & argument passing.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 2, hits=%0d exp 2, rx=%h exp F197)",
                     pass_count, callback_hits, last_logged_rx);
            error_count++;
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | req=%b ack=%b | tx=%h rx=%h | hits=%0d passes=%0d",
                 $time, req, ack, tx_data, rx_data, callback_hits, pass_count);
    end
endmodule