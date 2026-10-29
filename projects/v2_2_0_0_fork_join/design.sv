// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v2.2.0.0
// Standard Target: IEEE Std 1800-2023 §9.3.2 (Parallel Blocks: fork-join)
// Module Name    : v2_2_0_0_fork_core
// ============================================================================

module v2_2_0_0_fork_core (
    input  logic        clk,
    input  logic [7:0]  in_val,
    output logic [7:0]  out_join,
    output logic [7:0]  out_any,
    output logic [7:0]  out_none
);
    // Core module serves as pass-through target for fork processes
    always_comb begin
        out_join = in_val;
    end
endmodule
