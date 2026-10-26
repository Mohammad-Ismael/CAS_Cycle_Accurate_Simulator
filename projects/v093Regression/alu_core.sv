// alu_core.sv
module alu_core (
    input  logic [3:0] op_a,
    input  logic [3:0] op_b,
    input  logic [2:0] opcode,
    output logic [3:0] alu_out
);
    always_comb begin
        case (opcode)
            3'd0: alu_out = op_a & op_b;
            3'd1: alu_out = op_a | op_b;
            3'd2: alu_out = op_a ^ op_b;
            3'd3: alu_out = ~op_a;
            3'd4: alu_out = op_a << 4'd1;
            3'd5: alu_out = op_a >> 4'd1;
            3'd6: alu_out = op_a + op_b;
            3'd7: alu_out = op_a - op_b;
            default: alu_out = 4'd0;
        endcase
    end
endmodule