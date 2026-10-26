// bit4_adder.sv
// 2. 4-bit ripple-carry adder using positional port mapping and bit-slices (v0.9.3)
module bit4_adder (sum, cout, a, b, cin);
    input  [3:0] a, b;
    input  cin;
    output [3:0] sum;
    output cout;

    wire [3:0] a, b, sum;
    wire cin, cout;
    wire [2:0] c;

    full_adder fa0 (sum[0], c[0], a[0], b[0], cin);
    full_adder fa1 (sum[1], c[1], a[1], b[1], c[0]);
    full_adder fa2 (sum[2], c[2], a[2], b[2], c[1]);
    full_adder fa3 (sum[3], cout, a[3], b[3], c[2]);
endmodule