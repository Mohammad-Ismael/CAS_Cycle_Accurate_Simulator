// Submodule to verify hierarchical port binding and cross-module net routing (v0.2.0)
module alu_core (
    input  logic [3:0] op_a,
    input  logic [3:0] op_b,
    input  logic [2:0] opcode,
    output logic [3:0] alu_out
);

always_comb begin
    // Ternary tree exercising arithmetic, bitwise, shift, and inversion (v0.7.0)
    alu_out = (opcode == 3'd0) ? (op_a & op_b) :
              (opcode == 3'd1) ? (op_a | op_b) :
              (opcode == 3'd2) ? (op_a ^ op_b) :
              (opcode == 3'd3) ? (~op_a) :
              (opcode == 3'd4) ? (op_a << 4'd1) :
              (opcode == 3'd5) ? (op_a >> 4'd1) :
              (opcode == 3'd6) ? (op_a + op_b) :
                                 (op_a - op_b);
end

endmodule

// Top DUT
module regression_dut (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [3:0] a,
    input  logic [3:0] b,
    input  logic [2:0] opcode,
    output logic [3:0] alu_result,
    output logic [3:0] reg_accum,
    output logic       is_equal,
    output logic       is_less
);

    // Hierarchical submodule instantiation (v0.2.0)
    alu_core u_core (
        .op_a(a),
        .op_b(b),
        .opcode(opcode),
        .alu_out(alu_result)
    );

    always_comb begin
        // Relational and equality operator checks (v0.7.0)
        is_equal = (a == b) ? 1'd1 : 1'd0;
        is_less  = (a < b)  ? 1'd1 : 1'd0;
    end

    // Sequential process testing Active vs NBA stratified scheduling (v0.4.0)
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            reg_accum <= 4'd0;
        end else begin
            reg_accum <= reg_accum + alu_result;
        end
    end

endmodule