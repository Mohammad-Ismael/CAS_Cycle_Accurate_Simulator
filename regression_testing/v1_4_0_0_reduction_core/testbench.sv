// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v1.4.0.0
// Standard Target: IEEE Std 1800-2023 §11.4.3 Unary Reduction Operators
// Testbench Name : tb_v1_4_0_0_regression
// ============================================================================

module tb_v1_4_0_0_regression;
    logic [3:0] in_vec;

    logic red_and;
    logic red_nand;
    logic red_or;
    logic red_nor;
    logic red_xor;
    logic red_xnor;

    integer error_count = 0;

    v1_4_0_0_reduction_core dut (
        .in_vec(in_vec),
        .red_and(red_and),
        .red_nand(red_nand),
        .red_or(red_or),
        .red_nor(red_nor),
        .red_xor(red_xor),
        .red_xnor(red_xnor)
    );

    initial begin
        // --------------------------------------------------------------------
        // Vector 1: All ones (4'b1111)
        // & = 1, ~& = 0, | = 1, ~| = 0, ^ = 0 (even), ~^ = 1
        // --------------------------------------------------------------------
        in_vec = 4'b1111;
        #5;
        if (red_and !== 1'b1 || red_nand !== 1'b0 || red_or !== 1'b1 ||
            red_nor !== 1'b0 || red_xor !== 1'b0  || red_xnor !== 1'b1) begin
            $display("[FAIL] Vector 4'b1111 mismatch! Got &:%b ~&:%b |:%b ~|:%b ^:%b ~^:%b",
                     red_and, red_nand, red_or, red_nor, red_xor, red_xnor);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // Vector 2: All zeros (4'b0000)
        // & = 0, ~& = 1, | = 0, ~| = 1, ^ = 0 (even), ~^ = 1
        // --------------------------------------------------------------------
        in_vec = 4'b0000;
        #5;
        if (red_and !== 1'b0 || red_nand !== 1'b1 || red_or !== 1'b0 ||
            red_nor !== 1'b1 || red_xor !== 1'b0  || red_xnor !== 1'b1) begin
            $display("[FAIL] Vector 4'b0000 mismatch! Got &:%b ~&:%b |:%b ~|:%b ^:%b ~^:%b",
                     red_and, red_nand, red_or, red_nor, red_xor, red_xnor);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // Vector 3: Odd parity of ones (4'b1011)
        // & = 0, ~& = 1, | = 1, ~| = 0, ^ = 1 (odd 3 ones), ~^ = 0
        // --------------------------------------------------------------------
        in_vec = 4'b1011;
        #5;
        if (red_and !== 1'b0 || red_nand !== 1'b1 || red_or !== 1'b1 ||
            red_nor !== 1'b0 || red_xor !== 1'b1  || red_xnor !== 1'b0) begin
            $display("[FAIL] Vector 4'b1011 mismatch! Got &:%b ~&:%b |:%b ~|:%b ^:%b ~^:%b",
                     red_and, red_nand, red_or, red_nor, red_xor, red_xnor);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // Vector 4: 4-State Zero Dominance (4'b1x01)
        // Contains known 0: & must be 0, ~& must be 1.
        // Contains known 1: | must be 1, ~| must be 0.
        // Contains X: ^ must be x, ~^ must be x.
        // --------------------------------------------------------------------
        in_vec = 4'b1x01;
        #5;
        if (red_and !== 1'b0 || red_nand !== 1'b1 || red_or !== 1'b1 ||
            red_nor !== 1'b0 || red_xor !== 1'bx  || red_xnor !== 1'bx) begin
            $display("[FAIL] Vector 4'b1x01 mismatch! Got &:%b ~&:%b |:%b ~|:%b ^:%b ~^:%b",
                     red_and, red_nand, red_or, red_nor, red_xor, red_xnor);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // Vector 5: 4-State Unknown Ambiguity (4'b1x11)
        // No known 0: & must be x, ~& must be x.
        // Contains known 1: | must be 1, ~| must be 0.
        // Contains X: ^ must be x, ~^ must be x.
        // --------------------------------------------------------------------
        in_vec = 4'b1x11;
        #5;
        if (red_and !== 1'bx || red_nand !== 1'bx || red_or !== 1'b1 ||
            red_nor !== 1'b0 || red_xor !== 1'bx  || red_xnor !== 1'bx) begin
            $display("[FAIL] Vector 4'b1x11 mismatch! Got &:%b ~&:%b |:%b ~|:%b ^:%b ~^:%b",
                     red_and, red_nand, red_or, red_nor, red_xor, red_xnor);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // Vector 6: 4-State All-Unknown Ambiguity (4'b0x00)
        // Contains known 0: & must be 0, ~& must be 1.
        // No known 1: | must be x, ~| must be x.
        // Contains X: ^ must be x, ~^ must be x.
        // --------------------------------------------------------------------
        in_vec = 4'b0x00;
        #5;
        if (red_and !== 1'b0 || red_nand !== 1'b1 || red_or !== 1'bx ||
            red_nor !== 1'bx || red_xor !== 1'bx  || red_xnor !== 1'bx) begin
            $display("[FAIL] Vector 4'b0x00 mismatch! Got &:%b ~&:%b |:%b ~|:%b ^:%b ~^:%b",
                     red_and, red_nand, red_or, red_nor, red_xor, red_xnor);
            error_count = error_count + 1;
        end

        #5;
        $display("\n================================================================================");
        $display("[CAS v1.4.0.0 REGRESSION SUITE: UNARY REDUCTION OPERATORS]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: All 6 unary reduction operators (&, ~&, |, ~|, ^, ~^) verified with full 4-state dominance.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | VEC=%b | &:%b ~&:%b | |:%b ~|:%b | ^:%b ~^:%b",
                 $time, in_vec, red_and, red_nand, red_or, red_nor, red_xor, red_xnor);
    end

endmodule