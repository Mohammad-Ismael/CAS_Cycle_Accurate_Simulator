// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v2.5.0.0
// Standard Target: IEEE Std 1800-2023 §6.19 (Enumerated Types)
// Module Name    : v2_5_0_0_fsm_core
// ============================================================================

module v2_5_0_0_fsm_core (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [1:0] cmd_in,
    output logic [1:0] state_out
);
    // Explicit 2-bit logic base type enumerated FSM states
    typedef enum logic [1:0] {
        ST_IDLE  = 2'b00,
        ST_READ  = 2'b01,
        ST_WRITE = 2'b10,
        ST_ERROR = 2'b11
    } state_t;

    state_t current_state, next_state;

    // State register
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            current_state <= ST_IDLE;
        end else begin
            current_state <= next_state;
        end
    end

    // Next-state logic
    always_comb begin
        case (current_state)
            ST_IDLE: begin
                if (cmd_in == 2'b01)      next_state = ST_READ;
                else if (cmd_in == 2'b10) next_state = ST_WRITE;
                else                      next_state = ST_IDLE;
            end
            ST_READ:  next_state = ST_IDLE;
            ST_WRITE: next_state = ST_IDLE;
            default:  next_state = ST_ERROR;
        endcase
    end

    assign state_out = current_state;

endmodule