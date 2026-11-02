`timescale 1ns/1ps

// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.51.0.0
// Standard Target: IEEE Std 1800-2023 §20.8 ($clog2 and Integer Math Functions)
// Testbench Name : tb_v3_51_0_regression
// ============================================================================

module tb_v3_51_0_regression;
    import v3_51_0_0_pkg::*;

    localparam int TEST_DEPTH = 128; // $clog2(128) = 7 bits

    logic                             clk;
    logic                             rst_n;
    logic                             push;
    logic                             pop;
    logic [7:0]                       data_in;
    logic [7:0]                       data_out;
    logic [$clog2(TEST_DEPTH)-1:0]    wr_ptr;
    logic [$clog2(TEST_DEPTH)-1:0]    rd_ptr;
    logic [31:0]                      runtime_clog_out;

    int pass_count  = 0;
    int error_count = 0;

    v3_51_0_0_fifo_ctrl #(
        .BUFFER_ENTRIES(TEST_DEPTH)
    ) dut (
        .clk             (clk),
        .rst_n           (rst_n),
        .push            (push),
        .pop             (pop),
        .data_in         (data_in),
        .data_out        (data_out),
        .wr_ptr          (wr_ptr),
        .rd_ptr          (rd_ptr),
        .runtime_clog_out(runtime_clog_out)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n   = 0;
        push    = 0;
        pop     = 0;
        data_in = 8'h00;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Standard IEEE 1800-2023 §20.8.1 edge-case table validation
        // --------------------------------------------------------------------
        if ($clog2(0) == 0 &&
            $clog2(1) == 0 &&
            $clog2(2) == 1 &&
            $clog2(3) == 2 &&
            $clog2(4) == 2 &&
            $clog2(5) == 3 &&
            $clog2(8) == 3 &&
            $clog2(9) == 4 &&
            $clog2(16) == 4 &&
            $clog2(1024) == 10 &&
            $clog2(1025) == 11) begin
            $display("[PASS T=%0t] Standard $clog2 mathematical table verified across boundary conditions", $time);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Standard $clog2 boundary condition table failed!", $time);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: Parameterized port width and pointer declaration validation
        // $clog2(128) = 7 bits -> wr_ptr must be 7-bit wide
        // --------------------------------------------------------------------
        if ($bits(wr_ptr) == 7 && ADDR_WIDTH == 5) begin
            $display("[PASS T=%0t] Elaboration-time $clog2 port & parameter sizing verified: wr_ptr_bits=%0d pkg_bits=%0d",
                     $time, $bits(wr_ptr), ADDR_WIDTH);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Elaboration-time $clog2 port sizing failed! got wr_ptr_bits=%0d exp 7",
                     $time, $bits(wr_ptr));
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Sequential DUT execution and runtime $clog2 evaluation
        // Push 4 entries -> w_ptr increments from 0 to 4
        // At w_ptr = 3, w_ptr + 1 = 4 -> $clog2(4) = 2
        // At w_ptr = 4, w_ptr + 1 = 5 -> $clog2(5) = 3
        // --------------------------------------------------------------------
        @(negedge clk);
        push    = 1'b1;
        data_in = 8'hA1;

        repeat (4) @(posedge clk);
        #1;
        push = 1'b0;

        // One more clock cycle to sample w_ptr = 4 into runtime_clog_out
        @(posedge clk);
        #1;
        if (wr_ptr === 7'd4 && runtime_clog_out === 32'd3) begin
            $display("[PASS T=%0t] Sequential runtime $clog2 execution verified: wr_ptr=%0d clog2(w_ptr+1)=%0d",
                     $time, wr_ptr, runtime_clog_out);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Sequential runtime $clog2 execution failed! wr_ptr=%0d clog2=%0d",
                     $time, wr_ptr, runtime_clog_out);
            error_count++;
        end

        @(negedge clk);

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.51.0.0 REGRESSION SUITE: MATH FUNCTIONS $clog2 §20.8]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §20.8 $clog2 elaboration-time sizing and runtime math.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule