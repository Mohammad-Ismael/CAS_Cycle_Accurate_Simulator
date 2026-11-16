`timescale 1ns/1ps

//==============================================================================
// Testbench: tb_v3_86_0_regression
// Standard: IEEE 1800-2023 SystemVerilog (§12.8 break/continue, §11.3.5 short-circuit)
//==============================================================================

module tb_v3_86_0_regression;

    logic        clk;
    logic        rst_n;
    logic [31:0] for_break_idx;
    logic [31:0] for_break_sum;
    logic [31:0] for_cont_sum;
    logic [31:0] while_break_idx;
    logic [31:0] while_cont_sum;
    logic [31:0] side_effect_count;
    logic        sc_res1;
    logic        sc_res2;
    logic        sc_res3;
    logic        sc_res4;

    integer errors = 0;

    v3_86_0_0_loop_jump_and_short_circuit dut (.*);

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n = 0;
        #10;
        rst_n = 1;
        #10;

        // Check for-loop break results
        if (for_break_idx !== 32'd10) begin
            $display("[FAIL T=%0t] for_break_idx mismatch: exp=10 got=%0d", $time, for_break_idx);
            errors++;
        end else begin
            $display("[PASS T=%0t] for_break_idx = %0d", $time, for_break_idx);
        end

        if (for_break_sum !== 32'd45) begin
            $display("[FAIL T=%0t] for_break_sum mismatch: exp=45 got=%0d", $time, for_break_sum);
            errors++;
        end else begin
            $display("[PASS T=%0t] for_break_sum = %0d", $time, for_break_sum);
        end

        // Check for-loop continue results
        if (for_cont_sum !== 32'd20) begin
            $display("[FAIL T=%0t] for_cont_sum mismatch: exp=20 got=%0d", $time, for_cont_sum);
            errors++;
        end else begin
            $display("[PASS T=%0t] for_cont_sum = %0d", $time, for_cont_sum);
        end

        // Check while-loop break results
        if (while_break_idx !== 32'd15) begin
            $display("[FAIL T=%0t] while_break_idx mismatch: exp=15 got=%0d", $time, while_break_idx);
            errors++;
        end else begin
            $display("[PASS T=%0t] while_break_idx = %0d", $time, while_break_idx);
        end

        // Check while-loop continue results
        if (while_cont_sum !== 32'd30) begin
            $display("[FAIL T=%0t] while_cont_sum mismatch: exp=30 got=%0d", $time, while_cont_sum);
            errors++;
        end else begin
            $display("[PASS T=%0t] while_cont_sum = %0d", $time, while_cont_sum);
        end

        // Check short-circuit results
        if (sc_res1 !== 1'b0 || sc_res2 !== 1'b1 || sc_res3 !== 1'b1 || sc_res4 !== 1'b1) begin
            $display("[FAIL T=%0t] Short circuit boolean results mismatch: res1=%b res2=%b res3=%b res4=%b",
                     $time, sc_res1, sc_res2, sc_res3, sc_res4);
            errors++;
        end else begin
            $display("[PASS T=%0t] Short circuit boolean results verified", $time);
        end

        // Verify side effect counter is exactly 2 (not 4)
        if (side_effect_count !== 32'd2) begin
            $display("[FAIL T=%0t] side_effect_count mismatch: exp=2 got=%0d (short-circuit failed)", $time, side_effect_count);
            errors++;
        end else begin
            $display("[PASS T=%0t] side_effect_count = %0d (short-circuit verified)", $time, side_effect_count);
        end

        #10;
        if (errors == 0) begin
            $display("\n================================================================================");
            $display("[CAS v3.86.0.0 REGRESSION SUITE: LOOP JUMP AND SHORT CIRCUIT]");
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800 break/continue and &&/|| short-circuit evaluation.");
            $display("================================================================================\n");
        end else begin
            $display("\n[FATAL] v3_86_0_0_loop_jump_and_short_circuit failed with %0d errors!\n", errors);
        end

        $finish;
    end

endmodule
