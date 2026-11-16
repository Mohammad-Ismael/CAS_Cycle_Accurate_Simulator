// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.57.0.0
// Standard Target: IEEE Std 1800-2023 §20.14 ($sformatf and String Formatting)
// Testbench Name : tb_v3_57_0_regression
// ============================================================================

module tb_v3_57_0_regression;
    import v3_57_0_0_pkg::*;

    logic        clk;
    logic        rst_n;
    logic [7:0]  tag_in;
    logic [15:0] addr_in;
    logic [31:0] data_in;
    logic [31:0] report_length_out;
    logic        report_match_valid;

    int pass_count  = 0;
    int error_count = 0;

    v3_57_0_0_report_generator dut (
        .clk               (clk),
        .rst_n             (rst_n),
        .tag_in            (tag_in),
        .addr_in           (addr_in),
        .data_in           (data_in),
        .report_length_out (report_length_out),
        .report_match_valid(report_match_valid)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        string s1, s2;
        int val_dec;
        logic [15:0] val_hex;
        logic [7:0]  val_bin;

        rst_n   = 0;
        tag_in  = 8'h00;
        addr_in = 16'h0000;
        data_in = 32'h00000000;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Procedural evaluation of $sformatf format specifiers
        // --------------------------------------------------------------------
        val_dec = 12345;
        val_hex = 16'hABCD;
        val_bin = 8'b1010_0101;

        s1 = $sformatf("DEC:%0d HEX:%0h BIN:%0b", val_dec, val_hex, val_bin);
        if (s1 == "DEC:12345 HEX:abcd BIN:10100101") begin
            $display("[PASS T=%0t] Procedural $sformatf specifiers verified: %s", $time, s1);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Procedural $sformatf failed! got '%s'", $time, s1);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: In-place $sformat and string interpolation
        // --------------------------------------------------------------------
        $sformat(s2, "STATUS: OK (code=%0d, msg=%s)", 0, "PASSED");
        if (s2 == "STATUS: OK (code=0, msg=PASSED)") begin
            $display("[PASS T=%0t] In-place $sformat string interpolation verified: %s", $time, s2);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] In-place $sformat failed! got '%s'", $time, s2);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Sequential DUT execution and transaction report serialization
        // --------------------------------------------------------------------
        @(negedge clk);
        tag_in  = 8'hFE;
        addr_in = 16'h1000;
        data_in = 32'hDEADBEEF;

        @(posedge clk);
        #1;
        if (report_length_out === 32'd37 &&
            report_match_valid === 1'b1) begin
            $display("[PASS T=%0t] DUT transaction serializer verified: len=%0d match=%b",
                     $time, report_length_out, report_match_valid);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] DUT transaction serializer failed! len=%0d match=%b",
                     $time, report_length_out, report_match_valid);
            error_count++;
        end

        @(negedge clk);

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.57.0.0 REGRESSION SUITE: STRING FORMATTING $sformatf §20.14]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §20.14 $sformatf and in-memory formatting.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule