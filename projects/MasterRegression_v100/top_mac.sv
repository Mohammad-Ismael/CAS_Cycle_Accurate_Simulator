// top_mac.sv - Top-Level RTL integrating Adder, Logic Unit, and Register Storage
`include "alu_defs.vh"
`include "ripple_adder.sv"
`include "register_file.sv"

module top_mac (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [2:0] opcode,
    input  logic [3:0] op_a,
    input  logic [3:0] op_b,
    input  logic       cin,
    input  logic       mem_write,
    input  logic [3:0] mem_addr,
    output logic [3:0] alu_result,
    output logic       alu_cout,
    output logic [7:0] mem_readback
);
    logic [3:0] add_sum;
    logic       add_cout;
    logic [3:0] logic_out;

    // Instantiate 4-bit Ripple Adder
    ripple_adder #(.WIDTH(4)) u_adder (
        .a(op_a),
        .b(op_b),
        .cin(cin),
        .sum(add_sum),
        .cout(add_cout)
    );

    // Instantiate Register File Storage
    register_file #(
        .DATA_WIDTH(8),
        .ADDR_WIDTH(4),
        .DEPTH(16)
    ) u_rf (
        .clk(clk),
        .rst_n(rst_n),
        .wr_en(mem_write),
        .wr_addr(mem_addr),
        .wr_data({add_cout, 3'b000, add_sum}),
        .rd_addr(mem_addr),
        .rd_data(mem_readback)
    );

    // Combinational ALU Multiplexer
    always_comb begin
        case (opcode)
            ALU_ADD: begin
                alu_result = add_sum;
                alu_cout   = add_cout;
            end
            ALU_SUB: begin
                alu_result = op_a - op_b;
                alu_cout   = (op_a >= op_b) ? 1'b1 : 1'b0;
            end
            ALU_AND: begin
                alu_result = op_a & op_b;
                alu_cout   = 1'b0;
            end
            ALU_OR: begin
                alu_result = op_a | op_b;
                alu_cout   = 1'b0;
            end
            ALU_XOR: begin
                alu_result = op_a ^ op_b;
                alu_cout   = 1'b0;
            end
            ALU_SHL: begin
                alu_result = op_a << op_b[1:0];
                alu_cout   = 1'b0;
            end
            ALU_SHR: begin
                alu_result = op_a >> op_b[1:0];
                alu_cout   = 1'b0;
            end
            default: begin
                alu_result = op_a;
                alu_cout   = 1'b0;
            end
        endcase
    end

endmodule