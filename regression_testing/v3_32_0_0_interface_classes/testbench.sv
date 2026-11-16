// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.32.0.0
// Standard Target: IEEE Std 1800-2023 §8.26 (Interface Classes)
// Testbench Name : tb_v3_32_0_0_regression
// ============================================================================

module tb_v3_32_0_0_regression;
    int pass_count  = 0;
    int error_count = 0;

    initial begin
        eth_packet pkt;
        i_checksum chk_h;
        i_inspectable insp_h;
        string info;

        #10;
        // Construct concrete class
        pkt = new(101, 4);

        // --------------------------------------------------------------------
        // Test 1: Polymorphic assignment to first interface handle (§8.26.2)
        // --------------------------------------------------------------------
        chk_h = pkt;
        if (chk_h.is_valid() !== 1'b1) begin
            $display("[FAIL T=%0t] Interface handle chk_h.is_valid() mismatch! got %b (exp 1)",
                     $time, chk_h.is_valid());
            error_count++;
        end else begin
            $display("[PASS T=%0t] Interface handle i_checksum polymorphically invoked: valid=%b crc=%04h",
                     $time, chk_h.is_valid(), chk_h.compute_crc());
            pass_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: Polymorphic assignment to second interface handle (§8.26.2)
        // --------------------------------------------------------------------
        insp_h = pkt;
        insp_h.format_header(info);
        if (info != "ETH_PKT ID=101 LEN=4 CRC=ffff") begin
            $display("[FAIL T=%0t] Interface handle insp_h.format_header() mismatch! got '%s'",
                     $time, info);
            error_count++;
        end else begin
            $display("[PASS T=%0t] Interface handle i_inspectable: '%s'", $time, info);
            pass_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Cross-casting between interfaces ($cast across contracts §8.26.5)
        // From i_checksum handle chk_h -> i_inspectable handle cross_insp_h
        // --------------------------------------------------------------------
        begin
            i_inspectable cross_insp_h;
            string cross_info;
            int cast_ok;

            cast_ok = $cast(cross_insp_h, chk_h);
            if (!cast_ok) begin
                $display("[FAIL T=%0t] $cast from i_checksum to i_inspectable returned 0", $time);
                error_count++;
            end else begin
                cross_insp_h.format_header(cross_info);
                if (cross_info == info) begin
                    $display("[PASS T=%0t] Cross-cast between interface handles successful: '%s'",
                             $time, cross_info);
                    pass_count++;
                end else begin
                    $display("[FAIL T=%0t] Cross-cast output mismatch! got '%s' (exp '%s')",
                             $time, cross_info, info);
                    error_count++;
                end
            end
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.32.0.0 REGRESSION SUITE: INTERFACE CLASSES]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §8.26 interface classes, multiple implements, and cross-casting.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule