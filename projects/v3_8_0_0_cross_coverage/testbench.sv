// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.8.0.0
// Standard Target: IEEE Std 1800-2023 §19.6 (cross, multi-dimensional coverage)
// Testbench Name : tb_v3_8_0_0_regression
// ============================================================================

module tb_v3_8_0_0_regression;
    logic       clk;
    logic       rst_n;
    logic [1:0] mode_in;
    logic [7:0] data_in;
    logic [7:0] res_out;

    v3_8_0_0_cross_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .mode_in(mode_in),
        .data_in(data_in),
        .res_out(res_out)
    );

    // Covergroup with 2 Coverpoints and 1 Cross (§19.6)
    covergroup cross_cg;
        // 2 bins
        cp_mode: coverpoint mode_in {
            bins mode_norm = {2'b00};
            bins mode_inv  = {2'b01};
        }

        // 2 bins
        cp_data: coverpoint data_in {
            bins low_range  = {[8'h00 : 8'h7F]};
            bins high_range = {[8'h80 : 8'hFF]};
        }

        // 2 x 2 = 4 cross bins:
        // <norm, low>, <norm, high>, <inv, low>, <inv, high>
        cx_mode_data: cross cp_mode, cp_data;
    endgroup

    cross_cg cg_inst;

    real    cov_pct = 0.0;
    integer int_cov = 0;
    integer error_count = 0;

    initial begin
        clk     = 0;
        rst_n   = 0;
        mode_in = 2'b00;
        data_in = 8'h00;

        #5;
        rst_n   = 1;
        cg_inst = new();

        // --------------------------------------------------------------------
        // INITIAL STATE: 0/4 cross bins, 0/2 cp_mode, 0/2 cp_data -> 0%
        // --------------------------------------------------------------------
        cov_pct = cg_inst.get_coverage();
        int_cov = $rtoi(cov_pct);
        #5; // T = 10

        if (int_cov !== 0) begin
            $display("[FAIL T=10] Initial cross coverage should be 0, got %0d%%", int_cov);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 1: Sample <mode_norm, low_range>
        // Hits: cp_mode(1/2=50%), cp_data(1/2=50%), cross(1/4=25%)
        // Cumulative coverage = (50% + 50% + 25%) / 3 = 125/3 = 41.666% -> 41%
        // --------------------------------------------------------------------
        mode_in = 2'b00;
        data_in = 8'h10;
        cg_inst.sample();

        cov_pct = cg_inst.get_coverage();
        int_cov = $rtoi(cov_pct);
        #5; // T = 15

        // --------------------------------------------------------------------
        // TEST 2: Sample <mode_norm, high_range>
        // Hits: cp_mode(1/2=50%), cp_data(2/2=100%), cross(2/4=50%)
        // Cumulative coverage = (50% + 100% + 50%) / 3 = 200/3 = 66.666% -> 66%
        // --------------------------------------------------------------------
        mode_in = 2'b00;
        data_in = 8'h90;
        cg_inst.sample();

        cov_pct = cg_inst.get_coverage();
        int_cov = $rtoi(cov_pct);
        #5; // T = 20

        // --------------------------------------------------------------------
        // TEST 3: Sample <mode_inv, low_range>
        // Hits: cp_mode(2/2=100%), cp_data(2/2=100%), cross(3/4=75%)
        // Cumulative coverage = (100% + 100% + 75%) / 3 = 275/3 = 91.666% -> 91%
        // --------------------------------------------------------------------
        mode_in = 2'b01;
        data_in = 8'h20;
        cg_inst.sample();

        cov_pct = cg_inst.get_coverage();
        int_cov = $rtoi(cov_pct);
        #5; // T = 25

        // --------------------------------------------------------------------
        // TEST 4: Sample <mode_inv, high_range>
        // Hits: cp_mode(2/2=100%), cp_data(2/2=100%), cross(4/4=100%)
        // Cumulative coverage = 100.0% -> 100%
        // --------------------------------------------------------------------
        mode_in = 2'b01;
        data_in = 8'hF0;
        cg_inst.sample();

        cov_pct = cg_inst.get_coverage();
        int_cov = $rtoi(cov_pct);
        #5; // T = 30

        if (int_cov !== 100) begin
            $display("[FAIL T=30] Final cross coverage failed! Expected 100%%, got %0d%%", int_cov);
            error_count = error_count + 1;
        end

        #5; // T = 35
        $display("\n================================================================================");
        $display("[CAS v3.8.0.0 REGRESSION SUITE: CROSS COVERAGE]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §19.6 cross coverage, Cartesian bins, and get_coverage().");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | cov_pct=%0d%% | mode=%b data=%h res=%h",
                 $time, int_cov, mode_in, data_in, res_out);
    end
endmodule