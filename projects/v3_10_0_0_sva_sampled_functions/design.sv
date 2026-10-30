// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.10.0.0
// Standard Target: IEEE Std 1800-2023 §16.9.3 (SVA Sampled Value Functions)
// Module Name    : v3_10_0_0_sampled_core
// ============================================================================

module v3_10_0_0_sampled_core (
    input  logic       clk,
    input  logic       rst_n,
    input  logic       start_pulse,
    input  logic [7:0] data_in,
    output logic       active,
    output logic [7:0] latched_data
);
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            active       <= 1'b0;
            latched_data <= 8'h00;
        end else begin
            active <= start_pulse;
            if (start_pulse) begin
                latched_data <= data_in;
            end
        end
    end
endmodule