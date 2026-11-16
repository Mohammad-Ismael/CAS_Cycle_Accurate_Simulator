// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.16.0.0
// Standard Target: IEEE Std 1800-2023 §8.25 (Parameterized Classes)
// Module Name    : v3_16_0_0_param_core
// ============================================================================

module v3_16_0_0_param_core (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [31:0] val_in,
    output logic [31:0] val_out
);
    always_comb begin
        if (!rst_n) begin
            val_out = 32'h00000000;
        end else begin
            val_out = val_in + 32'h00000002;
        end
    end
endmodule