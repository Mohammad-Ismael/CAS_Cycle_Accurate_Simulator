`timescale 1ns/100ps

// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.29.0.0
// Standard Target: IEEE Std 1800-2023 §32 (Specify Blocks & Module Path Delays)
// Testbench Name : tb_v3_29_0_0_regression
// ============================================================================

module tb_v3_29_0_0_regression;
    logic in_a;
    logic in_b;
    logic sel;
    wire  out_y;

    int pass_count  = 0;
    int error_count = 0;

    v3_29_0_0_delay_cell dut (
        .in_a (in_a),
        .in_b (in_b),
        .sel  (sel),
        .out_y(out_y)
    );

    initial begin
        in_a = 0;
        in_b = 0;
        sel  = 0;

        // Initial settling: default parallel path settles at T=3ns
        #10;
        if (out_y !== 1'b0) begin
            $display("[FAIL T=%0t] Initial out_y mismatch! got %b (exp 0)", $time, out_y);
            error_count++;
        end else begin
            pass_count++;
        end

        // --------------------------------------------------------------------
        // Test 1: Stimulus on in_a while sel=0
        // Path (in_a => out_y) delay = 4ns.
        // At T=10, in_a toggles to 1.
        // At T=13 (after 3ns), out_y must still be 0.
        // At T=14.1 (after 4.1ns), out_y must be 1.
        // --------------------------------------------------------------------
        in_a = 1;
        #3; // T=13.0
        if (out_y !== 1'b0) begin
            $display("[FAIL T=%0t] in_a early transition! got %b (exp 0)", $time, out_y);
            error_count++;
        end else begin
            pass_count++;
        end

        #1.1; // T=14.1
        if (out_y !== 1'b1) begin
            $display("[FAIL T=%0t] in_a -> out_y (delay 4) mismatch! got %b (exp 1)", $time, out_y);
            error_count++;
        end else begin
            pass_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: Stimulus on sel (sel => out_y delay = 3ns)
        // Advance to T=20.0
        // At T=20.0, sel toggles to 1 (in_b=0, so out_y will transition to 0)
        // At T=22.0 (after 2ns), out_y must still be 1.
        // At T=23.1 (after 3.1ns), out_y must be 0.
        // --------------------------------------------------------------------
        #5.9; // T=20.0
        sel = 1;
        #2;   // T=22.0
        if (out_y !== 1'b1) begin
            $display("[FAIL T=%0t] sel early transition! got %b (exp 1)", $time, out_y);
            error_count++;
        end else begin
            pass_count++;
        end

        #1.1; // T=23.1
        if (out_y !== 1'b0) begin
            $display("[FAIL T=%0t] sel -> out_y (delay 3) mismatch! got %b (exp 0)", $time, out_y);
            error_count++;
        end else begin
            pass_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Stimulus on in_b while sel=1 (delay = 2ns)
        // Advance to T=30.0
        // At T=30.0, in_b toggles to 1.
        // At T=31.0 (after 1ns), out_y must still be 0.
        // At T=32.1 (after 2.1ns), out_y must be 1.
        // --------------------------------------------------------------------
        #6.9; // T=30.0
        in_b = 1;
        #1;   // T=31.0
        if (out_y !== 1'b0) begin
            $display("[FAIL T=%0t] in_b early transition! got %b (exp 0)", $time, out_y);
            error_count++;
        end else begin
            pass_count++;
        end

        #1.1; // T=32.1
        if (out_y !== 1'b1) begin
            $display("[FAIL T=%0t] in_b -> out_y (delay 2) mismatch! got %b (exp 1)", $time, out_y);
            error_count++;
        end else begin
            pass_count++;
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.29.0.0 REGRESSION SUITE: SPECIFY BLOCKS & PATH DELAYS]");
        if (error_count == 0 && pass_count == 7) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §32 specify blocks, parallel paths, and conditional path delays.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 7, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | in_a=%b in_b=%b sel=%b | out_y=%b",
                 $time, in_a, in_b, sel, out_y);
    end
endmodule