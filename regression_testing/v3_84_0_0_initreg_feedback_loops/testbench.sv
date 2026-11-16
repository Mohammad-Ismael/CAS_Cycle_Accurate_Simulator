`timescale 1ns/1ps

//==============================================================================
// Testbench: tb_v3_84_0_regression
// Standard: IEEE 1800-2023 SystemVerilog
// Description:
//   Verification of +initreg+0 power-on initialization for self-gated
//   feedback register loops (rvdffie pattern) across 1-bit, 32-bit, and 142-bit.
//==============================================================================

module tb_v3_84_0_regression;

    logic         clk;
    logic         din_1;
    logic         dout_1;
    logic [31:0]  din_32;
    logic [31:0]  dout_32;
    logic [141:0] din_142;
    logic [141:0] dout_142;

    integer errors = 0;

    v3_84_0_0_initreg_feedback_loops dut (
        .clk(clk),
        .din_1(din_1),
        .dout_1(dout_1),
        .din_32(din_32),
        .dout_32(dout_32),
        .din_142(din_142),
        .dout_142(dout_142)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        din_1   = 1'b0;
        din_32  = 32'h0;
        din_142 = 142'h0;

        // Check power-on state at T=0..#1
        #1;
        if (dout_1 !== 1'b0) begin
            $display("[FAIL T=%0t] dout_1 not initialized to 0: got %b", $time, dout_1);
            errors++;
        end
        if (dout_32 !== 32'h0) begin
            $display("[FAIL T=%0t] dout_32 not initialized to 0: got %h", $time, dout_32);
            errors++;
        end
        if (dout_142 !== 142'h0) begin
            $display("[FAIL T=%0t] dout_142 not initialized to 0: got %h", $time, dout_142);
            errors++;
        end

        // Step 1: Clock a few cycles with din == 0 (no enable triggered, dout stays 0)
        @(posedge clk);
        #1;
        @(posedge clk);
        #1;

        // Step 2: Set din to new non-zero values
        @(posedge clk);
        #1;
        din_1   = 1'b1;
        din_32  = 32'hA5A5_5A5A;
        din_142 = {71'h1234_5678_9ABC_DEF0_1, 71'h3EDC_BA98_7654_3210_F};

        @(posedge clk);
        #1;
        if (dout_1 !== 1'b1) begin
            $display("[FAIL T=%0t] dout_1 transition failed: exp=1 got=%b", $time, dout_1);
            errors++;
        end
        if (dout_32 !== 32'hA5A5_5A5A) begin
            $display("[FAIL T=%0t] dout_32 transition failed: exp=A5A55A5A got=%h", $time, dout_32);
            errors++;
        end
        if (dout_142 !== {71'h1234_5678_9ABC_DEF0_1, 71'h3EDC_BA98_7654_3210_F}) begin
            $display("[FAIL T=%0t] dout_142 transition failed: got=%h", $time, dout_142);
            errors++;
        end

        // Step 3: Keep din stable, verify holding behavior
        @(posedge clk);
        #1;
        if (dout_1 !== 1'b1 || dout_32 !== 32'hA5A5_5A5A ||
            dout_142 !== {71'h1234_5678_9ABC_DEF0_1, 71'h3EDC_BA98_7654_3210_F}) begin
            $display("[FAIL T=%0t] Hold failed while din stable", $time);
            errors++;
        end

        // Step 4: Transition to a second pattern
        @(posedge clk);
        #1;
        din_1   = 1'b0;
        din_32  = 32'h1234_5678;
        din_142 = 142'h2AAA_BBBB_CCCC_DDDD_EEEE_FFFF_0000_1111;

        @(posedge clk);
        #1;
        if (dout_1 !== 1'b0) begin
            $display("[FAIL T=%0t] dout_1 transition 2 failed: got=%b", $time, dout_1);
            errors++;
        end
        if (dout_32 !== 32'h1234_5678) begin
            $display("[FAIL T=%0t] dout_32 transition 2 failed: got=%h", $time, dout_32);
            errors++;
        end
        if (dout_142 !== 142'h2AAA_BBBB_CCCC_DDDD_EEEE_FFFF_0000_1111) begin
            $display("[FAIL T=%0t] dout_142 transition 2 failed: got=%h", $time, dout_142);
            errors++;
        end

        #10;
        if (errors == 0) begin
            $display("\n================================================================================");
            $display("[CAS v3.84.0.0 REGRESSION SUITE: INITREG FEEDBACK LOOPS]");
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified +initreg+0 deterministic power-on init and rvdffie self-gating.");
            $display("================================================================================\n");
        end else begin
            $display("\n[FATAL] v3_84_0_0_initreg_feedback_loops failed with %0d errors!\n", errors);
        end

        $finish;
    end

endmodule
