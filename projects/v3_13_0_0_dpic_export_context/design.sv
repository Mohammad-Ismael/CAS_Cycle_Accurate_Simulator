// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.13.0.0
// Standard Target: IEEE Std 1800-2023 §35 (DPI-C Export Tasks & Context)
// Module Name    : v3_13_0_0_bus_slave
// ============================================================================

module v3_13_0_0_bus_slave (
    input  logic        clk,
    input  logic        rst_n,
    output logic [31:0] reg_0_out,
    output logic [31:0] reg_1_out
);
    logic [31:0] reg_bank [0:3];

    always_comb begin
        if (!rst_n) begin
            reg_0_out = 32'h00000000;
            reg_1_out = 32'h00000000;
        end else begin
            reg_0_out = reg_bank[0];
            reg_1_out = reg_bank[1];
        end
    end
endmodule