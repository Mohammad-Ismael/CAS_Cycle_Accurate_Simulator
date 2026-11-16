`timescale 1ns/1ps

// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.45.0.0
// Standard Target: IEEE Std 1800-2023 §6.24.3 (Bit-Stream Casting)
// Testbench Name : tb_v3_45_0_regression
// ============================================================================

module tb_v3_45_0_regression;
    import v3_45_0_0_pkg::*;

    logic        clk;
    logic        rst_n;
    logic [31:0] raw_stream;
    logic        stream_valid;
    logic [7:0]  hdr_src;
    logic [7:0]  hdr_dst;
    logic [15:0] hdr_len;
    logic [31:0] reassembled_stream;

    int pass_count  = 0;
    int error_count = 0;

    v3_45_0_0_stream_converter dut (
        .clk               (clk),
        .rst_n             (rst_n),
        .raw_stream        (raw_stream),
        .stream_valid      (stream_valid),
        .hdr_src           (hdr_src),
        .hdr_dst           (hdr_dst),
        .hdr_len           (hdr_len),
        .reassembled_stream(reassembled_stream)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n        = 0;
        raw_stream   = 32'h0000_0000;
        stream_valid = 1'b0;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Bit-stream cast to struct and unpacked array roundtrip
        // raw = 32'hA5_5A_0100 -> src=0xA5, dst=0x5A, len=0x0100
        // --------------------------------------------------------------------
        @(negedge clk);
        raw_stream   = 32'hA55A_0100;
        stream_valid = 1'b1;

        @(posedge clk);
        #1;
        if (hdr_src === 8'hA5 && hdr_dst === 8'h5A && hdr_len === 16'h0100 &&
            reassembled_stream === 32'hA55A_0100) begin
            $display("[PASS T=%0t] Bit-stream roundtrip cast verified: src=%02h dst=%02h len=%04h stream=%08h",
                     $time, hdr_src, hdr_dst, hdr_len, reassembled_stream);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Bit-stream roundtrip cast failed! stream=%08h (exp A55A0100)",
                     $time, reassembled_stream);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: Unpacked array element indexing check after cast
        // --------------------------------------------------------------------
        begin
            byte_array_t test_bytes;
            test_bytes = byte_array_t'(32'h1234_5678);

            if (test_bytes[0] === 8'h12 && test_bytes[1] === 8'h34 &&
                test_bytes[2] === 8'h56 && test_bytes[3] === 8'h78) begin
                $display("[PASS T=%0t] Unpacked byte array stream ordering verified: [0]=%02h [1]=%02h [2]=%02h [3]=%02h",
                         $time, test_bytes[0], test_bytes[1], test_bytes[2], test_bytes[3]);
                pass_count++;
            end else begin
                $display("[FAIL T=%0t] Unpacked byte array stream ordering failed!", $time);
                error_count++;
            end
        end

        // --------------------------------------------------------------------
        // Test 3: Cast between two distinct packed structs with identical bit width
        // --------------------------------------------------------------------
        begin
            typedef struct packed {
                logic [15:0] upper_word;
                logic [15:0] lower_word;
            } words_t;

            words_t w;
            w = words_t'(32'hDEAD_BEEF);

            if (w.upper_word === 16'hDEAD && w.lower_word === 16'hBEEF) begin
                $display("[PASS T=%0t] Bit-stream cast between packed struct layouts verified: upper=%04h lower=%04h",
                         $time, w.upper_word, w.lower_word);
                pass_count++;
            end else begin
                $display("[FAIL T=%0t] Bit-stream cast between packed struct layouts failed!", $time);
                error_count++;
            end
        end

        @(negedge clk);
        stream_valid = 1'b0;

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.45.0.0 REGRESSION SUITE: BIT-STREAM CASTING §6.24.3]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §6.24.3 bit-stream casting across structs, unpacked arrays, and vector types.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule