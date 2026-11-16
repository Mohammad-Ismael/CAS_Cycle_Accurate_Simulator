// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.17.0.0
// Standard Target: IEEE Std 1800-2023 §15 (Semaphores & Mailboxes)
// Module Name    : v3_17_0_0_sync_core
// ============================================================================

module v3_17_0_0_sync_core (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [31:0] data_in,
    output logic [31:0] data_out
);
    always_comb begin
        if (!rst_n) begin
            data_out = 32'h00000000;
        end else begin
            data_out = data_in ^ 32'hA5A5A5A5;
        end
    end
endmodule