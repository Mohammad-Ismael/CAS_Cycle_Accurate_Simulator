// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.19.0.0
// Standard Target: IEEE Std 1800-2023 §33 (SystemVerilog Configurations)
// Design Architectures: Default Core vs Alternate Configured Fast Core
// ============================================================================

// Standard default implementation
module v3_19_0_0_default_core (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [31:0] a_in,
    input  logic [31:0] b_in,
    output logic [31:0] res_out,
    output logic [7:0]  cfg_id
);
    assign cfg_id = 8'h01; // ID for default core
    always_comb begin
        if (!rst_n) begin
            res_out = 32'h00000000;
        end else begin
            res_out = a_in + b_in;
        end
    end
endmodule

// Alternate high-performance implementation selected via config
module v3_19_0_0_fast_core (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [31:0] a_in,
    input  logic [31:0] b_in,
    output logic [31:0] res_out,
    output logic [7:0]  cfg_id
);
    assign cfg_id = 8'hAA; // ID for fast core
    always_comb begin
        if (!rst_n) begin
            res_out = 32'h00000000;
        end else begin
            res_out = (a_in ^ b_in) + 32'h00000010;
        end
    end
endmodule