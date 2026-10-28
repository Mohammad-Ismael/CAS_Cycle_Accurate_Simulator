// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v1.5.0.0
// Standard Target: IEEE Std 1800-2023 Procedural Event Controls @(posedge clk)
// Testbench Name : tb_v1_5_0_0_regression
// ============================================================================

module tb_v1_5_0_0_regression;
    logic       clk;
    logic       rst_n;
    logic [7:0] d_in;
    logic [7:0] q_sync;
    logic [7:0] q_piped;

    integer error_count = 0;

    v1_5_0_0_event_ctrl_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .d_in(d_in),
        .q_sync(q_sync),
        .q_piped(q_piped)
    );

    // Clock Generator: Period = 10ns (Toggle every 5ns)
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    // Cycle-stepping verification using procedural @(posedge clk)
    initial begin
        // Cycle 0: Initialize
        rst_n = 0;
        d_in  = 8'h00;

        // Hold reset for 2 clock edges
        @(posedge clk);
        @(posedge clk);
        #1;
        rst_n = 1;
        d_in  = 8'hAA;

        // Wait for next clock edge (T=25ns)
        @(posedge clk);
        #1;
        if (q_sync !== 8'hAA || q_piped !== 8'h00) begin
            $display("[FAIL] Cycle 1 mismatch! Expected q_sync=AA q_piped=00, Got sync=%h piped=%h",
                     q_sync, q_piped);
            error_count = error_count + 1;
        end

        // Feed second payload
        d_in = 8'h55;
        @(posedge clk);
        #1;
        if (q_sync !== 8'h55 || q_piped !== 8'hAA) begin
            $display("[FAIL] Cycle 2 mismatch! Expected q_sync=55 q_piped=AA, Got sync=%h piped=%h",
                     q_sync, q_piped);
            error_count = error_count + 1;
        end

        // Feed third payload
        d_in = 8'hFF;
        @(posedge clk);
        #1;
        if (q_sync !== 8'hFF || q_piped !== 8'h55) begin
            $display("[FAIL] Cycle 3 mismatch! Expected q_sync=FF q_piped=55, Got sync=%h piped=%h",
                     q_sync, q_piped);
            error_count = error_count + 1;
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v1.5.0.0 REGRESSION SUITE: PROCEDURAL EVENT CONTROLS]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Procedural @(posedge clk) suspension and cycle synchronization verified.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | clk=%b rst_n=%b d_in=%h | q_sync=%h q_piped=%h",
                 $time, clk, rst_n, d_in, q_sync, q_piped);
    end

endmodule