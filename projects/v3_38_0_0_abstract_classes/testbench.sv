// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.38.0.0
// Standard Target: IEEE Std 1800-2023 §8.21 (Abstract Classes & Pure Virtuals)
// Testbench Name : tb_v3_38_0_0_regression
// ============================================================================

module tb_v3_38_0_0_regression;
    int pass_count  = 0;
    int error_count = 0;

    initial begin
        base_driver drivers[2];
        axi_driver  axi_inst;
        apb_driver  apb_inst;
        int pkt_axi, pkt_apb;

        #10;
        // Construct concrete derived instances
        axi_inst = new("AXI_DRV", 32'h0000_0100);
        apb_inst = new("APB_DRV");

        // Polymorphic assignment to abstract base handles (§8.21)
        drivers[0] = axi_inst;
        drivers[1] = apb_inst;

        // --------------------------------------------------------------------
        // Test 1: Polymorphic dispatch through base handle to AXI driver
        // raw = 32'h0000_0005, offset = 32'h0000_0100
        // encode = 32'h0000_0105
        // send_packet = 32'h0000_0105 ^ 32'hAAAA_0000 = 32'hAAAA_0105
        // --------------------------------------------------------------------
        drivers[0].send_packet(32'h0000_0005, pkt_axi);
        if (pkt_axi === 32'hAAAA_0105 && drivers[0].trans_count === 1) begin
            $display("[PASS T=%0t] Abstract handle dispatch to AXI driver: pkt=%08h count=%0d",
                     $time, pkt_axi, drivers[0].trans_count);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Abstract handle dispatch to AXI driver mismatch! got %08h (exp AAAA0105)",
                     $time, pkt_axi);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: Polymorphic dispatch through base handle to APB driver
        // raw = 32'h0000_0008
        // encode = 8 << 2 = 32'h0000_0020
        // send_packet = 32'h0000_0020 | 32'h0000_5555 = 32'h0000_5575
        // --------------------------------------------------------------------
        drivers[1].send_packet(32'h0000_0008, pkt_apb);
        if (pkt_apb === 32'h0000_5575 && drivers[1].trans_count === 1) begin
            $display("[PASS T=%0t] Abstract handle dispatch to APB driver: pkt=%08h count=%0d",
                     $time, pkt_apb, drivers[1].trans_count);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Abstract handle dispatch to APB driver mismatch! got %08h (exp 00005575)",
                     $time, pkt_apb);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Pure virtual function call directly on base reference
        // encode_payload(32'h10) on APB -> 16 << 2 = 64 (32'h0000_0040)
        // --------------------------------------------------------------------
        if (drivers[1].encode_payload(32'h0000_0010) === 32'h0000_0040) begin
            $display("[PASS T=%0t] Pure virtual function dispatch verified: res=%08h",
                     $time, drivers[1].encode_payload(32'h0000_0010));
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Pure virtual function dispatch mismatch! got %08h (exp 00000040)",
                     $time, drivers[1].encode_payload(32'h0000_0010));
            error_count++;
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.38.0.0 REGRESSION SUITE: ABSTRACT CLASSES & PURE VIRTUALS §8.21]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §8.21 abstract virtual classes, pure virtual methods, and polymorphic base dispatch.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule