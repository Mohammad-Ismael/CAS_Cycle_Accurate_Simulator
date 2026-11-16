`timescale 1ns/1ps

//==============================================================================
// Testbench: tb_v3_85_0_regression
// Standard: IEEE 1800-2023 SystemVerilog
// Description:
//   Verification of multi-word wide vector logic operations (>64 bits):
//   142-bit and 320-bit bitwise, reductions, ternary muxes, and 2D packed slices.
//==============================================================================

module tb_v3_85_0_regression;

    logic         clk;
    logic         rst_n;

    logic [141:0] a142, b142;
    logic         sel142;
    logic [141:0] or142, and142, xor142, not142, mux142;
    logic         red_or142, red_and142, red_xor142;

    logic [319:0] a320, b320;
    logic         sel320;
    logic [319:0] or320, and320, xor320, not320, mux320;
    logic         red_or320, red_and320, red_xor320;

    logic [1:0][1:0]   mask;
    logic [1:0][141:0] hold;
    logic [1:0][141:0] sel_data;

    integer errors = 0;

    v3_85_0_0_wide_vector_logic_ops dut (.*);

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    // Test constants
    localparam [141:0] C_A142 = {14'h1234, 64'hF0F0_0000_AAAA_5555, 64'h1111_2222_3333_4444};
    localparam [141:0] C_B142 = {14'h0ABC, 64'h0F0F_FFFF_5555_AAAA, 64'h5555_6666_7777_8888};

    localparam [319:0] C_A320 = {64'h0123_4567_89AB_CDEF,
                                 64'hFEDC_BA98_7654_3210,
                                 64'hA5A5_5A5A_A5A5_5A5A,
                                 64'hFFFF_0000_FFFF_0000,
                                 64'h1122_3344_5566_7788};

    localparam [319:0] C_B320 = {64'h0011_2233_4455_6677,
                                 64'h8899_AABB_CCDD_EEFF,
                                 64'h5A5A_A5A5_5A5A_A5A5,
                                 64'h0000_FFFF_0000_FFFF,
                                 64'h8877_6655_4433_2211};

    initial begin
        rst_n = 0;
        a142 = 0; b142 = 0; sel142 = 0;
        a320 = 0; b320 = 0; sel320 = 0;
        mask = 0;
        hold = 0;
        #10;
        rst_n = 1;
        #10;

        // --- Test 1: 142-bit Operations ---
        a142 = C_A142;
        b142 = C_B142;
        sel142 = 1'b1;
        #5;

        if (or142 !== (C_A142 | C_B142)) begin
            $display("[FAIL T=%0t] 142-bit OR mismatch: got=%h exp=%h", $time, or142, (C_A142 | C_B142));
            errors++;
        end
        if (and142 !== (C_A142 & C_B142)) begin
            $display("[FAIL T=%0t] 142-bit AND mismatch: got=%h exp=%h", $time, and142, (C_A142 & C_B142));
            errors++;
        end
        if (xor142 !== (C_A142 ^ C_B142)) begin
            $display("[FAIL T=%0t] 142-bit XOR mismatch: got=%h exp=%h", $time, xor142, (C_A142 ^ C_B142));
            errors++;
        end
        if (not142 !== (~C_A142)) begin
            $display("[FAIL T=%0t] 142-bit NOT mismatch: got=%h exp=%h", $time, not142, (~C_A142));
            errors++;
        end
        if (mux142 !== C_A142) begin
            $display("[FAIL T=%0t] 142-bit MUX (sel=1) mismatch: got=%h exp=%h", $time, mux142, C_A142);
            errors++;
        end

        sel142 = 1'b0;
        #5;
        if (mux142 !== C_B142) begin
            $display("[FAIL T=%0t] 142-bit MUX (sel=0) mismatch: got=%h exp=%h", $time, mux142, C_B142);
            errors++;
        end

        // 142-bit Reductions
        if (red_or142 !== 1'b1 || red_and142 !== 1'b0 || red_xor142 !== (^C_A142)) begin
            $display("[FAIL T=%0t] 142-bit Reductions failed: or=%b and=%b xor=%b", $time, red_or142, red_and142, red_xor142);
            errors++;
        end

        // --- Test 2: 320-bit Operations ---
        a320 = C_A320;
        b320 = C_B320;
        sel320 = 1'b1;
        #5;

        if (or320 !== (C_A320 | C_B320)) begin
            $display("[FAIL T=%0t] 320-bit OR mismatch: got=%h exp=%h", $time, or320, (C_A320 | C_B320));
            errors++;
        end
        if (and320 !== (C_A320 & C_B320)) begin
            $display("[FAIL T=%0t] 320-bit AND mismatch: got=%h exp=%h", $time, and320, (C_A320 & C_B320));
            errors++;
        end
        if (xor320 !== (C_A320 ^ C_B320)) begin
            $display("[FAIL T=%0t] 320-bit XOR mismatch: got=%h exp=%h", $time, xor320, (C_A320 ^ C_B320));
            errors++;
        end
        if (not320 !== (~C_A320)) begin
            $display("[FAIL T=%0t] 320-bit NOT mismatch: got=%h exp=%h", $time, not320, (~C_A320));
            errors++;
        end
        if (mux320 !== C_A320) begin
            $display("[FAIL T=%0t] 320-bit MUX (sel=1) mismatch: got=%h exp=%h", $time, mux320, C_A320);
            errors++;
        end

        sel320 = 1'b0;
        #5;
        if (mux320 !== C_B320) begin
            $display("[FAIL T=%0t] 320-bit MUX (sel=0) mismatch: got=%h exp=%h", $time, mux320, C_B320);
            errors++;
        end

        // 320-bit Reductions
        if (red_or320 !== 1'b1 || red_and320 !== 1'b0 || red_xor320 !== (^C_A320)) begin
            $display("[FAIL T=%0t] 320-bit Reductions failed: or=%b and=%b xor=%b", $time, red_or320, red_and320, red_xor320);
            errors++;
        end

        // Test reduction of all-1s 320-bit vector
        a320 = ~320'h0;
        #5;
        if (red_and320 !== 1'b1 || red_or320 !== 1'b1 || red_xor320 !== 1'b0) begin
            $display("[FAIL T=%0t] 320-bit all-1s Reductions failed: or=%b and=%b xor=%b", $time, red_or320, red_and320, red_xor320);
            errors++;
        end

        // --- Test 3: 2D Packed Array Compound OR Bypass ---
        hold[0] = {71'h0, 71'h1111_2222_3333_4444_5};
        hold[1] = {71'h5555_6666_7777_8888_9, 71'h0};

        // Bank 0: enable both Way 0 and Way 1
        mask[0] = 2'b11;
        // Bank 1: enable Way 0 only
        mask[1] = 2'b01;
        #5;

        if (sel_data[0] !== {71'h5555_6666_7777_8888_9, 71'h1111_2222_3333_4444_5}) begin
            $display("[FAIL T=%0t] 2D packed compound OR Bank 0 mismatch: got=%h", $time, sel_data[0]);
            errors++;
        end

        if (sel_data[1] !== {71'h0, 71'h1111_2222_3333_4444_5}) begin
            $display("[FAIL T=%0t] 2D packed compound OR Bank 1 mismatch: got=%h", $time, sel_data[1]);
            errors++;
        end

        #10;
        if (errors == 0) begin
            $display("\n================================================================================");
            $display("[CAS v3.85.0.0 REGRESSION SUITE: WIDE VECTOR LOGIC OPS]");
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified 142-bit & 320-bit multi-word bitwise, reduction, mux & slices.");
            $display("================================================================================\n");
        end else begin
            $display("\n[FATAL] v3_85_0_0_wide_vector_logic_ops failed with %0d errors!\n", errors);
        end

        $finish;
    end

endmodule
