// IEEE 1800-2023 §22.5.1 Macro Expansion & §22.12 `line Directive
`line 10 "macro_design.sv" 0

/* verilator lint_off WIDTHEXPAND */

`define COMBINE(x, y) {x, y}
`define DEF_ADD(a=16'd10, b=16'd20, c=16'd30) ((a) + (b) + (c))
`define GATE_NAND(dly) nand #dly

module v4_3_1_core (
    input  logic [3:0] in_a,
    input  logic [3:0] in_b,
    output logic [7:0] out_concat,
    output logic [15:0] out_sum1,
    output logic [15:0] out_sum2,
    output logic [15:0] out_sum3,
    output wire         out_nand
);
    // §22.5.1 Macro expansion with trailing / empty actuals & defaults
    assign out_concat = `COMBINE(in_a, in_b);
    assign out_sum1   = `DEF_ADD(16'(in_a), , 16'd5);   // b defaults to 20 -> in_a + 20 + 5
    assign out_sum2   = `DEF_ADD(, 16'(in_b), );        // a defaults to 10, c to 30 -> 10 + in_b + 30
    assign out_sum3   = `DEF_ADD(16'd7);                // b defaults to 20, c to 30 -> 7 + 20 + 30

    // Gate delay handling
    `GATE_NAND(1) g1 (out_nand, in_a[0], in_b[0]);

endmodule
