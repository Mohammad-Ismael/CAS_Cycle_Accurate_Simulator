`timescale 1ns/1ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.51.0.0
// Standard Target: IEEE Std 1800-2023 §20.8 ($clog2 and Integer Math Functions)
// Design Module: Parameterized Circular FIFO Pointer & Dynamic Depth Controller
// ============================================================================

package v3_51_0_0_pkg;
    parameter int DEFAULT_DEPTH = 32;
    parameter int ADDR_WIDTH    = $clog2(DEFAULT_DEPTH); // 5 bits for depth 32
endpackage

module v3_51_0_0_fifo_ctrl #(
    parameter int BUFFER_ENTRIES = 64
) (
    input  logic                                clk,
    input  logic                                rst_n,
    input  logic                                push,
    input  logic                                pop,
    input  logic [7:0]                          data_in,
    output logic [7:0]                          data_out,
    output logic [$clog2(BUFFER_ENTRIES)-1:0]   wr_ptr,
    output logic [$clog2(BUFFER_ENTRIES)-1:0]   rd_ptr,
    output logic [31:0]                         runtime_clog_out
);
    localparam int PTR_BITS = $clog2(BUFFER_ENTRIES);

    logic [7:0] mem [BUFFER_ENTRIES];
    logic [PTR_BITS-1:0] w_ptr;
    logic [PTR_BITS-1:0] r_ptr;

    assign wr_ptr = w_ptr;
    assign rd_ptr = r_ptr;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            w_ptr            <= '0;
            r_ptr            <= '0;
            data_out         <= 8'h00;
            runtime_clog_out <= 32'd0;
        end else begin
            // 1. Dynamic procedural evaluation of $clog2
            runtime_clog_out <= $clog2(w_ptr + 1);

            if (push) begin
                mem[w_ptr] <= data_in;
                w_ptr      <= w_ptr + 1'b1;
            end
            if (pop) begin
                data_out <= mem[r_ptr];
                r_ptr    <= r_ptr + 1'b1;
            end
        end
    end

endmodule