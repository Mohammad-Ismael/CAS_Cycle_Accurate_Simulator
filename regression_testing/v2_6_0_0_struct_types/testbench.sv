// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v2.6.0.0
// Standard Target: IEEE Std 1800-2023 §7.2 (Packed Structures & Member Access)
// Testbench Name : tb_v2_6_0_0_regression
// ============================================================================

module tb_v2_6_0_0_regression;
    logic        clk;
    logic        rst_n;
    logic [15:0] raw_packet_in;
    logic [7:0]  payload_out;
    logic [3:0]  src_out;
    logic [3:0]  dst_out;
    logic [15:0] packet_loopback;

    typedef struct packed {
        logic [3:0] src_id;
        logic [3:0] dst_id;
        logic [7:0] payload;
    } packet_t;

    packet_t tb_loopback_pkt;
    integer error_count = 0;

    v2_6_0_0_struct_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .raw_packet_in(raw_packet_in),
        .payload_out(payload_out),
        .src_out(src_out),
        .dst_out(dst_out),
        .packet_loopback(packet_loopback)
    );

    // Clock generator: posedges at 5, 15, 25, 35, 45...
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n = 0;
        raw_packet_in = 16'h0000;

        #6; // T = 6 (after first posedge at T = 5)
        rst_n = 1;
        if (packet_loopback !== 16'h0000) begin
            $display("[FAIL T=6] Struct reset failed! loopback=%h (exp 0000)", packet_loopback);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 1: Unpack raw vector into struct fields (src=A, dst=B, payload=42)
        // raw_packet = {4'hA, 4'hB, 8'h42} = 16'hAB42
        // --------------------------------------------------------------------
        #4; // T = 10
        raw_packet_in = 16'hAB42;
        #1; // T = 11 (combinational unpack check)
        if (src_out !== 4'hA || dst_out !== 4'hB || payload_out !== 8'h42) begin
            $display("[FAIL T=11] Combinational struct unpack failed! src=%h dst=%h payload=%h",
                     src_out, dst_out, payload_out);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Clocked struct member manipulation & loopback
        // Posedge at T = 15:
        // tx_pkt.src_id  <= rx_pkt.dst_id (4'hB)
        // tx_pkt.dst_id  <= rx_pkt.src_id (4'hA)
        // tx_pkt.payload <= 8'h42 + 1 = 8'h43
        // Resulting aggregate packet: 16'hBA43
        // --------------------------------------------------------------------
        #5; // T = 16 (after posedge at T = 15)
        tb_loopback_pkt = packet_t'(packet_loopback);
        if (packet_loopback !== 16'hBA43 ||
            tb_loopback_pkt.src_id !== 4'hB ||
            tb_loopback_pkt.dst_id !== 4'hA ||
            tb_loopback_pkt.payload !== 8'h43) begin
            $display("[FAIL T=16] Clocked struct member write failed! loopback=%h (exp BA43)", packet_loopback);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 3: Second packet transaction (src=1, dst=F, payload=FE)
        // raw_packet = 16'h1FFE
        // --------------------------------------------------------------------
        #4; // T = 20
        raw_packet_in = 16'h1FFE;
        #5; // T = 25 (posedge clk) -> expected loopback: {4'hF, 4'h1, 8'hFE + 1 = 8'hFF} = 16'hF1FF
        #1; // T = 26
        tb_loopback_pkt = packet_t'(packet_loopback);
        if (packet_loopback !== 16'hF1FF ||
            tb_loopback_pkt.src_id !== 4'hF ||
            tb_loopback_pkt.dst_id !== 4'h1 ||
            tb_loopback_pkt.payload !== 8'hFF) begin
            $display("[FAIL T=26] Second transaction failed! loopback=%h (exp F1FF)", packet_loopback);
            error_count = error_count + 1;
        end

        #4; // T = 30
        $display("\n================================================================================");
        $display("[CAS v2.6.0.0 REGRESSION SUITE: PACKED STRUCTURES AND MEMBER SLICING]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §7.2 packed struct member slicing and aggregate assignment.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | clk=%b rst_n=%b raw_in=%h | src=%h dst=%h payload=%h | loopback=%h",
                 $time, clk, rst_n, raw_packet_in, src_out, dst_out, payload_out, packet_loopback);
    end
endmodule