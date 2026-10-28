// design.sv - Complete Arithmetic Logic Unit
module arithmetic_unit #(
    parameter WIDTH = 4
)(
    input  logic [WIDTH-1:0]   op_a,
    input  logic [WIDTH-1:0]   op_b,
    input  logic [2:0]         alu_sel,

    output logic [WIDTH:0]     add_result,  // (WIDTH+1)-bit to retain carry
    output logic [WIDTH:0]     sub_result,  // (WIDTH+1)-bit to retain borrow/sign
    output logic [2*WIDTH-1:0] mul_result,  // (2*WIDTH)-bit full product
    output logic               is_equal,
    output logic               is_less_than,
    output logic               is_greater_than,
    output logic [2*WIDTH-1:0] final_result
);

    // Continuous full-width arithmetic assignments
    assign add_result      = op_a + op_b;
    assign sub_result      = op_a - op_b;
    assign mul_result      = op_a * op_b;

    // Relational comparisons
    assign is_equal        = (op_a == op_b);
    assign is_less_than    = (op_a < op_b);
    assign is_greater_than = (op_a > op_b);

    // Multiplexed output based on opcode
    always_comb begin
        case (alu_sel)
            3'd0: final_result = { {(WIDTH-1){1'b0}}, add_result };
            3'd1: final_result = { {(WIDTH-1){1'b0}}, sub_result };
            3'd2: final_result = mul_result;
            3'd3: final_result = { {(2*WIDTH-1){1'b0}}, is_equal };
            3'd4: final_result = { {(2*WIDTH-1){1'b0}}, is_less_than };
            3'd5: final_result = { {(2*WIDTH-1){1'b0}}, is_greater_than };
            default: final_result = '0;
        endcase
    end

endmodule