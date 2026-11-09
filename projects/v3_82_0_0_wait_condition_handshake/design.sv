// ============================================================================
// CAS Milestone Regression Design
// Target Release : v3.82.0.0
// Standard Target: IEEE Std 1800-2023 §9.4.3 (Level-Sensitive wait Statement)
// Design Name    : v3_82_0_0_wait_condition_handshake
// ============================================================================

package v3_82_0_0_pkg;
endpackage

module v3_82_0_0_wait_condition_handshake (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        req,
    input  logic [15:0] data_in,
    output logic        ack,
    output logic [15:0] data_out
);
    // 2-state handshake responder with 1-cycle pipeline delay
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            ack      <= 1'b0;
            data_out <= 16'h0;
        end else begin
            if (req && !ack) begin
                ack      <= 1'b1;
                data_out <= data_in + 16'hA000;
            end else if (!req) begin
                ack      <= 1'b0;
            end
        end
    end
endmodule
