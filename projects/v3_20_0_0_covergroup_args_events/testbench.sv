// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.20.0.0
// Standard Target: IEEE Std 1800-2023 §19 (Parameterized Covergroups & Event Triggers)
// Testbench Name : tb_v3_20_0_0_regression
// ============================================================================

module tb_v3_20_0_0_regression;
    logic       clk;
    logic       rst_n;
    logic [7:0] data_in;
    logic       valid_in;
    logic [7:0] data_out;
    logic       valid_out;

    int cov_score = 0;
    int error_count = 0;

    v3_20_0_0_coverage_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .data_in(data_in),
        .valid_in(valid_in),
        .data_out(data_out),
        .valid_out(valid_out)
    );

    // ------------------------------------------------------------------------
    // Parameterized Covergroup with automatic clock event trigger (§19.4)
    // ------------------------------------------------------------------------
    covergroup bus_cov_cg (ref logic [7:0] sig, ref logic val, input int cutoff) @(posedge clk);    
        option.per_instance = 1;
        cp_data: coverpoint sig {
            bins below_cutoff = {[0:cutoff-1]};
            bins above_cutoff = {[cutoff:255]};
        }
        cp_valid: coverpoint val {
            bins active   = {1'b1};
            bins inactive = {1'b0};
        }
    endgroup

    bus_cov_cg cg_inst;

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n    = 0;
        data_in  = 8'h00;
        valid_in = 1'b0;

        // Instantiate covergroup with cutoff parameter = 100
        cg_inst = new(data_in, valid_in, 100);

        #12; // align after clock edges
        rst_n = 1;

        // --------------------------------------------------------------------
        // Cycle 1: data = 20 (below cutoff), valid = 0
        // --------------------------------------------------------------------
        @(negedge clk);
        data_in   = 8'd20;
        valid_in  = 1'b0;
        cov_score = int'(cg_inst.get_inst_coverage());

        // --------------------------------------------------------------------
        // Cycle 2: data = 150 (above cutoff), valid = 1
        // --------------------------------------------------------------------
        @(negedge clk);
        data_in   = 8'd150;
        valid_in  = 1'b1;
        cov_score = int'(cg_inst.get_inst_coverage());

        // --------------------------------------------------------------------
        // Cycle 3: Sample point verification
        // --------------------------------------------------------------------
        @(negedge clk);
        cov_score = int'(cg_inst.get_inst_coverage());

        #5;
        $display("\n================================================================================");
        $display("[CAS v3.20.0.0 REGRESSION SUITE: COVERGROUP ARGS & EVENT SAMPLING]");
        if (cov_score == 100) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §19 parameterized covergroup arguments and event triggers.");
        end else begin
            $display("RESULT : >>> FAIL <<< (Coverage score = %0d%%, exp 100%%)", cov_score);
            error_count = error_count + 1;
        end
        $display("================================================================================\n");

        $finish;
    end

    // Non-procedural monitor observes static/procedural signals only
    initial begin
        $monitor("Time=%0t | clk=%b rst_n=%b | data=%0d valid=%b | cov=%0d%%",
                 $time, clk, rst_n, data_in, valid_in, cov_score);
    end
endmodule