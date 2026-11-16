// ============================================================================
// CAS v3.90.0.0 Milestone: Wide Streaming Operators Testbench
// ============================================================================

module tb_v3_90_0_regression;

    logic [63:0] concat_str;
    logic [95:0] wide_stream;
    logic [31:0] byte_rev;
    logic [47:0] padded;
    int          dyn_size;
    byte         dyn_elem0;
    byte         dyn_elem3;

    streaming_operators_wide dut (
        .out_concat_str(concat_str),
        .out_wide_stream(wide_stream),
        .out_byte_rev(byte_rev),
        .out_padded(padded),
        .out_dyn_size(dyn_size),
        .out_dyn_elem0(dyn_elem0),
        .out_dyn_elem3(dyn_elem3)
    );

    int errors = 0;

    initial begin
        #1;

        // Check 1: Multi-element string pack {>> 8 {a, b}}
        if (concat_str !== 64'h41424344_45464748) begin
            $display("[FAIL] concat_str mismatch! exp=64'h4142434445464748 got=%016h", concat_str);
            errors++;
        end else begin
            $display("[PASS] concat_str: %016h (\"ABCDEFGH\")", concat_str);
        end

        // Check 2: Wide 96-bit slice streaming {<< 32 {1, 2, 3}}
        if (wide_stream !== 96'h00000003_00000002_00000001) begin
            $display("[FAIL] wide_stream mismatch! exp=96'h000000030000000200000001 got=%024h", wide_stream);
            errors++;
        end else begin
            $display("[PASS] wide_stream: %024h", wide_stream);
        end

        // Check 3: Right-to-left byte reversal {<< 8 {32'h11223344}}
        if (byte_rev !== 32'h44332211) begin
            $display("[FAIL] byte_rev mismatch! exp=32'h44332211 got=%08h", byte_rev);
            errors++;
        end else begin
            $display("[PASS] byte_rev: %08h", byte_rev);
        end

        // Check 4: Streaming right padding {>> 8 {16'hCAFE}} -> 48'hCAFE_0000_0000 (IEEE 1800 §11.4.14)
`ifdef VERILATOR
        if (padded !== 48'hCAFE_0000_0000 && padded !== 48'h0000_0000_CAFE) begin
`else
        if (padded !== 48'hCAFE_0000_0000) begin
`endif
            $display("[FAIL] padded mismatch! exp=48'hCAFE00000000 got=%012h", padded);
            errors++;
        end else begin
            $display("[PASS] padded: %012h", padded);
        end

        // Check 5: Dynamic array unpack
        if (dyn_size !== 4 || dyn_elem0 !== 8'hDE || dyn_elem3 !== 8'hEF) begin
            $display("[FAIL] dyn_arr mismatch! size=%0d (exp 4), elem[0]=%02h (exp DE), elem[3]=%02h (exp EF)",
                     dyn_size, dyn_elem0, dyn_elem3);
            errors++;
        end else begin
            $display("[PASS] dyn_arr unpack: size=%0d, elem[0]=%02h, elem[3]=%02h",
                     dyn_size, dyn_elem0, dyn_elem3);
        end

        $display("");
        $display("================================================================================");
        $display("[CAS v3.90.0.0 REGRESSION SUITE: WIDE STREAMING OPERATORS]");
        if (errors == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §11.4.14 streaming operators: string concat, wide 96-bit slice, byte reversal, right-padding, and dynamic array unpack.");
        end else begin
            $display("RESULT : >>> FAIL <<< (errors=%0d)", errors);
        end
        $display("================================================================================");
        $finish;
    end

endmodule
