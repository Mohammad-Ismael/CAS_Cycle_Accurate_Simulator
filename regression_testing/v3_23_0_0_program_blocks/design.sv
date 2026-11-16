// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.23.0.0
// Standard Target: IEEE Std 1800-2023 §24 (SystemVerilog Program Blocks)
// Design Module Core
// ============================================================================

module v3_23_0_0_fifo_core (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        push,
    input  logic [15:0] din,
    output logic        valid,
    output logic [15:0] dout
);
    // Design processes execute in the Active / NBA scheduler regions (§4)
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            valid <= 1'b0;
            dout  <= 16'h0000;
        end else begin
            valid <= push;
            if (push) begin
                dout <= din + 16'h0010;
            end else begin
                dout <= 16'h0000;
            end
        end
    end
endmodule