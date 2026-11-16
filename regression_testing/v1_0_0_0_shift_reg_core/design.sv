// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v1.0.0.0
// Standard Target: IEEE Std 1800-2023 Chapter 4 (Scheduling Semantics)
// Module Name    : v1_0_0_0_shift_reg_core
//
// WHAT THIS MODULE TESTS:
// 1. Nonblocking Assignment (NBA) Delta Queue (§4.4.2.3):
//    - RHS expressions evaluate in the Active region.
//    - LHS target updates are deferred and committed simultaneously in the NBA region.
//    - Proves stage-by-stage bit propagation across clock cycles (q1 -> q2 -> q3)
//      without race conditions or same-cycle ripple-through.
//
// 2. Blocking Assignment Evaluation Chain (§4.4.2.1):
//    - Blocking statements (=) execute immediately in the Active region.
//    - Demonstrates immediate order-of-execution collapse into a single cycle
//      to verify the contrast between Active and NBA regions.
// ============================================================================

module v1_0_0_0_shift_reg_core (
    input  logic       clk,
    input  logic       rst_n,
    input  logic       d_in,
    output logic       stage1_nba,
    output logic       stage2_nba,
    output logic       stage3_nba,
    output logic [2:0] blocking_out
);

    // ------------------------------------------------------------------------
    // 1. IEEE 1800-2023 Compliant NBA Shift Register (§4.4.2.3)
    // ------------------------------------------------------------------------
    // On every rising clock edge, each stage MUST sample the state from the
    // PREVIOUS clock tick. A pulse at d_in requires 3 clock cycles to reach stage 3.
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            stage1_nba <= 1'b0;
            stage2_nba <= 1'b0;
            stage3_nba <= 1'b0;
        end else begin
            stage1_nba <= d_in;
            stage2_nba <= stage1_nba;
            stage3_nba <= stage2_nba;
        end
    end

    // ------------------------------------------------------------------------
    // 2. Active Region Blocking Assignment Chain (§4.4.2.1)
    // ------------------------------------------------------------------------
    // In contrast to NBA, blocking assignments evaluate and assign immediately,
    // causing a pulse at d_in to race through b1, b2, and b3 within the same cycle.
    logic b1, b2, b3;
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            b1 <= 1'b0;
            b2 <= 1'b0;
            b3 <= 1'b0;
        end else begin
            b1 = d_in;
            b2 = b1;
            b3 = b2;
        end
    end

    assign blocking_out = {b3, b2, b1};

endmodule