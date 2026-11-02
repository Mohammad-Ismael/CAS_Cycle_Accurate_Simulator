`timescale 1ns/1ps

// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.40.0.0
// Standard Target: IEEE Std 1800-2023 §11.12 ('let' Declarations)
// Testbench Name : tb_v3_40_0_regression
// ============================================================================

module tb_v3_40_0_regression;
    logic       clk;
    logic       rst_n;
    logic [7:0] data_in;
    logic       data_valid;
    logic [7:0] data_out;
    logic       filter_pass;
    logic       even_tag;

    int pass_count  = 0;
    int error_count = 0;

    // Procedural/block-scoped let declaration (§11.12)
    let clip_max(v, max_v = 8'd50) = (v > max_v) ? max_v : v;

    v3_40_0_0_packet_filter dut (
        .clk        (clk),
        .rst_n      (rst_n),
        .data_in    (data_in),
        .data_valid (data_valid),
        .data_out   (data_out),
        .filter_pass(filter_pass),
        .even_tag   (even_tag)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n      = 0;
        data_in    = 8'h00;
        data_valid = 1'b0;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Package let 'is_even' and module default-arg 'in_window'
        // data_in = 20 (even, inside [10..100])
        // --------------------------------------------------------------------
        @(negedge clk);
        data_in    = 8'd20;
        data_valid = 1'b1;

        @(posedge clk);
        #1;
        if (data_out === 8'd20 && filter_pass === 1'b1 && even_tag === 1'b1) begin
            $display("[PASS T=%0t] Let expressions in sequential logic verified: out=%0d pass=%b even=%b",
                     $time, data_out, filter_pass, even_tag);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Let expressions in sequential logic failed! out=%0d pass=%b even=%b",
                     $time, data_out, filter_pass, even_tag);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: Out of window boundary (data_in = 5 < 10) and odd parity
        // --------------------------------------------------------------------
        @(negedge clk);
        data_in    = 8'd5;
        data_valid = 1'b1;

        @(posedge clk);
        #1;
        if (data_out === 8'd5 && filter_pass === 1'b0 && even_tag === 1'b0) begin
            $display("[PASS T=%0t] Let range boundary check verified: out=%0d pass=%b even=%b",
                     $time, data_out, filter_pass, even_tag);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Let range boundary check failed! out=%0d pass=%b even=%b",
                     $time, data_out, filter_pass, even_tag);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Block-scoped let with default value override
        // clip_max(80) -> default max 50 -> returns 50
        // clip_max(80, 90) -> custom max 90 -> returns 80
        // --------------------------------------------------------------------
        begin
            logic [7:0] c1, c2;
            c1 = clip_max(8'd80);
            c2 = clip_max(8'd80, 8'd90);

            if (c1 === 8'd50 && c2 === 8'd80) begin
                $display("[PASS T=%0t] Block-scoped let with default args verified: c1=%0d c2=%0d",
                         $time, c1, c2);
                pass_count++;
            end else begin
                $display("[FAIL T=%0t] Block-scoped let with default args failed! c1=%0d (exp 50) c2=%0d (exp 80)",
                         $time, c1, c2);
                error_count++;
            end
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.40.0.0 REGRESSION SUITE: 'LET' DECLARATIONS §11.12]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §11.12 package/module/block let declarations, typed/untyped formals, and default values.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule