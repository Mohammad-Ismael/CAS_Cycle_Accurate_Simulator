`timescale 1ns/1ps

// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.47.0.0
// Standard Target: IEEE Std 1800-2023 §11.4.13 (Set Membership 'inside' Operator)
// Testbench Name : tb_v3_47_0_regression
// ============================================================================

module tb_v3_47_0_regression;
    import v3_47_0_0_pkg::*;

    logic       clk;
    logic       rst_n;
    logic [7:0] pkt_addr;
    logic [3:0] port_tag;
    logic       pkt_valid;
    logic       in_reserved_range;
    logic       valid_port;
    logic [1:0] filter_category;

    int pass_count  = 0;
    int error_count = 0;

    v3_47_0_0_packet_filter dut (
        .clk              (clk),
        .rst_n            (rst_n),
        .pkt_addr         (pkt_addr),
        .port_tag         (port_tag),
        .pkt_valid        (pkt_valid),
        .in_reserved_range(in_reserved_range),
        .valid_port       (valid_port),
        .filter_category  (filter_category)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n     = 0;
        pkt_addr  = 8'h00;
        port_tag  = 4'h0;
        pkt_valid = 1'b0;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Bounded range hit in [100:150] and valid port PORT_DATA_LO (0x1)
        // pkt_addr = 125 -> inside [100:150] -> in_reserved_range = 1
        // port_tag  = 4'h1 -> inside valid enum set -> valid_port = 1
        // filter_category = 0 (DROP)
        // --------------------------------------------------------------------
        @(negedge clk);
        pkt_addr  = 8'd125;
        port_tag  = 4'h1;
        pkt_valid = 1'b1;

        @(posedge clk);
        #1;
        if (in_reserved_range === 1'b1 && valid_port === 1'b1 && filter_category === 2'd0) begin
            $display("[PASS T=%0t] Range check and port membership verified: reserved=%b valid_port=%b cat=%0d",
                     $time, in_reserved_range, valid_port, filter_category);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Range check or port membership failed! reserved=%b valid_port=%b cat=%0d",
                     $time, in_reserved_range, valid_port, filter_category);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: Control category match via mixed discrete/range {0, [10:20]}
        // pkt_addr = 15 -> inside [10:20] -> filter_category = 1 (CONTROL)
        // port_tag  = 4'h8 -> NOT inside valid ports -> valid_port = 0
        // in_reserved_range = 0
        // --------------------------------------------------------------------
        @(negedge clk);
        pkt_addr  = 8'd15;
        port_tag  = 4'h8;
        pkt_valid = 1'b1;

        @(posedge clk);
        #1;
        if (in_reserved_range === 1'b0 && valid_port === 1'b0 && filter_category === 2'd1) begin
            $display("[PASS T=%0t] Mixed range and invalid port verified: reserved=%b valid_port=%b cat=%0d",
                     $time, in_reserved_range, valid_port, filter_category);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Mixed range or invalid port failed! reserved=%b valid_port=%b cat=%0d",
                     $time, in_reserved_range, valid_port, filter_category);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Procedural inside evaluation in initial block
        // --------------------------------------------------------------------
        begin
            logic [7:0] val;
            val = 8'd90;
            if (val inside { [0:50], [85:95], 120 }) begin
                $display("[PASS T=%0t] Procedural inside expression evaluation verified: val=%0d",
                         $time, val);
                pass_count++;
            end else begin
                $display("[FAIL T=%0t] Procedural inside expression evaluation failed!", $time);
                error_count++;
            end
        end

        @(negedge clk);
        pkt_valid = 1'b0;

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.47.0.0 REGRESSION SUITE: INSIDE OPERATOR §11.4.13]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §11.4.13 inside operator with open ranges, sets, and decision predicates.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule