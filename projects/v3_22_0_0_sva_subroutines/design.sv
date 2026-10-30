// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.22.0.0
// Standard Target: IEEE Std 1800-2023 §16.11 (SVA Subroutine Calls on Match)
// Module Name    : v3_22_0_0_handshake_core
// ============================================================================

module v3_22_0_0_handshake_core (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        req,
    input  logic [15:0] tx_data,
    output logic        ack,
    output logic [15:0] rx_data
);
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            ack     <= 1'b0;
            rx_data <= 16'h0000;
        end else begin
            if (req) begin
                ack     <= 1'b1;
                rx_data <= tx_data ^ 16'h5A5A;
            end else begin
                ack     <= 1'b0;
                rx_data <= 16'h0000;
            end
        end
    end
endmodule