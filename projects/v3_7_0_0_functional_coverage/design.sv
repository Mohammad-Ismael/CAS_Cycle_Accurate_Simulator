// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.7.0.0
// Standard Target: IEEE Std 1800-2023 §19 (Functional Coverage & Covergroups)
// Module Name    : v3_7_0_0_cov_core
// ============================================================================

module v3_7_0_0_cov_core (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [7:0] cmd_in,
    input  logic [7:0] addr_in,
    output logic [7:0] status_out
);
    always_comb begin
        if (!rst_n) begin
            status_out = 8'h00;
        end else begin
            status_out = cmd_in ^ addr_in;
        end
    end
endmodule