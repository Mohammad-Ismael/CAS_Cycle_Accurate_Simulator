`timescale 1ns/1ps

// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.53.0.0
// Standard Target: IEEE Std 1800-2023 §20.7 ($dimensions & $unpacked_dimensions)
// Testbench Name : tb_v3_53_0_regression
// ============================================================================

module tb_v3_53_0_regression;
    import v3_53_0_0_pkg::*;

    logic       clk;
    logic       rst_n;
    logic [7:0] ctrl_in;
    logic [3:0] total_dims_out;
    logic [3:0] unpacked_dims_out;
    logic       dim_match_valid;

    // Test 3: Elaboration-time constant usage of $dimensions and $unpacked_dimensions
    localparam int D_TOTAL = $dimensions(frame_buf_t);
    localparam int D_UNPKD = $unpacked_dimensions(frame_buf_t);
    logic [D_TOTAL-1:0] total_dim_bus;
    logic [D_UNPKD-1:0] unpkd_dim_bus;

    int pass_count  = 0;
    int error_count = 0;

    v3_53_0_0_dim_checker dut (
        .clk              (clk),
        .rst_n            (rst_n),
        .ctrl_in          (ctrl_in),
        .total_dims_out   (total_dims_out),
        .unpacked_dims_out(unpacked_dims_out),
        .dim_match_valid  (dim_match_valid)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        logic [31:0]     vec_1d;
        logic [7:0][7:0] packed_2d;
        logic [15:0]     mem_1u [32];
        logic [7:0]      cube_3d [2][4][8];

        rst_n   = 0;
        ctrl_in = 8'h00;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Procedural evaluation on array variables and array data types
        // --------------------------------------------------------------------
        if ($dimensions(vec_1d)          == 1 && $unpacked_dimensions(vec_1d)          == 0 &&
            $dimensions(packed_2d)       == 2 && $unpacked_dimensions(packed_2d)       == 0 &&
            $dimensions(mem_1u)          == 2 && $unpacked_dimensions(mem_1u)          == 1 &&
            $dimensions(cube_3d)         == 4 && $unpacked_dimensions(cube_3d)         == 3 &&
            $dimensions(frame_buf_t)     == 3 && $unpacked_dimensions(frame_buf_t)     == 2) begin
            $display("[PASS T=%0t] Procedural $dimensions and $unpacked_dimensions verified across packed and unpacked structures", $time);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Procedural array dimension queries failed!", $time);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: DUT sequential execution and internal dimension evaluation
        // --------------------------------------------------------------------
        @(negedge clk);
        ctrl_in = 8'hA5;

        @(posedge clk);
        #1;
        if (total_dims_out === 4'd3 &&
            unpacked_dims_out === 4'd2 &&
            dim_match_valid === 1'b1) begin
            $display("[PASS T=%0t] DUT dimension queries verified: total_dims=%0d unpacked_dims=%0d match_valid=%b",
                     $time, total_dims_out, unpacked_dims_out, dim_match_valid);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] DUT dimension queries failed! total_dims=%0d unpacked_dims=%0d match_valid=%b",
                     $time, total_dims_out, unpacked_dims_out, dim_match_valid);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Elaboration-time constant usage of $dimensions
        // --------------------------------------------------------------------
        if ($bits(total_dim_bus) == 3 && $bits(unpkd_dim_bus) == 2) begin
            $display("[PASS T=%0t] Constant-folded localparam vector sizing via $dimensions verified: bits(total)=%0d bits(unpkd)=%0d",
                     $time, $bits(total_dim_bus), $bits(unpkd_dim_bus));
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Constant-folded localparam vector sizing failed!", $time);
            error_count++;
        end

        @(negedge clk);

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.53.0.0 REGRESSION SUITE: ARRAY DIMENSIONS $dimensions §20.7]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §20.7 $dimensions and $unpacked_dimensions queries.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule
