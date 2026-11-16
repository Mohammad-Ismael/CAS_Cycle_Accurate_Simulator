// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.18.0.0
// Standard Target: IEEE Std 1800-2023 §26 (package, import pkg::*, pkg::item)
// Testbench Name : tb_v3_18_0_0_regression
// ============================================================================

module tb_v3_18_0_0_regression;
    // Explicit and Wildcard package imports inside testbench scope (§26.3)
    import bus_proto_pkg::DEFAULT_BURST;
    import bus_proto_pkg::*;

    logic        clk;
    logic        rst_n;
    logic [1:0]  cmd_in;
    logic [15:0] addr_in;
    logic [31:0] tag_out;
    logic        valid_out;

    // Explicit scope resolution without prior import (§26.3)
    bus_proto_pkg::bus_hdr_s pkt_hdr;
    int error_count = 0;

    v3_18_0_0_package_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .cmd_in(cmd_in),
        .addr_in(addr_in),
        .tag_out(tag_out),
        .valid_out(valid_out)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n   = 0;
        cmd_in  = 2'b11; // CMD_IDLE
        addr_in = 16'h0000;

        #5;
        rst_n = 1;

        // --------------------------------------------------------------------
        // TEST 1: Package Parameter verification
        // --------------------------------------------------------------------
        #5; // T = 10
        if (DEFAULT_BURST !== 4 || bus_proto_pkg::ADDR_MASK !== 32'h0000FFFF) begin
            $display("[FAIL T=10] Package parameter mismatch! burst=%0d mask=%h",
                     DEFAULT_BURST, bus_proto_pkg::ADDR_MASK);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Package Function & Enum execution (CMD_READ = 2'b00, addr = 16'h8020)
        // Expected tag: {14'h0000, 2'b00, 16'h8020} = 32'h00008020, valid = 1
        // --------------------------------------------------------------------
        cmd_in  = bus_proto_pkg::CMD_READ;
        addr_in = 16'h8020;
        #5; // T = 15

        if (tag_out !== 32'h00008020 || valid_out !== 1'b1) begin
            $display("[FAIL T=15] CMD_READ tag mismatch! tag=%h valid=%b (exp 00008020, 1)",
                     tag_out, valid_out);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 3: Package Enum & Function execution (CMD_WRITE = 2'b01, addr = 16'hA004)
        // Expected tag: {14'h0000, 2'b01, 16'hA004} = 32'h0001A004, valid = 1
        // --------------------------------------------------------------------
        cmd_in  = CMD_WRITE; // from wildcard import
        addr_in = 16'hA004;
        #5; // T = 20

        if (tag_out !== 32'h0001A004 || valid_out !== 1'b1) begin
            $display("[FAIL T=20] CMD_WRITE tag mismatch! tag=%h valid=%b (exp 0001A004, 1)",
                     tag_out, valid_out);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 4: Package Packed Structure assignment & field access
        // --------------------------------------------------------------------
        pkt_hdr.cmd      = CMD_WRITE;
        pkt_hdr.reserved = 14'h0000;
        pkt_hdr.addr     = 16'h1234;
        #5; // T = 25

        if (pkt_hdr.cmd !== CMD_WRITE || pkt_hdr.addr !== 16'h1234) begin
            $display("[FAIL T=25] Packed struct field mismatch! cmd=%b addr=%h",
                     pkt_hdr.cmd, pkt_hdr.addr);
            error_count = error_count + 1;
        end

        #5; // T = 30
        $display("\n================================================================================");
        $display("[CAS v3.18.0.0 REGRESSION SUITE: SYSTEMVERILOG PACKAGES]");
        if (error_count == 0 && tag_out == 32'h0001A004 && pkt_hdr.addr == 16'h1234) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §26 packages, wildcard imports, explicit imports, and scope resolution.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | cmd=%b addr=%h | tag=%h val=%b | hdr_addr=%h",
                 $time, cmd_in, addr_in, tag_out, valid_out, pkt_hdr.addr);
    end
endmodule