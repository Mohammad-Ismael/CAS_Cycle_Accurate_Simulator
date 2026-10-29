// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.5.0.0
// Standard Target: IEEE Std 1800-2023 §8.13 - §8.20 (Inheritance & Polymorphism)
// Module Name    : v3_5_0_0_poly_core
// ============================================================================

module v3_5_0_0_poly_core (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  tag_in,
    input  logic [31:0] payload_in,
    output logic [31:0] encoded_out
);
    always_comb begin
        if (!rst_n) begin
            encoded_out = 32'h00000000;
        end else begin
            encoded_out = payload_in ^ {24'h000000, tag_in};
        end
    end
endmodule