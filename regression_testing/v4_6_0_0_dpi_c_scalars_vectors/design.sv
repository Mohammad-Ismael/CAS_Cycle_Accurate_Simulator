// ============================================================================
// CAS Milestone Regression Design
// Target Release : v4.6.0.0
// Standard Target: IEEE Std 1800-2023 §35 (DPI-C Scalars & Vectors)
// ============================================================================

module v4_6_0_0_alu_helper(
    input  logic [31:0] a,
    input  logic [31:0] b,
    output logic [31:0] res
);
    assign res = a + b;
endmodule
