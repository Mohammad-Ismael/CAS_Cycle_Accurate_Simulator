// ============================================================================
// CAS (Cycle Accurate Simulator) // Stress Test Module
// Feature Target: Multi-feature interaction (intra-delays, edge controls, time-wheel)
// ============================================================================

module random_stress_top (
    input  logic       clk,
    input  logic [7:0] d_in,
    output logic [7:0] d_out
);
    // Passthrough / registered pipeline stage with intra-assignment delay
    always_ff @(posedge clk) begin
        d_out <= #2 d_in;
    end
endmodule