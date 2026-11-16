// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.21.0.0
// Standard Target: IEEE Std 1800-2023 §16.10 (SVA Sequence Local Variables)
// Module Name    : v3_21_0_0_pipe_core
// ============================================================================

module v3_21_0_0_pipe_core (
    input  logic       clk,
    input  logic       rst_n,
    input  logic       req_in,
    input  logic [7:0] data_in,
    output logic       ack_out,
    output logic [7:0] data_out
);
    // 2-stage pipeline register
    logic [7:0] pipe_data_q1, pipe_data_q2;
    logic       pipe_vld_q1,  pipe_vld_q2;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pipe_data_q1 <= 8'h00;
            pipe_data_q2 <= 8'h00;
            pipe_vld_q1  <= 1'b0;
            pipe_vld_q2  <= 1'b0;
            ack_out      <= 1'b0;
            data_out     <= 8'h00;
        end else begin
            // Stage 1
            pipe_vld_q1  <= req_in;
            pipe_data_q1 <= data_in;

            // Stage 2: pipeline increment transform (+2)
            pipe_vld_q2  <= pipe_vld_q1;
            pipe_data_q2 <= pipe_data_q1 + 8'h02;

            // Stage 3 Output
            ack_out      <= pipe_vld_q2;
            data_out     <= pipe_data_q2;
        end
    end
endmodule