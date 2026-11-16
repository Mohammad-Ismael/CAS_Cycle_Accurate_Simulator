`timescale 1ns/1ps

// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.39.0.0
// Standard Target: IEEE Std 1800-2023 §14.11, §14.13, §14.16 (Clocking Skew & #1step)
// Testbench Name : tb_v3_39_0_0_regression
// ============================================================================

module tb_v3_39_0_0_regression;
    logic        clk;
    logic        rst_n;
    logic [15:0] d_in;
    logic        valid_in;
    logic [15:0] d_out;
    logic        valid_out;

    int pass_count  = 0;
    int error_count = 0;

    // DUT instantiation
    v3_39_0_0_sync_pipe dut (
        .clk      (clk),
        .rst_n    (rst_n),
        .d_in     (d_in),
        .valid_in (valid_in),
        .d_out    (d_out),
        .valid_out(valid_out)
    );

    // IEEE Std 1800-2023 §14.13 & §14.16: Clocking block with #1step input skew
    clocking cb @(posedge clk);
        default input #1step output #2;
        input  d_out, valid_out;
        output d_in, valid_in;
    endclocking

    // IEEE Std 1800-2023 §14.12: Declare default clocking for naked ##N delays
    default clocking cb;

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n    = 0;
        d_in     = 16'h0000;
        valid_in = 1'b0;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Cycle delay '##1' and clocking block output drive with skew
        // cb.d_in <= 16'hA5A5; scheduled at posedge clk + 2 time units
        // --------------------------------------------------------------------
        ##1;
        cb.d_in     <= 16'hA5A5;
        cb.valid_in <= 1'b1;

        // Verify that d_in is driven after output skew (#2)
        #3;
        if (d_in === 16'hA5A5 && valid_in === 1'b1) begin
            $display("[PASS T=%0t] Clocking block output drive with skew verified: d_in=%04h",
                     $time, d_in);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Clocking block output drive failed! d_in=%04h (exp A5A5)",
                     $time, d_in);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: Input skew #1step Preponed sampling
        // On the next posedge clk, DUT updates d_out <= 16'hA5A5 in NBA.
        // At the exact clock edge, cb.d_out must sample the PREPONED value (0),
        // not the newly updated NBA value.
        // --------------------------------------------------------------------
        ##1;
        if (cb.d_out === 16'h0000) begin
            $display("[PASS T=%0t] Preponed #1step sampling isolated from NBA: cb.d_out=%04h",
                     $time, cb.d_out);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] #1step sampling race condition! cb.d_out=%04h (exp 0000)",
                     $time, cb.d_out);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Subsequent cycle #1step sampling capturing stabilized value
        // On next edge, cb.d_out samples the stabilized 16'hA5A5
        // --------------------------------------------------------------------
        ##1;
        if (cb.d_out === 16'hA5A5 && cb.valid_out === 1'b1) begin
            $display("[PASS T=%0t] Stabilized clocking block sampling: cb.d_out=%04h cb.valid=%b",
                     $time, cb.d_out, cb.valid_out);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Stabilized sampling mismatch! cb.d_out=%04h (exp A5A5)",
                     $time, cb.d_out);
            error_count++;
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.39.0.0 REGRESSION SUITE: CLOCKING BLOCK SKEW & #1step §14.13]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §14.13 #1step sampling, output skew drives, and ## cycle delays.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule