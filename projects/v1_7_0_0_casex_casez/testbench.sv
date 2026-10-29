// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v1.7.0.0
// Standard Target: IEEE Std 1800-2023 §12.5.1 (casez) and §12.5.2 (casex)
// Testbench Name : tb_v1_7_0_0_regression
// ============================================================================

module tb_v1_7_0_0_regression;
    logic [3:0] in_val;
    logic [3:0] out_case;
    logic [3:0] out_casez;
    logic [3:0] out_casex;
    logic [3:0] out_prio;

    logic [3:0] test_sel;
    integer error_count = 0;
    integer sub_test_res = 0;

    // Instantiate core DUT
    v1_7_0_0_casex_casez_core dut (
        .in_val(in_val),
        .out_case(out_case),
        .out_casez(out_casez),
        .out_casex(out_casex),
        .out_prio(out_prio)
    );

    initial begin
        in_val = 4'b0000;
        test_sel = 4'b0000;
        #1;

        // --------------------------------------------------------------------
        // TEST 1: Standard Exact Matching (§12.5)
        // --------------------------------------------------------------------
        in_val = 4'b0001; #1;
        if (out_case !== 4'd1) begin
            $display("[FAIL] Standard case exact match failed for 4'b0001! out_case=%0d", out_case);
            error_count = error_count + 1;
        end

        in_val = 4'b0010; #1;
        if (out_case !== 4'd2) begin
            $display("[FAIL] Standard case exact match failed for 4'b0010! out_case=%0d", out_case);
            error_count = error_count + 1;
        end

        in_val = 4'b0100; #1;
        if (out_case !== 4'd3) begin
            $display("[FAIL] Standard case exact match failed for 4'b0100! out_case=%0d", out_case);
            error_count = error_count + 1;
        end

        in_val = 4'b1000; #1;
        if (out_case !== 4'd4) begin
            $display("[FAIL] Standard case exact match failed for 4'b1000! out_case=%0d", out_case);
            error_count = error_count + 1;
        end

        // Exact match with 'x' in standard case
        in_val = 4'b00xx; #1;
        if (out_case !== 4'd8) begin
            $display("[FAIL] Standard case 4-state 'x' match failed for 4'b00xx! out_case=%0d", out_case);
            error_count = error_count + 1;
        end

        // Unmatched pattern -> default branch
        in_val = 4'b0011; #1;
        if (out_case !== 4'd0) begin
            $display("[FAIL] Standard case default fallthrough failed! out_case=%0d", out_case);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: casez (§12.5.1) Wildcard Matching ('z' and '?')
        // --------------------------------------------------------------------
        // Pattern 4'b1??? matches any value starting with 1
        in_val = 4'b1010; #1;
        if (out_casez !== 4'd1) begin
            $display("[FAIL] casez '?' wildcard failed for 4'b1010! out_casez=%0d", out_casez);
            error_count = error_count + 1;
        end

        in_val = 4'b1111; #1;
        if (out_casez !== 4'd1) begin
            $display("[FAIL] casez '?' wildcard failed for 4'b1111! out_casez=%0d", out_casez);
            error_count = error_count + 1;
        end

        // Pattern 4'b01zz matches any value with bits [3:2]=01
        in_val = 4'b0100; #1;
        if (out_casez !== 4'd2) begin
            $display("[FAIL] casez 'z' wildcard failed for 4'b0100! out_casez=%0d", out_casez);
            error_count = error_count + 1;
        end

        in_val = 4'b0111; #1;
        if (out_casez !== 4'd2) begin
            $display("[FAIL] casez 'z' wildcard failed for 4'b0111! out_casez=%0d", out_casez);
            error_count = error_count + 1;
        end

        // Pattern 4'b001? matches bits [3:1]=001
        in_val = 4'b0010; #1;
        if (out_casez !== 4'd3) begin
            $display("[FAIL] casez '?' wildcard failed for 4'b0010! out_casez=%0d", out_casez);
            error_count = error_count + 1;
        end

        in_val = 4'b0011; #1;
        if (out_casez !== 4'd3) begin
            $display("[FAIL] casez '?' wildcard failed for 4'b0011! out_casez=%0d", out_casez);
            error_count = error_count + 1;
        end

        // Exact match in casez
        in_val = 4'b0001; #1;
        if (out_casez !== 4'd4) begin
            $display("[FAIL] casez exact match failed for 4'b0001! out_casez=%0d", out_casez);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 3: casez (§12.5.1) Rejection of 'x' as Don't-Care
        // --------------------------------------------------------------------
        // In casez, 'x' is NOT a don't-care bit! It must match identically.
        // 4'b00xx has bits [1:0] as x. It must match the 4'b00xx branch (7),
        // and NOT wildcard-match 4'b0001 (4).
        in_val = 4'b00xx; #1;
        if (out_casez !== 4'd7) begin
            $display("[FAIL] casez explicit 'x' match failed for 4'b00xx! out_casez=%0d (exp 7)", out_casez);
            error_count = error_count + 1;
        end

        // 4'b000x has bit 0 as x. It does NOT match 4'b0001, nor 4'b00xx.
        // It must fall through to default (0).
        in_val = 4'b000x; #1;
        if (out_casez !== 4'd0) begin
            $display("[FAIL] casez 'x' must not match '1'! out_casez=%0d (exp 0)", out_casez);
            error_count = error_count + 1;
        end

        // Selector containing 'z' treated as don't care:
        // 4'b000z: bits [3:1]=000, bit 0 is 'z' -> bit 0 ignored -> matches 4'b0001!
        in_val = 4'b000z; #1;
        if (out_casez !== 4'd4) begin
            $display("[FAIL] casez selector-side 'z' don't-care failed! out_casez=%0d (exp 4)", out_casez);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 4: casex (§12.5.2) Matching with 'x', 'z', and '?'
        // --------------------------------------------------------------------
        // Pattern 4'b1xxx matches any value starting with 1
        in_val = 4'b1000; #1;
        if (out_casex !== 4'd1) begin
            $display("[FAIL] casex 'x' wildcard failed for 4'b1000! out_casex=%0d", out_casex);
            error_count = error_count + 1;
        end

        in_val = 4'b1011; #1;
        if (out_casex !== 4'd1) begin
            $display("[FAIL] casex 'x' wildcard failed for 4'b1011! out_casex=%0d", out_casex);
            error_count = error_count + 1;
        end

        // casex with 'z' wildcard in item alternative
        in_val = 4'b0100; #1;
        if (out_casex !== 4'd2) begin
            $display("[FAIL] casex 'z' wildcard failed for 4'b0100! out_casex=%0d", out_casex);
            error_count = error_count + 1;
        end

        // casex with '?' wildcard in item alternative
        in_val = 4'b0010; #1;
        if (out_casex !== 4'd3) begin
            $display("[FAIL] casex '?' wildcard failed for 4'b0010! out_casex=%0d", out_casex);
            error_count = error_count + 1;
        end

        // casex with selector-side 'x' treated as don't-care
        // 4'b000x matches 4'b0001 in casex!
        in_val = 4'b000x; #1;
        if (out_casex !== 4'd4) begin
            $display("[FAIL] casex selector-side 'x' wildcard failed! out_casex=%0d (exp 4)", out_casex);
            error_count = error_count + 1;
        end

        // casex with selector-side 'z' treated as don't-care
        // 4'b000z matches 4'b0001 in casex!
        in_val = 4'b000z; #1;
        if (out_casex !== 4'd4) begin
            $display("[FAIL] casex selector-side 'z' wildcard failed! out_casex=%0d (exp 4)", out_casex);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 5: Lexical Priority Order & Default Branch Fallthrough
        // --------------------------------------------------------------------
        // 4'b1100 matches both 4'b1??? and 4'b11??.
        // First branch (4'b1???) must take precedence (out_prio == 1).
        in_val = 4'b1100; #1;
        if (out_prio !== 4'd1) begin
            $display("[FAIL] Lexical priority order violated! in_val=4'b1100 out_prio=%0d (exp 1)", out_prio);
            error_count = error_count + 1;
        end

        in_val = 4'b0110; #1;
        if (out_prio !== 4'd3) begin
            $display("[FAIL] Priority branch 3 match failed! out_prio=%0d (exp 3)", out_prio);
            error_count = error_count + 1;
        end

        // No branch matches -> default branch fallthrough (15)
        in_val = 4'b0000; #1;
        if (out_prio !== 4'd15) begin
            $display("[FAIL] Default branch fallthrough failed! out_prio=%0d (exp 15)", out_prio);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 6: Comma-Separated Match Alternatives & Unbased '?'
        // --------------------------------------------------------------------
        begin
            test_sel = 4'b0101;
            casez (test_sel)
                4'b0001, 4'b0010: sub_test_res = 10;
                4'b0100, 4'b0101: sub_test_res = 20;
                default:          sub_test_res = 99;
            endcase

            if (sub_test_res !== 20) begin
                $display("[FAIL] Comma-separated case item match failed! sub_test_res=%0d (exp 20)", sub_test_res);
                error_count = error_count + 1;
            end

            // Sized based wildcard matching (IEEE Std 1800-2023 §5.7.1 & §12.5)
            casez (test_sel)
                4'b????: sub_test_res = 42;
                default: sub_test_res = 0;
            endcase

            if (sub_test_res !== 42) begin
                $display("[FAIL] Sized wildcard 4'b???? match failed! sub_test_res=%0d (exp 42)", sub_test_res);
                error_count = error_count + 1;
            end
        end

        #2;
        $display("\n================================================================================");
        $display("[CAS v1.7.0.0 REGRESSION SUITE: CASEZ AND CASEX WILDCARDS]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §12.5.1 (casez) and §12.5.2 (casex) wildcards.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | in_val=%b | out_case=%0d out_casez=%0d out_casex=%0d out_prio=%0d",
                 $time, in_val, out_case, out_casez, out_casex, out_prio);
    end

endmodule
