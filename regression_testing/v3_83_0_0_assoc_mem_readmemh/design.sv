`timescale 1ns/1ps

//==============================================================================
// Module: v3_83_0_0_assoc_mem_readmemh
// Standard: IEEE 1800-2023 SystemVerilog Sparse Associative Array $readmemh
// Description:
//   Demonstrates sparse associative array memory initialization using $readmemh
//   with non-contiguous address gaps (@0000, @0004, @0010, @0020).
//==============================================================================

module v3_83_0_0_assoc_mem_readmemh (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        req,
    input  logic [31:0] addr,
    output logic [31:0] rdata,
    output logic        hit
);

    logic [31:0] mem [logic [31:0]];

    initial begin
        $readmemh("mem.hex", mem);
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            rdata <= 32'h0;
            hit   <= 1'b0;
        end else if (req) begin
            if (mem.exists(addr)) begin
                rdata <= mem[addr];
                hit   <= 1'b1;
            end else begin
                rdata <= 32'h0;
                hit   <= 1'b0;
            end
        end else begin
            rdata <= 32'h0;
            hit   <= 1'b0;
        end
    end

endmodule
