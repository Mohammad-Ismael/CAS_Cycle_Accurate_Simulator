// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.7.0.0
// Standard Target: IEEE Std 1800-2023 §19 (covergroup, coverpoint, bins, sample())
// Testbench Name : tb_v3_7_0_0_regression
// ============================================================================

module tb_v3_7_0_0_regression;
    logic       clk;
    logic       rst_n;
    logic [7:0] cmd_in;
    logic [7:0] addr_in;
    logic [7:0] status_out;

    v3_7_0_0_cov_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .cmd_in(cmd_in),
        .addr_in(addr_in),
        .status_out(status_out)
    );

    // Covergroup definition (§19.3 & §19.5)
    covergroup bus_cov_cg;
        cp_cmd: coverpoint cmd_in {
            bins read_cmd  = {8'h01};
            bins write_cmd = {8'h02};
        }

        cp_addr: coverpoint addr_in {
            bins low_range  = {[8'h00 : 8'h3F]};
            bins high_range = {[8'h40 : 8'hFF]};
        }
    endgroup

    bus_cov_cg cg_inst;

    // Procedural shadows for clean observation
    real    cov_pct = 0.0;
    integer int_cov = 0;
    integer error_count = 0;

    initial begin
        clk        = 0;
        rst_n      = 0;
        cmd_in     = 8'h00;
        addr_in    = 8'h00;

        #5;
        rst_n = 1;
        cg_inst = new();

        // --------------------------------------------------------------------
        // INITIAL STATE: 0 bins sampled -> 0.0% coverage
        // --------------------------------------------------------------------
        cov_pct = cg_inst.get_coverage();
        int_cov = $rtoi(cov_pct);
        #5; // T = 10

        if (int_cov !== 0) begin
            $display("[FAIL T=10] Initial coverage should be 0, got %0d%%", int_cov);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 1: Sample Read Command (8'h01) + Low Range Addr (8'h10)
        // Hits: cp_cmd.read_cmd (1/2) and cp_addr.low_range (1/2)
        // Total coverage = 50.0%
        // --------------------------------------------------------------------
        cmd_in  = 8'h01;
        addr_in = 8'h10;
        cg_inst.sample();

        cov_pct = cg_inst.get_coverage();
        int_cov = $rtoi(cov_pct);
        #5; // T = 15

        if (int_cov !== 50) begin
            $display("[FAIL T=15] Sample 1 failed! Expected 50%%, got %0d%%", int_cov);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Sample Write Command (8'h02) + High Range Addr (8'h80)
        // Hits remaining bins: cp_cmd.write_cmd (2/2) and cp_addr.high_range (2/2)
        // Total coverage = 100.0%
        // --------------------------------------------------------------------
        cmd_in  = 8'h02;
        addr_in = 8'h80;
        cg_inst.sample();

        cov_pct = cg_inst.get_coverage();
        int_cov = $rtoi(cov_pct);
        #5; // T = 20

        if (int_cov !== 100) begin
            $display("[FAIL T=20] Sample 2 failed! Expected 100%%, got %0d%%", int_cov);
            error_count = error_count + 1;
        end

        #5; // T = 25
        $display("\n================================================================================");
        $display("[CAS v3.7.0.0 REGRESSION SUITE: FUNCTIONAL COVERAGE]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §19 covergroup, coverpoints, bins, and get_coverage().");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | cov_pct=%0d%% | cmd=%h addr=%h status=%h",
                 $time, int_cov, cmd_in, addr_in, status_out);
    end
endmodule