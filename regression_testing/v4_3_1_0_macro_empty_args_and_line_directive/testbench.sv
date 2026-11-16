// IEEE 1800-2023 §22.5.1, §22.12 Regression Testbench
`line 100 "macro_testbench.sv" 0

`define PRINT_VALS(a, b, c) $display("TB_VALS:", a,,b,,c);

module tb_v4_3_1_regression;

    logic [3:0] in_a;
    logic [3:0] in_b;
    logic [7:0] out_concat;
    logic [15:0] out_sum1;
    logic [15:0] out_sum2;
    logic [15:0] out_sum3;
    wire         out_nand;

    v4_3_1_core dut (
        .in_a(in_a),
        .in_b(in_b),
        .out_concat(out_concat),
        .out_sum1(out_sum1),
        .out_sum2(out_sum2),
        .out_sum3(out_sum3),
        .out_nand(out_nand)
    );

    initial begin
        in_a = 4'h3;
        in_b = 4'h5;
        #1;

        // Verify §22.12 and $display consecutive commas empty argument
        `PRINT_VALS(in_a, in_b, out_concat)

        // Verify §22.5.1 empty arguments and macro defaults
        // out_concat = {4'h3, 4'h5} = 8'h35
        if (out_concat !== 8'h35) begin
            $display("[FAIL] out_concat expected 8'h35, got %h", out_concat);
            $finish;
        end

        // out_sum1 = in_a (3) + 20 + 5 = 28
        if (out_sum1 !== 16'd28) begin
            $display("[FAIL] out_sum1 expected 28, got %d", out_sum1);
            $finish;
        end

        // out_sum2 = 10 + in_b (5) + 30 = 45
        if (out_sum2 !== 16'd45) begin
            $display("[FAIL] out_sum2 expected 45, got %d", out_sum2);
            $finish;
        end

        // out_sum3 = 7 + 20 + 30 = 57
        if (out_sum3 !== 16'd57) begin
            $display("[FAIL] out_sum3 expected 57, got %d", out_sum3);
            $finish;
        end

        // out_nand = ~(3[0] & 5[0]) = ~(1 & 1) = 0
        if (out_nand !== 1'b0) begin
            $display("[FAIL] out_nand expected 0, got %b", out_nand);
            $finish;
        end

        $display("[PASS] IEEE 1800-2023 §22.5.1 Macro Expansion & §22.12 Line Directives Verified Cleanly!");
        $finish;
    end

endmodule
