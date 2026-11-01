// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.26.0.0
// Standard Target: IEEE Std 1800-2023 §6.20 & §23.10 (Parameter Overrides & Defparam)
// Testbench Name : tb_v3_26_0_0_regression
// ============================================================================

module tb_v3_26_0_0_regression;
    logic        clk;
    logic        rst_n;
    logic [15:0] din;
    logic [15:0] dout;

    int pass_count  = 0;
    int error_count = 0;

    // Instance 1: Instantiation-based parameter override by name (§23.10.2)
    // WIDTH=16, SHIFT=4
    v3_26_0_0_pipeline_stage #(
        .WIDTH(16),
        .SHIFT(4)
    ) direct_inst (
        .clk  (clk),
        .rst_n(rst_n),
        .din  (din),
        .dout (dout)
    );

    // Instance 2: Nested instance altered via defparam (§23.10.1)
    logic [15:0] nested_dout;
    v3_26_0_0_nested_pipeline nested_inst (
        .clk  (clk),
        .rst_n(rst_n),
        .din  (din),
        .dout (nested_dout)
    );

    // Hierarchical defparam overriding sub-instance parameters
    defparam nested_inst.stage_inst.WIDTH = 16;
    defparam nested_inst.stage_inst.SHIFT = 2;

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n = 0;
        din   = 16'h0000;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Stimulus din = 16'h1234
        // direct_inst (SHIFT=4, WIDTH=16):
        // (1234 << 4) | (1234 >> 12) = 2340 | 0001 = 16'h2341
        // nested_inst (SHIFT=2, WIDTH=16 via defparam):
        // (1234 << 2) | (1234 >> 14) = 48D0 | 0000 = 16'h48D0
        // --------------------------------------------------------------------
        @(negedge clk);
        din = 16'h1234;

        @(posedge clk);
        #1;
        if (dout !== 16'h2341) begin
            $display("[FAIL T=%0t] direct_inst mismatch! got %h (exp 2341)", $time, dout);
            error_count++;
        end else begin
            pass_count++;
        end

        if (nested_dout !== 16'h48D0) begin
            $display("[FAIL T=%0t] nested_inst defparam mismatch! got %h (exp 48D0)", $time, nested_dout);
            error_count++;
        end else begin
            pass_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: Stimulus din = 16'hA005
        // direct_inst (SHIFT=4):
        // (A005 << 4) | (A005 >> 12) = 0050 | 000A = 16'h005A
        // nested_inst (SHIFT=2):
        // (A005 << 2) | (A005 >> 14) = 8014 | 0002 = 16'h8016
        // --------------------------------------------------------------------
        @(negedge clk);
        din = 16'hA005;

        @(posedge clk);
        #1;
        if (dout !== 16'h005A) begin
            $display("[FAIL T=%0t] direct_inst Tx2 mismatch! got %h (exp 005A)", $time, dout);
            error_count++;
        end else begin
            pass_count++;
        end

        if (nested_dout !== 16'h8016) begin
            $display("[FAIL T=%0t] nested_inst Tx2 mismatch! got %h (exp 8016)", $time, nested_dout);
            error_count++;
        end else begin
            pass_count++;
        end

        #5;
        $display("\n================================================================================");
        $display("[CAS v3.26.0.0 REGRESSION SUITE: DEFPARAMS & PARAMETER OVERRIDES]");
        if (error_count == 0 && pass_count == 4) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §6.20 & §23.10 defparam & named parameter overrides.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 4, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | rst_n=%b din=%h | direct_dout=%h nested_dout=%h",
                 $time, rst_n, din, dout, nested_dout);
    end
endmodule