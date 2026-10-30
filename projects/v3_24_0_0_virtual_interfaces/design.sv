// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.24.0.0
// Standard Target: IEEE Std 1800-2023 §25.9 (Virtual Interfaces in OOP)
// Interface & Design Module
// ============================================================================

interface mem_bus_if (input logic clk);
    logic        rst_n;
    logic        req;
    logic [15:0] addr;
    logic [15:0] wdata;
    logic        ready;
    logic [15:0] rdata;

    modport drv_mp (
        input  clk, rst_n, ready, rdata,
        output req, addr, wdata
    );

    modport dut_mp (
        input  clk, rst_n, req, addr, wdata,
        output ready, rdata
    );
endinterface

module v3_24_0_0_mem_core (
    mem_bus_if.dut_mp bus
);
    logic [15:0] ram [0:3];

    always_ff @(posedge bus.clk or negedge bus.rst_n) begin
        if (!bus.rst_n) begin
            bus.ready <= 1'b0;
            bus.rdata <= 16'h0000;
            ram[0]    <= 16'h0000;
            ram[1]    <= 16'h0000;
            ram[2]    <= 16'h0000;
            ram[3]    <= 16'h0000;
        end else begin
            if (bus.req) begin
                bus.ready <= 1'b1;
                ram[bus.addr[1:0]] <= bus.wdata;
                bus.rdata <= bus.wdata + 16'h0005; // read-back transformed
            end else begin
                bus.ready <= 1'b0;
                bus.rdata <= 16'h0000;
            end
        end
    end
endmodule