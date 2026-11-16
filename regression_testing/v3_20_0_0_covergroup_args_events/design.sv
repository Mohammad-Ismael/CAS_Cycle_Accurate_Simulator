// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.20.0.0
// Standard Target: IEEE Std 1800-2023 §19.4 & §19.8 (Covergroup Args & Events)
// Module Name    : v3_20_0_0_coverage_core
// ============================================================================

module v3_20_0_0_coverage_core (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  data_in,
    input  logic        valid_in,
    output logic [7:0]  data_out,
    output logic        valid_out
);
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            data_out  <= 8'h00;
            valid_out <= 1'b0;
        end else begin
            data_out  <= data_in + 8'h01;
            valid_out <= valid_in;
        end
    end
endmodule