// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.11.0.0
// Standard Target: IEEE Std 1800-2023 §16.9.2 (SVA Sequence Repetitions)
// Module Name    : v3_11_0_0_rep_core
// ============================================================================

module v3_11_0_0_rep_core (
    input  logic       clk,
    input  logic       rst_n,
    input  logic       start,
    input  logic       busy,
    output logic       done,
    output logic [7:0] count_out
);
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            done      <= 1'b0;
            count_out <= 8'h00;
        end else begin
            if (busy) begin
                count_out <= count_out + 8'h01;
            end
            done <= (count_out == 8'h02 && busy);
        end
    end
endmodule