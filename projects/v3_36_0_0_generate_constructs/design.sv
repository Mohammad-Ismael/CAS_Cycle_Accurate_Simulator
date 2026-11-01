// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.36.0.0
// Standard Target: IEEE Std 1800-2023 §27 (Generate Constructs)
// Design Module: Parameterized Pipelined Adder Tree with Conditional Feature Set
// ============================================================================

module v3_36_0_0_param_adder_tree #(
    parameter int WIDTH       = 8,
    parameter int NUM_STAGES  = 4,
    parameter bit ENABLE_PIPE = 1'b1,
    parameter int ARITH_MODE  = 0     // 0: ADD, 1: SUB, 2: XOR
)(
    input  logic                   clk,
    input  logic                   rst_n,
    input  logic [WIDTH-1:0]       operand_in [0:NUM_STAGES-1],
    output logic [(WIDTH+NUM_STAGES)-1:0] result_out
);
    // Intermediate wires across generated stages
    logic [(WIDTH+NUM_STAGES)-1:0] stage_data [0:NUM_STAGES];

    assign stage_data[0] = operand_in[0];

    // 1. Loop Generate Construct (§27.4) with named scope 'gen_stages'
    genvar i;
    generate
        for (i = 0; i < NUM_STAGES - 1; i++) begin : gen_stages
            logic [(WIDTH+NUM_STAGES)-1:0] partial_accum;

            // 2. Case Generate Construct (§27.6): Select arithmetic mode at elaboration time
            case (ARITH_MODE)
                0: begin : gen_op
                    assign partial_accum = stage_data[i] + operand_in[i+1];
                end
                1: begin : gen_op
                    assign partial_accum = stage_data[i] - operand_in[i+1];
                end
                default: begin : gen_op
                    assign partial_accum = stage_data[i] ^ operand_in[i+1];
                end
            endcase

            // 3. Conditional If-Generate (§27.5): Pipelined vs. Combinational path
            if (ENABLE_PIPE) begin : gen_reg
                always_ff @(posedge clk or negedge rst_n) begin
                    if (!rst_n) begin
                        stage_data[i+1] <= '0;
                    end else begin
                        stage_data[i+1] <= partial_accum;
                    end
                end
            end else begin : gen_wire
                assign stage_data[i+1] = partial_accum;
            end
        end
    endgenerate

    assign result_out = stage_data[NUM_STAGES-1];

endmodule