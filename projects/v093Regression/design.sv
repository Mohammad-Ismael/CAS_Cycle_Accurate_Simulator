// 4. Top-level unified DUT exercising memories, dataflow, sequential logic, and submodules
module master_dut (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [3:0] in_a,
    input  logic [3:0] in_b,
    input  logic [2:0] opcode,
    input  logic [1:0] mem_addr,
    input  logic       mem_we,
    output logic [3:0] adder_sum,
    output logic       adder_cout,
    output logic [3:0] alu_result,
    output logic [3:0] reg_accum,
    output logic       is_equal,
    output logic       is_less,
    output logic       bit_extracted,
    output logic [1:0] slice_extracted,
    output logic [3:0] cont_assign_out,
    output logic [3:0] mem_rdata,
    output logic       dff_q,
    output logic       dff_qb
);

    // v0.9.0 Continuous assign dataflow
    assign cont_assign_out = in_a + 4'd1;

    // v0.9.0 1D Unpacked Memory Array
    logic [3:0] ram [0:3];
    assign mem_rdata = ram[mem_addr];

    // v0.9.3 Submodule positional slice instantiation
    bit4_adder u_adder (adder_sum, adder_cout, in_a, in_b, 1'b0);

    // v0.2.0 Submodule named instantiation
    alu_core u_alu (
        .op_a(in_a),
        .op_b(in_b),
        .opcode(opcode),
        .alu_out(alu_result)
    );

    // v0.7.0 / v0.8.0 Continuous relational expressions and bit-slicing
    always_comb begin
        is_equal = (in_a == in_b) ? 1'd1 : 1'd0;
        is_less  = (in_a < in_b)  ? 1'd1 : 1'd0;
        bit_extracted   = in_a[2];
        slice_extracted = in_a[3:2];
    end

    // v0.4.0 / v0.9.2 Generic always with edge sensitivity list (clk or negedge rst_n)
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            reg_accum <= 4'd0;
            dff_q     <= 1'b0;
        end else begin
            reg_accum <= reg_accum + alu_result;
            dff_q     <= in_a[0];
            if (mem_we) begin
                ram[mem_addr] <= in_a;
            end
        end
    end

    assign dff_qb = ~dff_q;

endmodule