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

module regression_dut (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [3:0] a,
    input  logic [3:0] b,
    input  logic [2:0] opcode,
    input  logic [1:0] mem_addr,
    input  logic       mem_we,
    output logic [3:0] alu_result,
    output logic [3:0] reg_accum,
    output logic       is_equal,
    output logic       is_less,
    output logic       bit_extracted,
    output logic [1:0] slice_extracted,
    output logic [3:0] cont_assign_out,
    output logic [3:0] mem_rdata
);

    // Continuous assign statement (v0.9.0 dataflow)
    assign cont_assign_out = a + 4'd1;

    // 1D Unpacked Memory Array: 4 words x 4 bits (v0.9.0)
    logic [3:0] ram [0:3];

    // Asynchronous memory read
    assign mem_rdata = ram[mem_addr];

    alu_core u_core (
        .op_a(a),
        .op_b(b),
        .opcode(opcode),
        .alu_out(alu_result)
    );

    always_comb begin
        is_equal = (a == b) ? 1'd1 : 1'd0;
        is_less  = (a < b)  ? 1'd1 : 1'd0;

        bit_extracted   = a[2];
        slice_extracted = a[3:2];
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            reg_accum <= 4'd0;
        end else begin
            reg_accum <= reg_accum + alu_result;
            // Synchronous memory write
            if (mem_we) begin
                ram[mem_addr] <= a;
            end
        end
    end

endmodule