// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.80.0.0
// Standard Target: IEEE Std 1800-2023 §7.2 (Packed Struct Member Slicing)
// Testbench Name : tb_v3_80_0_regression
// ============================================================================

module tb_v3_80_0_regression;
    import v3_80_0_0_pkg::*;

    logic clk;
    logic rst_n;
    packet_t pkt_in;
    logic [7:0]  payload_byte0_comb;
    logic [7:0]  payload_byte1_comb;
    logic [15:0] payload_word_seq;

    // Cross-hierarchical continuous assignment to struct member part-select (inst.pkt.member[7:0])
    logic [7:0] hier_member_slice;
    assign hier_member_slice = dut.pkt_in.payload[7:0];

    int pass_count  = 0;
    int error_count = 0;

    v3_80_0_0_struct_member_slice dut (
        .clk               (clk),
        .rst_n             (rst_n),
        .pkt_in            (pkt_in),
        .payload_byte0_comb(payload_byte0_comb),
        .payload_byte1_comb(payload_byte1_comb),
        .payload_word_seq  (payload_word_seq)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n = 0;
        pkt_in = '0;
        #15;
        rst_n = 1;
        #10;

        // Transaction 1
        pkt_in.header = 16'h1234;
        pkt_in.payload = 32'hDEADBEEF;
        pkt_in.crc = 8'h99;

        #2; // comb propagation
        if (payload_byte0_comb === 8'hEF && payload_byte1_comb === 8'hBE) begin
            $display("[PASS T=%0t] Struct member comb slice: byte0=0x%02x byte1=0x%02x",
                     $time, payload_byte0_comb, payload_byte1_comb);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Struct member comb slice mismatch: byte0=0x%02x byte1=0x%02x (exp EF, BE)",
                     $time, payload_byte0_comb, payload_byte1_comb);
            error_count++;
        end

        if (hier_member_slice === 8'hEF) begin
            $display("[PASS T=%0t] Hierarchical struct member slice: 0x%02x", $time, hier_member_slice);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Hierarchical struct member slice mismatch: 0x%02x (exp EF)",
                     $time, hier_member_slice);
            error_count++;
        end

        @(posedge clk);
        #1;
        if (payload_word_seq === 16'hADBE) begin
            $display("[PASS T=%0t] Struct member seq slice: word=0x%04x", $time, payload_word_seq);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Struct member seq slice mismatch: word=0x%04x (exp ADBE)",
                     $time, payload_word_seq);
            error_count++;
        end

        // Transaction 2
        @(negedge clk);
        pkt_in.payload = 32'h11223344;
        #2;
        if (payload_byte0_comb === 8'h44 && payload_byte1_comb === 8'h33) begin
            $display("[PASS T=%0t] Struct member comb slice 2: byte0=0x%02x byte1=0x%02x",
                     $time, payload_byte0_comb, payload_byte1_comb);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Struct member comb slice 2 mismatch: byte0=0x%02x byte1=0x%02x",
                     $time, payload_byte0_comb, payload_byte1_comb);
            error_count++;
        end

        @(posedge clk);
        #1;
        if (payload_word_seq === 16'h2233) begin
            $display("[PASS T=%0t] Struct member seq slice 2: word=0x%04x", $time, payload_word_seq);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Struct member seq slice 2 mismatch: word=0x%04x (exp 2233)",
                     $time, payload_word_seq);
            error_count++;
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.80.0.0 REGRESSION SUITE: STRUCT MEMBER SLICE §7.2]");
        if (error_count == 0 && pass_count == 5) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §7.2 Packed Struct Member Slicing.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 5, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule
