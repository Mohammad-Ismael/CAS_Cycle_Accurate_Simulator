// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.6.0.0
// Standard Target: IEEE Std 1800-2023 §18 (Constrained Randomization)
// Module Name    : v3_6_0_0_rand_core
// ============================================================================

module v3_6_0_0_rand_core (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  addr_in,
    input  logic [31:0] data_in,
    output logic [31:0] resp_out
);
    always_comb begin
        if (!rst_n) begin
            resp_out = 32'h00000000;
        end else begin
            resp_out = data_in + {24'h000000, addr_in};
        end
    end
endmodule