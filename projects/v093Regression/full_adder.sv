// full_adder.sv
// 1. Gate-level full adder using built-in gate primitives and non-ANSI port header (v0.9.2, v0.9.3)
module full_adder (sum, cout, a, b, cin);
    input  a, b, cin;
    output sum, cout;
    wire   a, b, cin, sum, cout;
    wire   w1, w2, w3;

    xor (sum, a, b, cin);
    and (w1, a, b);
    and (w2, b, cin);
    and (w3, a, cin);
    or  (cout, w1, w2, w3);
endmodule