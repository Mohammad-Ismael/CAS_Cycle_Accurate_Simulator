// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.15.0.0
// Standard Target: IEEE Std 1800-2023 §35 (DPI-C Open Arrays)
// Module Name    : v3_15_0_0_open_arr_core
// ============================================================================

module v3_15_0_0_open_arr_core (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [31:0] in_val,
    output logic [31:0] out_val
);
    always_comb begin
        if (!rst_n) begin
            out_val = 32'h00000000;
        end else begin
            out_val = in_val + 32'h00000001;
        end
    end
endmodule