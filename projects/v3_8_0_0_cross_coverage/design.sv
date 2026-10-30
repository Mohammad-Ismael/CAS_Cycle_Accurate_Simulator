// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.8.0.0
// Standard Target: IEEE Std 1800-2023 §19.6 (Cross Coverage)
// Module Name    : v3_8_0_0_cross_core
// ============================================================================

module v3_8_0_0_cross_core (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [1:0] mode_in,
    input  logic [7:0] data_in,
    output logic [7:0] res_out
);
    always_comb begin
        if (!rst_n) begin
            res_out = 8'h00;
        end else begin
            case (mode_in)
                2'b00:   res_out = data_in;
                2'b01:   res_out = ~data_in;
                2'b10:   res_out = data_in + 8'h01;
                default: res_out = 8'hFF;
            endcase
        end
    end
endmodule