/* verilator lint_off WIDTHEXPAND */
// Design module for v4.3.2.0 UVM Testbench regression
module v4_3_2_alu (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  op_a,
    input  logic [7:0]  op_b,
    input  logic [1:0]  op_code,
    output logic [15:0] alu_out,
    output logic        valid_out
);

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            alu_out   <= 16'd0;
            valid_out <= 1'b0;
        end else begin
            valid_out <= 1'b1;
            case (op_code)
                2'b00: alu_out <= 16'(op_a + op_b);
                2'b01: alu_out <= (op_a >= op_b) ? 16'(op_a - op_b) : 16'd0;
                2'b10: alu_out <= 16'(op_a * op_b);
                2'b11: alu_out <= {8'd0, (op_a ^ op_b)};
            endcase
        end
    end

endmodule
