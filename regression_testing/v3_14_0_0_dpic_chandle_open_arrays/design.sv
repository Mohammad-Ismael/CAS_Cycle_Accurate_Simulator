// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.14.0.0
// Standard Target: IEEE Std 1800-2023 §35 (DPI-C Chandle & Context Sessions)
// Module Name    : v3_14_0_0_session_core
// ============================================================================

module v3_14_0_0_session_core (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [31:0] val_in,
    output logic [31:0] val_out
);
    always_comb begin
        if (!rst_n) begin
            val_out = 32'h00000000;
        end else begin
            val_out = val_in ^ 32'h5A5A5A5A;
        end
    end
endmodule