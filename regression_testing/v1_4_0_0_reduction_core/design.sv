// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v1.4.0.0
// Standard Target: IEEE Std 1800-2023 §11.4.3 (Unary Reduction Operators)
// Module Name    : v1_4_0_0_reduction_core
// ============================================================================

module v1_4_0_0_reduction_core (
    input  logic [3:0] in_vec,
    output logic       red_and,
    output logic       red_nand,
    output logic       red_or,
    output logic       red_nor,
    output logic       red_xor,
    output logic       red_xnor
);

    // Continuous assignments exercising all 6 reduction operators
    assign red_and  = &in_vec;
    assign red_nand = ~&in_vec;
    assign red_or   = |in_vec;
    assign red_nor  = ~|in_vec;
    assign red_xor  = ^in_vec;
    assign red_xnor = ~^in_vec;

endmodule