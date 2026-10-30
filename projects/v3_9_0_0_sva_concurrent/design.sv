// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.9.0.0
// Standard Target: IEEE Std 1800-2023 §16 (SVA Concurrent Assertions)
// Module Name    : v3_9_0_0_sva_core
// ============================================================================

module v3_9_0_0_sva_core (
    input  logic       clk,
    input  logic       rst_n,
    input  logic       req,
    output logic       ack,
    output logic [7:0] data_out
);
    // Simple protocol responder: ack asserts 1 cycle after req
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            ack      <= 1'b0;
            data_out <= 8'h00;
        end else begin
            ack <= req; // 1-cycle latency
            if (req) begin
                data_out <= data_out + 8'h05;
            end
        end
    end
endmodule