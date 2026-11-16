// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.2.0.0
// Standard Target: IEEE Std 1800-2023 §14 (Clocking Blocks & Cycle Delays)
// Module Name    : v3_2_0_0_cb_core
// ============================================================================

module v3_2_0_0_cb_core (
    input  logic       clk,
    input  logic       rst_n,
    input  logic       valid_in,
    input  logic [7:0] data_in,
    output logic       ready_out,
    output logic [7:0] data_out
);
    // Standard synchronous pipeline register
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            ready_out <= 1'b1;
            data_out  <= 8'h00;
        end else begin
            if (valid_in && ready_out) begin
                data_out  <= data_in + 8'h01; // Pipeline processing: increment by 1
            end
        end
    end
endmodule