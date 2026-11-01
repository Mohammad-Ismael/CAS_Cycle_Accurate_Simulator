// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.27.0.0
// Standard Target: IEEE Std 1800-2023 §10.6 (Net Aliasing & Bidirectional Nets)
// Testbench Name : tb_v3_27_0_0_regression
// ============================================================================

module tb_v3_27_0_0_regression;
    logic       clk;
    logic       rst_n;
    logic       drive_n_en;
    logic [7:0] data_n;
    logic       drive_s_en;
    logic [7:0] data_s;
    logic [7:0] captured_tap;

    int pass_count  = 0;
    int error_count = 0;

    v3_27_0_0_bidir_core dut (
        .clk           (clk),
        .rst_n         (rst_n),
        .drive_north_en(drive_n_en),
        .data_north    (data_n),
        .drive_south_en(drive_s_en),
        .data_south    (data_s),
        .captured_tap  (captured_tap)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n      = 0;
        drive_n_en = 0;
        data_n     = 8'h00;
        drive_s_en = 0;
        data_s     = 8'h00;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: North drives 8'hA5, South disabled (high-z).
        // Alias ensures tap_monitor and port_south both reflect 8'hA5.
        // --------------------------------------------------------------------
        @(negedge clk);
        drive_n_en = 1;
        data_n     = 8'hA5;
        drive_s_en = 0;
        data_s     = 8'h00;

        @(posedge clk);
        #1;
        if (captured_tap !== 8'hA5) begin
            $display("[FAIL T=%0t] Test 1 North->South alias mismatch! got %h (exp A5)", $time, captured_tap);
            error_count++;
        end else begin
            pass_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: South drives 8'h3C, North disabled (high-z).
        // Bidirectional alias must propagate south driver to tap and north net.
        // --------------------------------------------------------------------
        @(negedge clk);
        drive_n_en = 0;
        data_n     = 8'h00;
        drive_s_en = 1;
        data_s     = 8'h3C;

        @(posedge clk);
        #1;
        if (captured_tap !== 8'h3C) begin
            $display("[FAIL T=%0t] Test 2 South->North alias mismatch! got %h (exp 3C)", $time, captured_tap);
            error_count++;
        end else begin
            pass_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Contention test - North drives 8'hF0, South drives 8'h0F.
        // Nets resolve to 8'hXX on conflicting bits per 4-state wired net model.
        // --------------------------------------------------------------------
        @(negedge clk);
        drive_n_en = 1;
        data_n     = 8'hF0;
        drive_s_en = 1;
        data_s     = 8'h0F;

        @(posedge clk);
        #1;
        if (captured_tap !== 8'hxx) begin
            $display("[FAIL T=%0t] Test 3 Contention mismatch! got %h (exp xx)", $time, captured_tap);
            error_count++;
        end else begin
            pass_count++;
        end

        #5;
        $display("\n================================================================================");
        $display("[CAS v3.27.0.0 REGRESSION SUITE: NET ALIASING]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §10.6 net aliasing, bidirectional propagation, and contention.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | rst_n=%b | dn_en=%b dn=%h | ds_en=%b ds=%h | tap=%h",
                 $time, rst_n, drive_n_en, data_n, drive_s_en, data_s, captured_tap);
    end
endmodule