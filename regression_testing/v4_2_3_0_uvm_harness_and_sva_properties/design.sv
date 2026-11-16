`timescale 1ns/1ps

module alu_core (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  a,
    input  logic [7:0]  b,
    input  logic [1:0]  op,
    input  logic        valid,
    output logic [8:0]  res,
    output logic        ready
);

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            res   <= 9'b0;
            ready <= 1'b0;
        end else if (valid) begin
            case (op)
                2'b00: res <= a + b;
                2'b01: res <= (a >= b) ? (a - b) : (b - a);
                2'b10: res <= {1'b0, a & b};
                2'b11: res <= {1'b0, a | b};
            endcase
            ready <= 1'b1;
        end else begin
            ready <= 1'b0;
        end
    end

`ifndef VERILATOR
    property p_ready_follows_valid;
        @(posedge clk) disable iff (!rst_n)
        valid |=> ready;
    endproperty
    a_ready: assert property (p_ready_follows_valid);

    property p_valid_assume;
        @(posedge clk) valid |-> (op != 2'b11 || (a | b) >= 0);
    endproperty
    asm_valid: assume property (p_valid_assume);
`endif

endmodule
