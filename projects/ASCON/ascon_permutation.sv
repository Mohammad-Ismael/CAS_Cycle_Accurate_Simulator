// ascon_permutation.sv
// file: ascon_permutation.sv start
`default_nettype none
import ascon_constants::*;

module ascon_permutation #(
    parameter int NUM_ROUNDS = 12
) (
    input  logic [63:0] x0_in, x1_in, x2_in, x3_in, x4_in,
    output logic [63:0] x0_out, x1_out, x2_out, x3_out, x4_out
);
    logic [63:0] x0_0, x1_0, x2_0, x3_0, x4_0;
    logic [63:0] x0_1, x1_1, x2_1, x3_1, x4_1;
    logic [63:0] x0_2, x1_2, x2_2, x3_2, x4_2;
    logic [63:0] x0_3, x1_3, x2_3, x3_3, x4_3;
    logic [63:0] x0_4, x1_4, x2_4, x3_4, x4_4;
    logic [63:0] x0_5, x1_5, x2_5, x3_5, x4_5;
    logic [63:0] x0_6, x1_6, x2_6, x3_6, x4_6;
    logic [63:0] x0_7, x1_7, x2_7, x3_7, x4_7;
    logic [63:0] x0_8, x1_8, x2_8, x3_8, x4_8;
    logic [63:0] x0_9, x1_9, x2_9, x3_9, x4_9;
    logic [63:0] x0_10, x1_10, x2_10, x3_10, x4_10;
    logic [63:0] x0_11, x1_11, x2_11, x3_11, x4_11;
    
    generate
        if (NUM_ROUNDS == 12) begin
            ascon_round r0 (.x0_in(x0_in), .x1_in(x1_in), .x2_in(x2_in), .x3_in(x3_in), .x4_in(x4_in), .rc(RC12[0]), .x0_out(x0_0), .x1_out(x1_0), .x2_out(x2_0), .x3_out(x3_0), .x4_out(x4_0));
            ascon_round r1 (.x0_in(x0_0),  .x1_in(x1_0), .x2_in(x2_0), .x3_in(x3_0), .x4_in(x4_0), .rc(RC12[1]), .x0_out(x0_1), .x1_out(x1_1), .x2_out(x2_1), .x3_out(x3_1), .x4_out(x4_1));
            ascon_round r2 (.x0_in(x0_1),  .x1_in(x1_1), .x2_in(x2_1), .x3_in(x3_1), .x4_in(x4_1), .rc(RC12[2]), .x0_out(x0_2), .x1_out(x1_2), .x2_out(x2_2), .x3_out(x3_2), .x4_out(x4_2));
            ascon_round r3 (.x0_in(x0_2),  .x1_in(x1_2), .x2_in(x2_2), .x3_in(x3_2), .x4_in(x4_2), .rc(RC12[3]), .x0_out(x0_3), .x1_out(x1_3), .x2_out(x2_3), .x3_out(x3_3), .x4_out(x4_3));
            ascon_round r4 (.x0_in(x0_3),  .x1_in(x1_3), .x2_in(x2_3), .x3_in(x3_3), .x4_in(x4_3), .rc(RC12[4]), .x0_out(x0_4), .x1_out(x1_4), .x2_out(x2_4), .x3_out(x3_4), .x4_out(x4_4));
            ascon_round r5 (.x0_in(x0_4),  .x1_in(x1_4), .x2_in(x2_4), .x3_in(x3_4), .x4_in(x4_4), .rc(RC12[5]), .x0_out(x0_5), .x1_out(x1_5), .x2_out(x2_5), .x3_out(x3_5), .x4_out(x4_5));
            ascon_round r6 (.x0_in(x0_5),  .x1_in(x1_5), .x2_in(x2_5), .x3_in(x3_5), .x4_in(x4_5), .rc(RC12[6]), .x0_out(x0_6), .x1_out(x1_6), .x2_out(x2_6), .x3_out(x3_6), .x4_out(x4_6));
            ascon_round r7 (.x0_in(x0_6),  .x1_in(x1_6), .x2_in(x2_6), .x3_in(x3_6), .x4_in(x4_6), .rc(RC12[7]), .x0_out(x0_7), .x1_out(x1_7), .x2_out(x2_7), .x3_out(x3_7), .x4_out(x4_7));
            ascon_round r8 (.x0_in(x0_7),  .x1_in(x1_7), .x2_in(x2_7), .x3_in(x3_7), .x4_in(x4_7), .rc(RC12[8]), .x0_out(x0_8), .x1_out(x1_8), .x2_out(x2_8), .x3_out(x3_8), .x4_out(x4_8));
            ascon_round r9 (.x0_in(x0_8),  .x1_in(x1_8), .x2_in(x2_8), .x3_in(x3_8), .x4_in(x4_8), .rc(RC12[9]), .x0_out(x0_9), .x1_out(x1_9), .x2_out(x2_9), .x3_out(x3_9), .x4_out(x4_9));
            ascon_round r10(.x0_in(x0_9),  .x1_in(x1_9), .x2_in(x2_9), .x3_in(x3_9), .x4_in(x4_9), .rc(RC12[10]), .x0_out(x0_10), .x1_out(x1_10), .x2_out(x2_10), .x3_out(x3_10), .x4_out(x4_10));
            ascon_round r11(.x0_in(x0_10), .x1_in(x1_10), .x2_in(x2_10), .x3_in(x3_10), .x4_in(x4_10), .rc(RC12[11]), .x0_out(x0_11), .x1_out(x1_11), .x2_out(x2_11), .x3_out(x3_11), .x4_out(x4_11));
            assign x0_out = x0_11;
            assign x1_out = x1_11;
            assign x2_out = x2_11;
            assign x3_out = x3_11;
            assign x4_out = x4_11;
        end
        else if (NUM_ROUNDS == 8) begin
            ascon_round r0 (.x0_in(x0_in), .x1_in(x1_in), .x2_in(x2_in), .x3_in(x3_in), .x4_in(x4_in), .rc(RC8[0]), .x0_out(x0_0), .x1_out(x1_0), .x2_out(x2_0), .x3_out(x3_0), .x4_out(x4_0));
            ascon_round r1 (.x0_in(x0_0),  .x1_in(x1_0), .x2_in(x2_0), .x3_in(x3_0), .x4_in(x4_0), .rc(RC8[1]), .x0_out(x0_1), .x1_out(x1_1), .x2_out(x2_1), .x3_out(x3_1), .x4_out(x4_1));
            ascon_round r2 (.x0_in(x0_1),  .x1_in(x1_1), .x2_in(x2_1), .x3_in(x3_1), .x4_in(x4_1), .rc(RC8[2]), .x0_out(x0_2), .x1_out(x1_2), .x2_out(x2_2), .x3_out(x3_2), .x4_out(x4_2));
            ascon_round r3 (.x0_in(x0_2),  .x1_in(x1_2), .x2_in(x2_2), .x3_in(x3_2), .x4_in(x4_2), .rc(RC8[3]), .x0_out(x0_3), .x1_out(x1_3), .x2_out(x2_3), .x3_out(x3_3), .x4_out(x4_3));
            ascon_round r4 (.x0_in(x0_3),  .x1_in(x1_3), .x2_in(x2_3), .x3_in(x3_3), .x4_in(x4_3), .rc(RC8[4]), .x0_out(x0_4), .x1_out(x1_4), .x2_out(x2_4), .x3_out(x3_4), .x4_out(x4_4));
            ascon_round r5 (.x0_in(x0_4),  .x1_in(x1_4), .x2_in(x2_4), .x3_in(x3_4), .x4_in(x4_4), .rc(RC8[5]), .x0_out(x0_5), .x1_out(x1_5), .x2_out(x2_5), .x3_out(x3_5), .x4_out(x4_5));
            ascon_round r6 (.x0_in(x0_5),  .x1_in(x1_5), .x2_in(x2_5), .x3_in(x3_5), .x4_in(x4_5), .rc(RC8[6]), .x0_out(x0_6), .x1_out(x1_6), .x2_out(x2_6), .x3_out(x3_6), .x4_out(x4_6));
            ascon_round r7 (.x0_in(x0_6),  .x1_in(x1_6), .x2_in(x2_6), .x3_in(x3_6), .x4_in(x4_6), .rc(RC8[7]), .x0_out(x0_7), .x1_out(x1_7), .x2_out(x2_7), .x3_out(x3_7), .x4_out(x4_7));
            assign x0_out = x0_7;
            assign x1_out = x1_7;
            assign x2_out = x2_7;
            assign x3_out = x3_7;
            assign x4_out = x4_7;
        end
        else begin
            assign x0_out = x0_in;
            assign x1_out = x1_in;
            assign x2_out = x2_in;
            assign x3_out = x3_in;
            assign x4_out = x4_in;
        end
    endgenerate
endmodule

// file: ascon_permutation.sv end