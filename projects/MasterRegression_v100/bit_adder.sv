// bit_adder.sv - 1-bit Full Adder built with structural gate primitives
module bit_adder (
    input  logic a,
    input  logic b,
    input  logic cin,
    output logic sum,
    output logic cout
);
    logic s1;
    logic c1;
    logic c2;

    xor g_xor1 (s1, a, b);
    xor g_xor2 (sum, s1, cin);

    and g_and1 (c1, a, b);
    and g_and2 (c2, s1, cin);
    or  g_or1  (cout, c1, c2);

endmodule