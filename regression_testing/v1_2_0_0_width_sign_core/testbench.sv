// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v1.2.0.0
// Standard Target: IEEE Std 1800-2023 Chapter 11 (Width & Sign Semantics)
// Testbench Name : tb_v1_2_0_0_regression
// ============================================================================

module tb_v1_2_0_0_regression;
    logic [3:0]        u_a;
    logic [3:0]        u_b;
    logic signed [3:0] s_a;
    logic signed [3:0] s_b;
    logic [1:0]        sh_amt;

    logic [7:0]        out_context_add;
    logic [3:0]        out_self_add;
    logic signed [7:0] out_signed_ext;
    logic signed [7:0] out_signed_add;
    logic signed [7:0] out_signed_ashr;
    logic [7:0]        out_unsigned_ashr;

    integer error_count = 0;

    v1_2_0_0_width_sign_core dut (
        .u_a(u_a), .u_b(u_b),
        .s_a(s_a), .s_b(s_b),
        .sh_amt(sh_amt),
        .out_context_add(out_context_add),
        .out_self_add(out_self_add),
        .out_signed_ext(out_signed_ext),
        .out_signed_add(out_signed_add),
        .out_signed_ashr(out_signed_ashr),
        .out_unsigned_ashr(out_unsigned_ashr)
    );

    initial begin
        // --------------------------------------------------------------------
        // Test 1: Context-Determined Width Addition (§11.6.1)
        // u_a = 4'hF (15), u_b = 4'h1 (1)
        // 4-bit self addition: (15 + 1) = 16 -> 4'h0 (truncated)
        // 8-bit context addition: (15 + 1) -> 8'h10 (preserved)
        // --------------------------------------------------------------------
        u_a = 4'hF;
        u_b = 4'h1;
        s_a = -4'sd2; // 4'b1110 (-2 in 2's complement)
        s_b = -4'sd3; // 4'b1101 (-3 in 2's complement)
        sh_amt = 2'd1;
        #5;

        if (out_context_add !== 8'h10) begin
            $display("[FAIL] Context-determined addition mismatch! Expected 8'h10, Got %h", out_context_add);
            error_count = error_count + 1;
        end

        if (out_self_add !== 4'h0) begin
            $display("[FAIL] Self-determined addition mismatch! Expected 4'h0, Got %h", out_self_add);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // Test 2: Signed Extension and Signed Arithmetic (§11.8.1)
        // s_a = -2 (4'b1110)
        // 8-bit sign-extended -> 8'b1111_1110 (8'hFE / -2)
        // s_a + s_b = (-2) + (-3) = -5 -> 8'b1111_1011 (8'hFB / -5)
        // --------------------------------------------------------------------
        if (out_signed_ext !== 8'hFE) begin
            $display("[FAIL] Signed extension mismatch! Expected 8'hFE, Got %h", out_signed_ext);
            error_count = error_count + 1;
        end

        if (out_signed_add !== 8'hFB) begin
            $display("[FAIL] Signed addition mismatch! Expected 8'hFB, Got %h", out_signed_add);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // Test 3: Arithmetic Shift Right (>>>) Signed vs Unsigned (§11.4.10)
        // s_a = 4'b1110 (-2), sh_amt = 1 -> arithmetic shift preserves sign bit:
        // Result widened to 8-bit -> 8'b1111_1111 (8'hFF / -1)
        // u_a = 4'b1111 (15), unsigned >>> 1 -> 4'b0111, zero-extended -> 8'h07
        // --------------------------------------------------------------------
        if (out_signed_ashr !== 8'hFF) begin
            $display("[FAIL] Signed arithmetic shift (>>>) failed! Expected 8'hFF, Got %h", out_signed_ashr);
            error_count = error_count + 1;
        end

        if (out_unsigned_ashr !== 8'h07) begin
            $display("[FAIL] Unsigned arithmetic shift (>>>) failed! Expected 8'h07, Got %h", out_unsigned_ashr);
            error_count = error_count + 1;
        end

        #5;
        $display("\n================================================================================");
        $display("[CAS v1.2.0.0 REGRESSION SUITE: EXPRESSION WIDTH & SIGN EXTENSION]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Context-determined width propagation and 2's complement sign extension verified.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | u_a=%h u_b=%h | CTX_ADD=%h SELF_ADD=%h | S_EXT=%h S_ADD=%h | S_ASHR=%h U_ASHR=%h",
                 $time, u_a, u_b, out_context_add, out_self_add, out_signed_ext, out_signed_add, out_signed_ashr, out_unsigned_ashr);
    end

endmodule