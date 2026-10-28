module testbench;

    localparam WIDTH = 4;

    logic [WIDTH-1:0]   op_a;
    logic [WIDTH-1:0]   op_b;
    logic [2:0]         alu_sel;

    wire  [WIDTH:0]     add_result;
    wire  [WIDTH:0]     sub_result;
    wire  [2*WIDTH-1:0] mul_result;
    wire                is_equal;
    wire                is_less_than;
    wire                is_greater_than;
    wire  [2*WIDTH-1:0] final_result;

    integer errors = 0;

    arithmetic_unit #(.WIDTH(WIDTH)) uut (
        .op_a(op_a),
        .op_b(op_b),
        .alu_sel(alu_sel),
        .add_result(add_result),
        .sub_result(sub_result),
        .mul_result(mul_result),
        .is_equal(is_equal),
        .is_less_than(is_less_than),
        .is_greater_than(is_greater_than),
        .final_result(final_result)
    );

    initial begin

        $display("=================================================");
        $display("   STARTING ARITHMETIC UNIT VERIFICATION        ");
        $display("=================================================");

        // ---------------------------------------------------------------- -
        // TEST 1: Addition without carry (3 + 4 = 7)
        // -----------------------------------------------------------------
        op_a = 4'd3; op_b = 4'd4; alu_sel = 3'd0;
        #5;
        if (add_result !== 5'd7) begin
            $display("[FAIL] Add: 3 + 4 expected 7, got %0d", add_result);
            errors = errors + 1;
        end else begin
            $display("[PASS] Add without carry: %0d + %0d = %0d", op_a, op_b, add_result);
        end

        // -----------------------------------------------------------------
        // TEST 2: Addition WITH carry expansion (15 + 1 = 16)
        // Checks that the 5th bit correctly captures the carry overflow
        // -----------------------------------------------------------------
        op_a = 4'd15; op_b = 4'd1; alu_sel = 3'd0;
        #5;
        if (add_result !== 5'd16) begin
            $display("[FAIL] Add with carry: 15 + 1 expected 16, got %0d", add_result);
            errors = errors + 1;
        end else begin
            $display("[PASS] Add with carry: %0d + %0d = %0d (5-bit result)", op_a, op_b, add_result);
        end

        // -----------------------------------------------------------------
        // TEST 3: Addition boundary maximum (15 + 15 = 30)
        // -----------------------------------------------------------------
        op_a = 4'd15; op_b = 4'd15; alu_sel = 3'd0;
        #5;
        if (add_result !== 5'd30) begin
            $display("[FAIL] Add max: 15 + 15 expected 30, got %0d", add_result);
            errors = errors + 1;
        end else begin
            $display("[PASS] Add maximum operands: %0d + %0d = %0d", op_a, op_b, add_result);
        end

        // -----------------------------------------------------------------
        // TEST 4: Subtraction positive (10 - 4 = 6)
        // -----------------------------------------------------------------
        op_a = 4'd10; op_b = 4'd4; alu_sel = 3'd1;
        #5;
        if (sub_result[3:0] !== 4'd6) begin
            $display("[FAIL] Sub: 10 - 4 expected 6, got %0d", sub_result[3:0]);
            errors = errors + 1;
        end else begin
            $display("[PASS] Subtraction positive: %0d - %0d = %0d", op_a, op_b, sub_result[3:0]);
        end

        // -----------------------------------------------------------------
        // TEST 5: Subtraction zero difference (7 - 7 = 0)
        // -----------------------------------------------------------------
        op_a = 4'd7; op_b = 4'd7; alu_sel = 3'd1;
        #5;
        if (sub_result[3:0] !== 4'd0 || !is_equal) begin
            $display("[FAIL] Sub zero / equality check failed: expected 0, got %0d", sub_result[3:0]);
            errors = errors + 1;
        end else begin
            $display("[PASS] Subtraction zero diff: %0d - %0d = %0d (equal = %0b)", op_a, op_b, sub_result[3:0], is_equal);
        end

        // -----------------------------------------------------------------
        // TEST 6: Subtraction with borrow underflow (3 - 5 = -2 -> 4'b1110)
        // -----------------------------------------------------------------
        op_a = 4'd3; op_b = 4'd5; alu_sel = 3'd1;
        #5;
        if (sub_result[3:0] !== 4'b1110) begin
            $display("[FAIL] Sub borrow: 3 - 5 expected 2's complement 14 (4'b1110), got %0d", sub_result[3:0]);
            errors = errors + 1;
        end else begin
            $display("[PASS] Subtraction with underflow/borrow: 3 - 5 = %0b (raw 2's complement)", sub_result[3:0]);
        end

        // -----------------------------------------------------------------
        // TEST 7: Multiplication basic (3 * 4 = 12)
        // -----------------------------------------------------------------
        op_a = 4'd3; op_b = 4'd4; alu_sel = 3'd2;
        #5;
        if (mul_result !== 8'd12) begin
            $display("[FAIL] Mul: 3 * 4 expected 12, got %0d", mul_result);
            errors = errors + 1;
        end else begin
            $display("[PASS] Multiplication basic: %0d * %0d = %0d", op_a, op_b, mul_result);
        end

        // -----------------------------------------------------------------
        // TEST 8: Multiplication maximum (15 * 15 = 225)
        // -----------------------------------------------------------------
        op_a = 4'd15; op_b = 4'd15; alu_sel = 3'd2;
        #5;
        if (mul_result !== 8'd225) begin
            $display("[FAIL] Mul max: 15 * 15 expected 225, got %0d", mul_result);
            errors = errors + 1;
        end else begin
            $display("[PASS] Multiplication max operands: %0d * %0d = %0d", op_a, op_b, mul_result);
        end

        // -----------------------------------------------------------------
        // TEST 9: Multiplication by zero (9 * 0 = 0)
        // -----------------------------------------------------------------
        op_a = 4'd9; op_b = 4'd0; alu_sel = 3'd2;
        #5;
        if (mul_result !== 8'd0) begin
            $display("[FAIL] Mul zero: 9 * 0 expected 0, got %0d", mul_result);
            errors = errors + 1;
        end else begin
            $display("[PASS] Multiplication by zero: %0d * %0d = %0d", op_a, op_b, mul_result);
        end

        // -----------------------------------------------------------------
        // TEST 10: Relational Comparisons (<, ==, >)
        // -----------------------------------------------------------------
        op_a = 4'd2; op_b = 4'd8; alu_sel = 3'd4;
        #5;
        if (is_less_than !== 1'b1 || is_greater_than !== 1'b0 || is_equal !== 1'b0) begin
            $display("[FAIL] Comparison failed for 2 < 8");
            errors = errors + 1;
        end else begin
            $display("[PASS] Comparison 2 < 8: lt=%0b, gt=%0b, eq=%0b", is_less_than, is_greater_than, is_equal);
        end

        op_a = 4'd13; op_b = 4'd7; alu_sel = 3'd5;
        #5;
        if (is_greater_than !== 1'b1 || is_less_than !== 1'b0) begin
            $display("[FAIL] Comparison failed for 13 > 7");
            errors = errors + 1;
        end else begin
            $display("[PASS] Comparison 13 > 7: lt=%0b, gt=%0b, eq=%0b", is_less_than, is_greater_than, is_equal);
        end

        // -----------------------------------------------------------------
        // Final Summary
        // -----------------------------------------------------------------
        #10;
        $display("=================================================");
        if (errors == 0) begin
            $display("   ALL 10 ARITHMETIC TESTS PASSED PERFECTLY!     ");
        end else begin
            $display("   TESTBENCH COMPLETED WITH %0d FAILURE(S)       ", errors);
        end
        $display("=================================================");
        $finish;
    end

endmodule