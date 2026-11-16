// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.12.0.0
// Standard Target: IEEE Std 1800-2023 §35 (Direct Programming Interface DPI-C)
// Module Name    : v3_12_0_0_dpic_core
// ============================================================================

module v3_12_0_0_dpic_core (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [31:0] a_in,
    input  logic [31:0] b_in,
    output logic [31:0] res_out
);
    always_comb begin
        if (!rst_n) begin
            res_out = 32'h00000000;
        end else begin
            res_out = a_in ^ b_in;
        end
    end
endmodule