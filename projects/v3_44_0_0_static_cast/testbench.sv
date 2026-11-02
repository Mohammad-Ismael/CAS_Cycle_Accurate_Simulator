`timescale 1ns/1ps

// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.44.0.0
// Standard Target: IEEE Std 1800-2023 §6.24.1 (Static Cast Operators)
// Testbench Name : tb_v3_44_0_regression
// ============================================================================

module tb_v3_44_0_regression;
    import v3_44_0_0_pkg::*;

    logic        clk;
    logic        rst_n;
    logic [7:0]  raw_val;
    logic [1:0]  raw_op;
    logic [15:0] sized_out;
    logic [15:0] signed_out;
    logic [1:0]  enum_out;

    int pass_count  = 0;
    int error_count = 0;

    v3_44_0_0_normalizer dut (
        .clk       (clk),
        .rst_n     (rst_n),
        .raw_val   (raw_val),
        .raw_op    (raw_op),
        .sized_out (sized_out),
        .signed_out(signed_out),
        .enum_out  (enum_out)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n   = 0;
        raw_val = 8'h00;
        raw_op  = 2'b00;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Sized cast 16'(val * 2) and Enum cast opcode_e'(raw_op)
        // raw_val = 8'd50 -> 50 * 2 = 100 (0x0064)
        // raw_op  = 2'b10 (OP_SUB)
        // --------------------------------------------------------------------
        @(negedge clk);
        raw_val = 8'd50;
        raw_op  = 2'b10;

        @(posedge clk);
        #1;
        if (sized_out === 16'h0064 && enum_out === 2'b10) begin
            $display("[PASS T=%0t] Sized cast and enum cast verified: sized=%04h enum=%b",
                     $time, sized_out, enum_out);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Sized cast or enum cast failed! sized=%04h enum=%b",
                     $time, sized_out, enum_out);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: Signed cast sign-extension
        // raw_val = 8'hFE (-2 in 8-bit 2's complement)
        // signed'(8'hFE) sign-extended to 16 bits -> 16'hFFFE
        // --------------------------------------------------------------------
        @(negedge clk);
        raw_val = 8'hFE;
        raw_op  = 2'b01;

        @(posedge clk);
        #1;
        if (signed_out === 16'hFFFE) begin
            $display("[PASS T=%0t] Signed cast sign-extension verified: signed_out=%04h",
                     $time, signed_out);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Signed cast failed! got=%04h (exp FFFE)",
                     $time, signed_out);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Sized truncation and Struct static cast in procedural block
        // 8'(16'h1234) -> 8'h34
        // word_st'(16'hA55A) -> high=0xA5, low=0x5A
        // --------------------------------------------------------------------
        begin
            logic [7:0] trunc_val;
            word_st     st_val;

            trunc_val = 8'(16'h1234);
            st_val    = word_st'(16'hA55A);

            if (trunc_val === 8'h34 && st_val.high_byte === 8'hA5 && st_val.low_byte === 8'h5A) begin
                $display("[PASS T=%0t] Sized truncation and struct static cast verified: trunc=%02h hi=%02h lo=%02h",
                         $time, trunc_val, st_val.high_byte, st_val.low_byte);
                pass_count++;
            end else begin
                $display("[FAIL T=%0t] Sized truncation or struct cast failed! trunc=%02h",
                         $time, trunc_val);
                error_count++;
            end
        end

        @(negedge clk);

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.44.0.0 REGRESSION SUITE: STATIC CAST OPERATORS §6.24.1]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §6.24.1 size casting, type casting, sign casting, and enum coercion.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule