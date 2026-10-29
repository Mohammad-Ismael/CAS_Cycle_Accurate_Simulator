// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.4.0.0
// Standard Target: IEEE Std 1800-2023 §8 (Classes, Objects, Methods, and new)
// Module Name    : v3_4_0_0_class_core
// ============================================================================

module v3_4_0_0_class_core (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  cmd_in,
    input  logic [15:0] addr_in,
    input  logic [31:0] data_in,
    output logic [31:0] result_out
);
    always_comb begin
        if (!rst_n) begin
            result_out = 32'h00000000;
        end else begin
            result_out = data_in ^ {16'h0000, addr_in};
        end
    end
endmodule