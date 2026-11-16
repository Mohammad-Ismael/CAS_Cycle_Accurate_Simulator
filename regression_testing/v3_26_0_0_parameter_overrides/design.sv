// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.26.0.0
// Standard Target: IEEE Std 1800-2023 §6.20 & §23.10 (Parameter Overrides & Defparam)
// Design Core: Parameterized Configurable Pipeline & Shifter
// ============================================================================

module v3_26_0_0_pipeline_stage #(
    parameter int WIDTH = 8,
    parameter int SHIFT = 1
) (
    input  logic             clk,
    input  logic             rst_n,
    input  logic [WIDTH-1:0] din,
    output logic [WIDTH-1:0] dout
);
    localparam int MASK = (1 << WIDTH) - 1;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            dout <= '0;
        end else begin
            dout <= ((din << SHIFT) | (din >> (WIDTH - SHIFT))) & MASK;
        end
    end
endmodule

module v3_26_0_0_nested_pipeline (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [15:0] din,
    output logic [15:0] dout
);
    // Default parameters are WIDTH=8, SHIFT=1
    // Top-level module will override these using defparam statements
    v3_26_0_0_pipeline_stage stage_inst (
        .clk  (clk),
        .rst_n(rst_n),
        .din  (din),
        .dout (dout)
    );
endmodule