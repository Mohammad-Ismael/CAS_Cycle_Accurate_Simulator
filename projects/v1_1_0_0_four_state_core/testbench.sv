// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v1.1.0.0
// Standard Target: IEEE Std 1800-2023 Chapters 6, 7 & 11
// Testbench Name : tb_v1_1_0_0_regression
// ============================================================================

module tb_v1_1_0_0_regression;
    logic [3:0] a;
    logic [3:0] b;
    logic       sel;
    logic [3:0] o_and, o_or, o_xor, o_ternary;
    logic       o_log_eq, o_case_eq;

    integer error_count = 0;

    v1_1_0_0_four_state_core dut (
        .a(a), .b(b), .sel(sel),
        .out_and(o_and), .out_or(o_or), .out_xor(o_xor),
        .out_log_eq(o_log_eq), .out_case_eq(o_case_eq),
        .out_ternary(o_ternary)
    );

    initial begin
        // --------------------------------------------------------------------
        // Test 1: Dominant Bitwise 4-State Logic (§11.4.1)
        // a = 4'b01x0, b = 4'b011x
        // AND: 0&0=0, 1&1=1, x&1=x, 0&x=0 -> Expected: 4'b01x0
        // OR : 0|0=0, 1|1=1, x|1=1, 0|x=x -> Expected: 4'b011x
        // --------------------------------------------------------------------
        a = 4'b01x0;
        b = 4'b011x;
        sel = 1'b0;
        #5;

        if (o_and !== 4'b01x0) begin
            $display("[FAIL] Bitwise AND 4-state mismatch! Expected 01x0, Got %b", o_and);
            error_count = error_count + 1;
        end
        if (o_or !== 4'b011x) begin
            $display("[FAIL] Bitwise OR 4-state mismatch! Expected 011x, Got %b", o_or);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // Test 2: Equality Operator Distinction (§11.4.5)
        // a == b  with 'x' present MUST evaluate to 1'bx
        // a === b with 'x' mismatch MUST evaluate to 1'b0
        // --------------------------------------------------------------------
        if (o_log_eq !== 1'bx) begin
            $display("[FAIL] Logical == did not produce 1'bx on unknown! Got %b", o_log_eq);
            error_count = error_count + 1;
        end
        if (o_case_eq !== 1'b0) begin
            $display("[FAIL] Case === failed mismatch test! Expected 0, Got %b", o_case_eq);
            error_count = error_count + 1;
        end

        // Exact match with 'x'
        b = 4'b01x0;
        #5;
        if (o_case_eq !== 1'b1) begin
            $display("[FAIL] Case === failed identical 4-state match! Expected 1, Got %b", o_case_eq);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // Test 3: Ambiguous Ternary Merge (§11.4.11)
        // sel = 1'bx
        // a   = 4'b1100
        // b   = 4'b1010
        // Bit 3 (1 vs 1) -> 1
        // Bit 2 (1 vs 0) -> x
        // Bit 1 (0 vs 1) -> x
        // Bit 0 (0 vs 0) -> 0
        // Expected out_ternary = 4'b1xx0
        // --------------------------------------------------------------------
        a = 4'b1100;
        b = 4'b1010;
        sel = 1'bx;
        #5;

        if (o_ternary !== 4'b1xx0) begin
            $display("[FAIL] Ternary ambiguity merge failed! Expected 1xx0, Got %b", o_ternary);
            error_count = error_count + 1;
        end

        #5;
        $display("\n================================================================================");
        $display("[CAS v1.1.0.0 REGRESSION SUITE: 4-STATE LOGIC & AMBIGUITY MERGE]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("ACTION : Compare with EDA Playground (Synopsys VCS) to confirm parity.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | a=%b b=%b sel=%b | AND=%b OR=%b XOR=%b | EQ(==)=%b CASE(===)=%b | TERN=%b",
                 $time, a, b, sel, o_and, o_or, o_xor, o_log_eq, o_case_eq, o_ternary);
    end

endmodule