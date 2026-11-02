`timescale 1ns/1ps

// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.49.0.0
// Standard Target: IEEE Std 1800-2023 §6.23 (Type Operator 'type(...)')
// Testbench Name : tb_v3_49_0_regression
// ============================================================================

module tb_v3_49_0_regression;
    import v3_49_0_0_pkg::*;

    logic       clk;
    logic       rst_n;
    logic [7:0] raw_in;
    logic       type_match_out;
    logic [7:0] shadow_out;

    int pass_count  = 0;
    int error_count = 0;

    v3_49_0_0_type_checker dut (
        .clk           (clk),
        .rst_n         (rst_n),
        .raw_in        (raw_in),
        .type_match_out(type_match_out),
        .shadow_out    (shadow_out)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        int          x, y;
        logic [7:0]  vec_a;
        logic [15:0] vec_b;
        bit          res_match;
        bit          res_mismatch;

        rst_n  = 0;
        raw_in = 8'h00;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Compile-time type comparison on primitive variables
        // --------------------------------------------------------------------
        res_match    = (type(x) == type(y));         // Both are int -> 1
        res_mismatch = (type(vec_a) == type(vec_b)); // logic[7:0] vs logic[15:0] -> 0

        if (res_match === 1'b1 && res_mismatch === 1'b0) begin
            $display("[PASS T=%0t] Primitive type comparisons verified: match=%b mismatch=%b",
                     $time, res_match, res_mismatch);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Primitive type comparisons failed! match=%b mismatch=%b",
                     $time, res_match, res_mismatch);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: Inferred type variable declaration and assignment
        // --------------------------------------------------------------------
        begin
            type(vec_a) local_shadow;
            local_shadow = 8'h5A;
            if (local_shadow === 8'h5A && type(local_shadow) == type(vec_a)) begin
                $display("[PASS T=%0t] Inferred type variable declaration verified: val=%02h",
                         $time, local_shadow);
                pass_count++;
            end else begin
                $display("[FAIL T=%0t] Inferred type variable declaration failed!", $time);
                error_count++;
            end
        end

        // --------------------------------------------------------------------
        // Test 3: DUT runtime execution with type comparisons & shadow pipeline
        // Clock 2 cycles to account for the two-stage non-blocking pipeline:
        // raw_in -> shadow_reg -> shadow_out
        // --------------------------------------------------------------------
        @(negedge clk);
        raw_in = 8'hA5;

        @(posedge clk); // Cycle 1: shadow_reg <= 8'hA5
        @(posedge clk); // Cycle 2: shadow_out <= shadow_reg (8'hA5)
        #1;
        if (type_match_out === 1'b1 && shadow_out === 8'hA5) begin
            $display("[PASS T=%0t] DUT type comparison and shadow register verified: match=%b shadow=%02h",
                     $time, type_match_out, shadow_out);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] DUT type comparison or shadow register failed! match=%b shadow=%02h",
                     $time, type_match_out, shadow_out);
            error_count++;
        end

        @(negedge clk);

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.49.0.0 REGRESSION SUITE: TYPE OPERATOR type(...) §6.23]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §6.23 type(...) comparisons and type-inferred variable declarations.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule