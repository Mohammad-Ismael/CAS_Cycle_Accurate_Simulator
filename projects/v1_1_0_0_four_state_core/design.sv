// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v1.1.0.0
// Standard Target: IEEE Std 1800-2023 Chapters 6, 7 & 11 (4-State Semantics)
// Module Name    : v1_1_0_0_four_state_core
// ============================================================================

module v1_1_0_0_four_state_core (
    input  logic [3:0] a,
    input  logic [3:0] b,
    input  logic       sel,
    output logic [3:0] out_and,
    output logic [3:0] out_or,
    output logic [3:0] out_xor,
    output logic       out_log_eq,
    output logic       out_case_eq,
    output logic [3:0] out_ternary
);

    assign out_and     = a & b;
    assign out_or      = a | b;
    assign out_xor     = a ^ b;
    assign out_log_eq  = (a == b);
    assign out_case_eq = (a === b);
    assign out_ternary = sel ? a : b;

endmodule