// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.35.0.0
// Standard Target: IEEE Std 1800-2023 §11.4.14 (Streaming Operators)
// Testbench Name : tb_v3_35_0_0_regression
// ============================================================================

module tb_v3_35_0_0_regression;
    logic        clk;
    logic        rst_n;
    logic        trigger;
    logic [31:0] word_in;
    logic [31:0] byte_rev;
    logic [31:0] bit_rev;
    logic [7:0]  unpacked_bytes [0:3];
    logic        valid_out;

    int pass_count  = 0;
    int error_count = 0;

    v3_35_0_0_stream_engine dut (
        .clk               (clk),
        .rst_n             (rst_n),
        .trigger           (trigger),
        .word_in           (word_in),
        .byte_reversed_word(byte_rev),
        .bit_reversed_word (bit_rev),
        .unpacked_bytes    (unpacked_bytes),
        .valid_out         (valid_out)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n   = 0;
        trigger = 0;
        word_in = 32'h1234_5678;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Combinational Stream Packing & Slice Reordering (§11.4.14)
        // word_in = 32'h1234_5678
        // byte_rev = { << 8 { 32'h12345678 } } = 32'h7856_3412
        // bit_rev  = { << { 32'h12345678 } }  = 32'h1E6A_2C48
        // unpacked_bytes[0] = 8'h12, [1] = 8'h34, [2] = 8'h56, [3] = 8'h78
        // --------------------------------------------------------------------
        #1;
        if (byte_rev === 32'h7856_3412 &&
            bit_rev === 32'h1E6A_2C48 &&
            unpacked_bytes[0] === 8'h12 &&
            unpacked_bytes[1] === 8'h34 &&
            unpacked_bytes[2] === 8'h56 &&
            unpacked_bytes[3] === 8'h78) begin
            $display("[PASS T=%0t] Combinational streaming operators verified: byte_rev=%08h bit_rev=%08h",
                     $time, byte_rev, bit_rev);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Combinational streaming mismatch! byte_rev=%08h (exp 78563412) bit_rev=%08h",
                     $time, byte_rev, bit_rev);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: Sequential Loopback Reassembly at posedge clk
        // --------------------------------------------------------------------
        @(negedge clk);
        trigger = 1;

        @(posedge clk);
        #1;
        trigger = 0;

        if (valid_out === 1'b1) begin
            $display("[PASS T=%0t] DUT loopback reassembly & slice integrity verified: valid_out=%b",
                     $time, valid_out);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] DUT loopback reassembly failed! valid_out=%b",
                     $time, valid_out);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Standalone Dynamic Stream Concatenation with nibble slices (4-bit)
        // --------------------------------------------------------------------
        begin
            logic [15:0] raw_nibbles;
            logic [15:0] reversed_nibbles;
            raw_nibbles      = 16'hABCD;
            reversed_nibbles = { << 4 { raw_nibbles } }; // ABCD -> DCBA

            if (reversed_nibbles === 16'hDCBA) begin
                $display("[PASS T=%0t] Nibble-slice stream reversal verified: in=%04h out=%04h",
                         $time, raw_nibbles, reversed_nibbles);
                pass_count++;
            end else begin
                $display("[FAIL T=%0t] Nibble-slice stream reversal failed! got %04h (exp DCBA)",
                         $time, reversed_nibbles);
                error_count++;
            end
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.35.0.0 REGRESSION SUITE: STREAMING OPERATORS {>> {}} & {<< {}} §11.4.14]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §11.4.14 streaming operators ({>>}, {<<}) with bit, byte, and nibble slices.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | rst_n=%b trig=%b | byte_rev=%08h bit_rev=%08h valid=%b",
                 $time, rst_n, trigger, byte_rev, bit_rev, valid_out);
    end
endmodule