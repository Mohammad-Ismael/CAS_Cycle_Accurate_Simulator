// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.21.0.0
// Standard Target: IEEE Std 1800-2023 §16.10 (SVA Sequence Local Variables)
// Testbench Name : tb_v3_21_0_0_regression
// ============================================================================

module tb_v3_21_0_0_regression;
    logic       clk;
    logic       rst_n;
    logic       req_in;
    logic [7:0] data_in;
    logic       ack_out;
    logic [7:0] data_out;

    int pass_count  = 0;
    int error_count = 0;

    v3_21_0_0_pipe_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .req_in(req_in),
        .data_in(data_in),
        .ack_out(ack_out),
        .data_out(data_out)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    // ------------------------------------------------------------------------
    // IEEE 1800-2023 §16.10 SVA Property with Local Variable & Implication (|->)
    // Captures data_in on req_in, checks pipeline output matches after 3 cycles
    // (Preponed value sampling across 3 registered stages)
    // ------------------------------------------------------------------------
    property p_pipe_integrity;
        logic [7:0] captured_val;
        @(posedge clk) disable iff (!rst_n)
        (req_in, captured_val = data_in) |-> ##3 (ack_out && (data_out == captured_val + 8'h02));
    endproperty

    assert_pipe: assert property (p_pipe_integrity) begin
        pass_count++;
    end else begin
        $display("[FAIL T=%0t] Assertion assert_pipe violated!", $time);
        error_count++;
    end

    initial begin
        rst_n   = 0;
        req_in  = 0;
        data_in = 8'h00;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Transaction 1: req with data = 8'h10 at T = 20
        // Sampled at posedge T=25 -> Output observed at posedge T=55
        // --------------------------------------------------------------------
        @(negedge clk);
        req_in  = 1'b1;
        data_in = 8'h10;

        // --------------------------------------------------------------------
        // Back-to-back Transaction 2: req with data = 8'h20 at T = 30
        // Sampled at posedge T=35 -> Output observed at posedge T=65
        // --------------------------------------------------------------------
        @(negedge clk);
        req_in  = 1'b1;
        data_in = 8'h20;

        @(negedge clk);
        req_in  = 1'b0;
        data_in = 8'h00;

        // Wait for pipelines to drain and assertions to finish
        repeat (6) @(posedge clk);
        #1;

        $display("\n================================================================================");
        $display("[CAS v3.21.0.0 REGRESSION SUITE: SVA SEQUENCE LOCAL VARIABLES]");
        if (error_count == 0 && pass_count == 2) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §16.10 sequence local variables and multi-thread isolation.");
        end else begin
            $display("RESULT : >>> FAIL <<< (Passes = %0d [exp 2], Errors = %0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | req=%b data_in=%h | ack=%b data_out=%h | passes=%0d",
                 $time, req_in, data_in, ack_out, data_out, pass_count);
    end
endmodule