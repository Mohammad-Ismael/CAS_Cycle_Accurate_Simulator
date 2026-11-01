// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.25.0.0
// Standard Target: IEEE Std 1800-2023 §7.12 (Array Reduction & Locator Methods)
// Design Module: Packet Checksum & Match Accelerator
// ============================================================================

module v3_25_0_0_packet_eval (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        start,
    input  logic [7:0]  byte_in,
    output logic [15:0] checksum_out,
    output logic [7:0]  max_byte_out,
    output logic        done
);
    logic [7:0] history [0:3];
    logic [1:0] idx;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            checksum_out <= 16'h0000;
            max_byte_out <= 8'h00;
            done         <= 1'b0;
            idx          <= 2'd0;
            for (int i = 0; i < 4; i++) history[i] <= 8'h00;
        end else begin
            if (start) begin
                history[idx] <= byte_in;
                idx          <= idx + 2'd1;
                done         <= (idx == 2'd3);

                // Compute rolling sum and peak
                checksum_out <= checksum_out + {8'h00, byte_in};
                if (byte_in > max_byte_out) begin
                    max_byte_out <= byte_in;
                end
            end else begin
                done <= 1'b0;
            end
        end
    end
endmodule