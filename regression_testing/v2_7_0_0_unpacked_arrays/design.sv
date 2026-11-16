// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v2.7.0.0
// Standard Target: IEEE Std 1800-2023 §7.4 (Fixed-size Unpacked Arrays & Memories)
// Module Name    : v2_7_0_0_array_core
// ============================================================================

module v2_7_0_0_array_core (
    input  logic       clk,
    input  logic       rst_n,
    input  logic       we,
    input  logic [1:0] waddr,
    input  logic [7:0] wdata,
    input  logic [1:0] raddr,
    output logic [7:0] rdata
);
    // 4-entry, 8-bit unpacked register file memory (§7.4.2)
    logic [7:0] reg_file [0:3];

    // Synchronous write port with reset initialization
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            reg_file[0] <= 8'h00;
            reg_file[1] <= 8'h00;
            reg_file[2] <= 8'h00;
            reg_file[3] <= 8'h00;
        end else if (we) begin
            reg_file[waddr] <= wdata;
        end
    end

    // Asynchronous read port with dynamic indexing
    always_comb begin
        rdata = reg_file[raddr];
    end

endmodule