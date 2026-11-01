// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.28.0.0
// Standard Target: IEEE Std 1800-2023 §28 & §29 (Gate Primitives & UDPs)
// Design Module: Synthesized-Style ALU Slice & Tristate Bus Multiplexer
// ============================================================================

// Combinational User-Defined Primitive (§29.2)
primitive v3_28_0_0_maj3_udp (
    output out,
    input  a,
    input  b,
    input  c
);
    table
        // a  b  c : out
           1  1  ? : 1;
           1  ?  1 : 1;
           ?  1  1 : 1;
           0  0  ? : 0;
           0  ?  0 : 0;
           ?  0  0 : 0;
    endtable
endprimitive

module v3_28_0_0_gate_core (
    input  logic       a,
    input  logic       b,
    input  logic       cin,
    input  logic       ctrl_en0,
    input  logic       ctrl_en1,
    output logic       sum,
    output logic       cout,
    inout  wire        shared_bus
);
    wire sum_raw;
    wire xor_ab;
    wire cout_maj;

    // Built-in gate primitives (§28.4)
    xor g_xor1 (xor_ab, a, b);
    xor g_xor2 (sum_raw, xor_ab, cin);

    // Combinational UDP instantiation (§29)
    v3_28_0_0_maj3_udp g_maj (cout_maj, a, b, cin);

    assign sum  = sum_raw;
    assign cout = cout_maj;

    // Tristate control gate primitives (§28.5)
    // bufif1 drives sum when ctrl_en1 == 1, else 'z'
    // bufif0 drives cout when ctrl_en0 == 0, else 'z'
    bufif1 g_tri1 (shared_bus, sum_raw, ctrl_en1);
    bufif0 g_tri0 (shared_bus, cout_maj, ctrl_en0);

endmodule