// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v4.6.0.0
// Standard Target: IEEE Std 1800-2023 §35.5.4 & §35.5.5 (Strings & Chandles)
// Testbench Name : tb_v4_6_0_0_strings_chandles
// ============================================================================

module tb_v4_6_0_0_strings_chandles;
    // DPI-C Imports
    import "DPI-C" function chandle c_alloc_payload(input int tag, input int val);
    import "DPI-C" function int c_read_payload_tag(input chandle handle);
    import "DPI-C" function int c_read_payload_val(input chandle handle);
    import "DPI-C" function void c_free_payload(input chandle handle);

    import "DPI-C" function int c_string_length(input string str);
    import "DPI-C" function string c_prefix_string(input string prefix, input string name);

    chandle h1;
    chandle h2;
    int tag1;
    int val1;
    int tag2;
    int val2;
    int len;
    string msg;
    int error_count = 0;

    initial begin
        // Test 1: Allocate opaque chandles
        h1 = c_alloc_payload(32'hA1, 100);
        h2 = c_alloc_payload(32'hB2, 200);

        if (h1 == null || h2 == null || h1 == h2) begin
            $display("[FAIL] Chandle allocation failed: h1=%p h2=%p", h1, h2);
            error_count = error_count + 1;
        end

        // Test 2: Read payload data via chandles
        tag1 = c_read_payload_tag(h1);
        val1 = c_read_payload_val(h1);
        tag2 = c_read_payload_tag(h2);
        val2 = c_read_payload_val(h2);

        if (tag1 !== 32'hA1 || val1 !== 100) begin
            $display("[FAIL] Payload 1 mismatch: tag=%h val=%0d", tag1, val1);
            error_count = error_count + 1;
        end
        if (tag2 !== 32'hB2 || val2 !== 200) begin
            $display("[FAIL] Payload 2 mismatch: tag=%h val=%0d", tag2, val2);
            error_count = error_count + 1;
        end

        c_free_payload(h1);
        c_free_payload(h2);

        // Test 3: String Length
        len = c_string_length("Hello SystemVerilog DPI-C!");
        if (len !== 26) begin
            $display("[FAIL] c_string_length returned %0d (exp 26)", len);
            error_count = error_count + 1;
        end

        // Test 4: String Return
        msg = c_prefix_string("CAS_TEST", "SUCCESS");
        if (msg != "[CAS_TEST] SUCCESS") begin
            $display("[FAIL] c_prefix_string returned '%s' (exp '[CAS_TEST] SUCCESS')", msg);
            error_count = error_count + 1;
        end

        #5;
        $display("\n================================================================================");
        $display("[CAS v4.6.0.0 REGRESSION SUITE: DPI-C STRINGS & CHANDLES]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §35 DPI-C chandle handles and string marshaling.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule
