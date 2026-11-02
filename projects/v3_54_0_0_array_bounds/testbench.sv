`timescale 1ns/1ps

// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.54.0.0
// Standard Target: IEEE Std 1800-2023 §20.7 (Array Bound Queries: $left, $right, $low, $high, $increment)
// Testbench Name : tb_v3_54_0_regression
// ============================================================================

module tb_v3_54_0_regression;
    import v3_54_0_0_pkg::*;

    logic               clk;
    logic               rst_n;
    logic [7:0]         ctrl_in;
    logic [31:0]        dim1_left;
    logic [31:0]        dim1_right;
    logic [31:0]        dim1_low;
    logic [31:0]        dim1_high;
    logic signed [31:0] dim1_inc;
    logic [31:0]        dim2_left;
    logic [31:0]        dim2_right;
    logic [31:0]        dim2_low;
    logic [31:0]        dim2_high;
    logic signed [31:0] dim2_inc;
    logic               bounds_valid;

    // Test 3: Elaboration-time constant folding of $high, $low, and $increment
    localparam int H_VAL = $high(byte_desc_t);
    localparam int L_VAL = $low(byte_desc_t);
    logic [H_VAL-L_VAL:0] sized_vector; // [7-0:0] = [7:0] -> 8 bits

    int pass_count  = 0;
    int error_count = 0;

    v3_54_0_0_bounds_scanner dut (
        .clk         (clk),
        .rst_n       (rst_n),
        .ctrl_in     (ctrl_in),
        .dim1_left   (dim1_left),
        .dim1_right  (dim1_right),
        .dim1_low    (dim1_low),
        .dim1_high   (dim1_high),
        .dim1_inc    (dim1_inc),
        .dim2_left   (dim2_left),
        .dim2_right  (dim2_right),
        .dim2_low    (dim2_low),
        .dim2_high   (dim2_high),
        .dim2_inc    (dim2_inc),
        .bounds_valid(bounds_valid)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        logic [7:0]       vec_desc;      // [7:0]:  left=7, right=0, low=0, high=7, inc=1  (7 >= 0)
        logic [0:31]      vec_asc;       // [0:31]: left=0, right=31, low=0, high=31, inc=-1 (0 < 31)
        logic [15:0]      mem_unpkd [8]; // Dim 1: [0:7]  -> left=0, right=7, low=0, high=7, inc=-1
                                         // Dim 2: [15:0] -> left=15, right=0, low=0, high=15, inc=1

        rst_n   = 0;
        ctrl_in = 8'h00;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Procedural evaluation with default and explicit dimension args
        // --------------------------------------------------------------------
        if ($left(vec_desc)      == 7  && $right(vec_desc)     == 0  &&
            $low(vec_desc)       == 0  && $high(vec_desc)      == 7  &&
            $increment(vec_desc) == 1  &&
            $left(vec_asc)       == 0  && $right(vec_asc)      == 31 &&
            $low(vec_asc)        == 0  && $high(vec_asc)       == 31 &&
            $increment(vec_asc)  == -1 &&
            $left(mem_unpkd, 1)  == 0  && $right(mem_unpkd, 1) == 7  &&
            $low(mem_unpkd, 1)   == 0  && $high(mem_unpkd, 1)  == 7  &&
            $increment(mem_unpkd, 1) == -1 &&
            $left(mem_unpkd, 2)  == 15 && $right(mem_unpkd, 2) == 0  &&
            $low(mem_unpkd, 2)   == 0  && $high(mem_unpkd, 2)  == 15 &&
            $increment(mem_unpkd, 2) == 1) begin
            $display("[PASS T=%0t] Procedural bound queries verified: left, right, low, high, increment", $time);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Procedural bound queries failed!", $time);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: DUT sequential execution and internal multidimensional bounds
        // --------------------------------------------------------------------
        @(negedge clk);
        ctrl_in = 8'h5A;

        @(posedge clk);
        #1;
        if (dim1_left === 32'd0 && dim1_right === 32'd3 &&
            dim1_low  === 32'd0 && dim1_high  === 32'd3 &&
            dim1_inc  === -32'sd1 &&
            dim2_left === 32'd7 && dim2_right === 32'd0 &&
            dim2_low  === 32'd0 && dim2_high  === 32'd7 &&
            dim2_inc  === 32'sd1 &&
            bounds_valid === 1'b1) begin
            $display("[PASS T=%0t] DUT multidimensional bounds verified: dim1=[%0d:%0d inc=%0d] dim2=[%0d:%0d inc=%0d]",
                     $time, dim1_left, dim1_right, dim1_inc, dim2_left, dim2_right, dim2_inc);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] DUT multidimensional bounds failed! dim1=[%0d:%0d inc=%0d] dim2=[%0d:%0d inc=%0d]",
                     $time, dim1_left, dim1_right, dim1_inc, dim2_left, dim2_right, dim2_inc);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Elaboration-time constant folding of $high, $low, and $increment
        // --------------------------------------------------------------------
        if ($bits(sized_vector) == 8 && $increment(byte_desc_t) == 1) begin
            $display("[PASS T=%0t] Elaboration constant-folded bounds sizing verified: bits=%0d inc=%0d",
                     $time, $bits(sized_vector), $increment(byte_desc_t));
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Elaboration constant-folded bounds sizing failed!", $time);
            error_count++;
        end

        @(negedge clk);

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.54.0.0 REGRESSION SUITE: ARRAY BOUND QUERIES §20.7]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §20.7 $left, $right, $low, $high, and $increment.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule
