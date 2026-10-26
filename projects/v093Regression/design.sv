// 1. Gate-level full adder using built-in gate primitives and non-ANSI port header (v0.9.2, v0.9.3)
module full_adder (sum, cout, a, b, cin);
    input  a, b, cin;
    output sum, cout;
    wire   a, b, cin, sum, cout;
    wire   w1, w2, w3;

    xor (sum, a, b, cin);
    and (w1, a, b);
    and (w2, b, cin);
    and (w3, a, cin);
    or  (cout, w1, w2, w3);
endmodule

// 2. 4-bit ripple-carry adder using positional port mapping and bit-slices (v0.9.3)
module bit4_adder (sum, cout, a, b, cin);
    input  [3:0] a, b;
    input  cin;
    output [3:0] sum;
    output cout;

    wire [3:0] a, b, sum;
    wire cin, cout;
    wire [2:0] c;

    full_adder fa0 (sum[0], c[0], a[0], b[0], cin);
    full_adder fa1 (sum[1], c[1], a[1], b[1], c[0]);
    full_adder fa2 (sum[2], c[2], a[2], b[2], c[1]);
    full_adder fa3 (sum[3], cout, a[3], b[3], c[2]);
endmodule

// 3. Complex ALU with full precedence operators and case branch control (v0.7.0, v0.8.0)
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