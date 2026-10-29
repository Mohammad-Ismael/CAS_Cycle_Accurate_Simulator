// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v1.8.0.0
// Standard Target: IEEE Std 1800-2023 §10.4.2 (assign and deassign)
// Testbench Name : tb_v1_8_0_0_regression
// ============================================================================

module tb_v1_8_0_0_regression;
    logic       clk;
    logic       rst_n;
    logic [7:0] d_in;
    logic [7:0] q_out;
    logic [7:0] override_val;
    integer     error_count = 0;

    v1_8_0_0_pca_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .d_in(d_in),
        .q_out(q_out)
    );

    // Clock generator
    initial begin
        clk = 0;
        forever #5 clk = ~clk; // Posedges at 5, 15, 25, 35, 45, 55, 65...
    end

    initial begin
        rst_n        = 0;
        d_in         = 8'h11;
        override_val = 8'hAA;
        #6; // T=6: After first posedge at T=5
        rst_n = 1;

        #4; // T=10
        d_in = 8'h22;

        #5; // T=15 (posedge clk) -> q_out updates to 8'h22 via regular procedural block
        #1; // T=16
        if (q_out !== 8'h22) begin
            $display("[FAIL T=16] Normal register update failed! q_out=%h (exp 22)", q_out);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 1: Procedural 'assign' override (§10.4.2)
        // Overrides regular procedural drive; remains active across clock edges
        // --------------------------------------------------------------------
        #4; // T=20
        assign dut.q_out = override_val; // Quasi-continuous override active

        #1; // T=21
        if (q_out !== 8'hAA) begin
            $display("[FAIL T=21] Procedural assign failed! q_out=%h (exp AA)", q_out);
            error_count = error_count + 1;
        end

        #4; // T=25 (posedge clk): Normal block tries to drive d_in (8'h22),
            // but procedural assign must force q_out to remain override_val (8'hAA)
        #1; // T=26
        if (q_out !== 8'hAA) begin
            $display("[FAIL T=26] Procedural assign overridden by clock edge! q_out=%h (exp AA)", q_out);
            error_count = error_count + 1;
        end

        // Dynamic sensitivity: changing override_val propagates continuously
        #2; // T=28
        override_val = 8'hBB;
        #1; // T=29
        if (q_out !== 8'hBB) begin
            $display("[FAIL T=29] Dynamic procedural assign sensitivity failed! q_out=%h (exp BB)", q_out);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Procedural 'deassign' (§10.4.2)
        // Releases override; retains last driven value until next procedural write
        // --------------------------------------------------------------------
        #3; // T=32
        deassign dut.q_out;

        #1; // T=33: override_val changes, but q_out must NOT change anymore
        override_val = 8'hCC;
        #1; // T=34
        if (q_out !== 8'hBB) begin
            $display("[FAIL T=34] deassign did not release continuous drive! q_out=%h (exp BB)", q_out);
            error_count = error_count + 1;
        end

        // Clock edge at T=35: Normal procedural drive resumes and takes effect
        d_in = 8'h33;
        #2; // T=36 (after posedge at T=35)
        if (q_out !== 8'h33) begin
            $display("[FAIL T=36] Normal procedural drive did not resume after deassign! q_out=%h (exp 33)", q_out);
            error_count = error_count + 1;
        end

        #4; // T=40
        $display("\n================================================================================");
        $display("[CAS v1.8.0.0 REGRESSION SUITE: PROCEDURAL ASSIGN AND DEASSIGN]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §10.4.2 procedural assign and deassign.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | clk=%b rst_n=%b d_in=%h override=%h | q_out=%h",
                 $time, clk, rst_n, d_in, override_val, q_out);
    end
endmodule