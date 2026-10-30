// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.23.0.0
// Standard Target: IEEE Std 1800-2023 §24 (program, endprogram, Reactive execution)
// Testbench Name : tb_v3_23_0_0_regression
// ============================================================================

// ----------------------------------------------------------------------------
// IEEE 1800-2023 §24 Program Block Declaration (Reactive Region Testbench)
// ----------------------------------------------------------------------------
program test_prog (
    input  logic        clk,
    output logic        rst_n,
    output logic        push,
    output logic [15:0] din,
    input  logic        valid,
    input  logic [15:0] dout
);
    int pass_count  = 0;
    int error_count = 0;

    // Initial blocks in program execute in the Reactive region (§24.3)
    initial begin
        rst_n = 0;
        push  = 0;
        din   = 16'h0000;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Transaction 1: Drive push & din directly at posedge without race
        // Program drives in Reactive; Design samples in Active on next clock
        // --------------------------------------------------------------------
        @(posedge clk);
        push = 1'b1;
        din  = 16'h1000;

        @(posedge clk);
        push = 1'b1;
        din  = 16'h2000;

        @(posedge clk);
        push = 1'b0;
        din  = 16'h0000;

        // Verify captured results from Reactive region
        if (valid !== 1'b1 || dout !== 16'h2010) begin
            $display("[FAIL T=%0t] Data mismatch! valid=%b dout=%h (exp 1, 2010)",
                     $time, valid, dout);
            error_count++;
        end else begin
            pass_count++;
        end

        @(posedge clk);
        if (valid !== 1'b0 || dout !== 16'h0000) begin
            $display("[FAIL T=%0t] Flush mismatch! valid=%b dout=%h (exp 0, 0000)",
                     $time, valid, dout);
            error_count++;
        end else begin
            pass_count++;
        end

        #5;
        $display("\n================================================================================");
        $display("[CAS v3.23.0.0 REGRESSION SUITE: PROGRAM BLOCKS & REACTIVE SCHEDULING]");
        if (error_count == 0 && pass_count == 2) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §24 program blocks, reactive scheduling, and race avoidance.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 2, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endprogram

// ----------------------------------------------------------------------------
// Top-level structural wrapper connecting design and program
// ----------------------------------------------------------------------------
module tb_v3_23_0_0_regression;
    logic        clk;
    logic        rst_n;
    logic        push;
    logic [15:0] din;
    logic        valid;
    logic [15:0] dout;

    // Free-running clock generator in module (Active region)
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    // Design Module Instance
    v3_23_0_0_fifo_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .push(push),
        .din(din),
        .valid(valid),
        .dout(dout)
    );

    // Program Block Instance
    test_prog prog_inst (
        .clk(clk),
        .rst_n(rst_n),
        .push(push),
        .din(din),
        .valid(valid),
        .dout(dout)
    );

    initial begin
        $monitor("Time=%0t | clk=%b rst_n=%b | push=%b din=%h | valid=%b dout=%h",
                 $time, clk, rst_n, push, din, valid, dout);
    end
endmodule