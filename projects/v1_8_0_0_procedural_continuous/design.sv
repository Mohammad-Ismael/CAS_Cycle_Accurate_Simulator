// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v1.8.0.0
// Standard Target: IEEE Std 1800-2023 §10.4.2 (Procedural Continuous Assignments)
// Module Name    : v1_8_0_0_pca_core
// ============================================================================

module v1_8_0_0_pca_core (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [7:0] d_in,
    output logic [7:0] q_out
);
    // IEEE 1800-2023 §10.4.2 allows procedural continuous assign on variables
    // driven by regular 'always' blocks (whereas 'always_ff' enforces single-process exclusivity §9.2.2.4)
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            q_out <= 8'h00;
        end else begin
            q_out <= d_in;
        end
    end
endmodule