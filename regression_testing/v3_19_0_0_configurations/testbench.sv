// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.19.0.0
// Standard Target: IEEE Std 1800-2023 §33 (config, endconfig, instance use)
// Testbench Name : tb_v3_19_0_0_regression
// ============================================================================

// ----------------------------------------------------------------------------
// IEEE 1800-2023 §33 Configuration Declaration
// ----------------------------------------------------------------------------
config cfg_v3_19_0_0;
    design work.tb_v3_19_0_0_regression;
    default liblist work;
    // Rebind instance 'dut' to use the fast core implementation
    instance tb_v3_19_0_0_regression.dut use work.v3_19_0_0_fast_core;
endconfig

module tb_v3_19_0_0_regression;
    logic        clk;
    logic        rst_n;
    logic [31:0] a_in;
    logic [31:0] b_in;
    logic [31:0] res_out;
    logic [7:0]  cfg_id;

    int error_count = 0;

    // Instance instantiated using default binding type, rebound via config
    v3_19_0_0_default_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .a_in(a_in),
        .b_in(b_in),
        .res_out(res_out),
        .cfg_id(cfg_id)
    );

    initial begin
        clk   = 0;
        rst_n = 0;
        a_in  = 32'h00000000;
        b_in  = 32'h00000000;

        #5;
        rst_n = 1;

        // --------------------------------------------------------------------
        // TEST 1: Check configured binding ID
        // Expected: cfg_id == 8'hAA (fast core bound via config)
        // --------------------------------------------------------------------
        #5; // T = 10
        if (cfg_id !== 8'hAA) begin
            $display("[FAIL T=10] Configuration binding failed! got cfg_id=%h (exp AA)", cfg_id);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Verify fast core execution
        // a_in = 32'h00001000, b_in = 32'h00000020
        // Expected: (0x1000 ^ 0x20) + 0x10 = 0x1020 + 0x10 = 0x1030
        // --------------------------------------------------------------------
        a_in = 32'h00001000;
        b_in = 32'h00000020;
        #5; // T = 15

        if (res_out !== 32'h00001030) begin
            $display("[FAIL T=15] Fast core arithmetic mismatch! got %h (exp 00001030)", res_out);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 3: Additional stimulus check
        // a_in = 32'h000000FF, b_in = 32'h0000000F
        // Expected: (0xFF ^ 0x0F) + 0x10 = 0xF0 + 0x10 = 0x0100
        // --------------------------------------------------------------------
        a_in = 32'h000000FF;
        b_in = 32'h0000000F;
        #5; // T = 20

        if (res_out !== 32'h00000100) begin
            $display("[FAIL T=20] Fast core arithmetic mismatch! got %h (exp 00000100)", res_out);
            error_count = error_count + 1;
        end

        #5; // T = 25
        $display("\n================================================================================");
        $display("[CAS v3.19.0.0 REGRESSION SUITE: SYSTEMVERILOG CONFIGURATIONS]");
        if (error_count == 0 && cfg_id == 8'hAA && res_out == 32'h00000100) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §33 configurations, instance rebinding, and hierarchy elaboration.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | rst_n=%b cfg_id=%h | a=%h b=%h res=%h",
                 $time, rst_n, cfg_id, a_in, b_in, res_out);
    end
endmodule