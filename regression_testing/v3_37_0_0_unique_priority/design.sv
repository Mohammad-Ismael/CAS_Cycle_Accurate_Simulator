// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.37.0.0
// Standard Target: IEEE Std 1800-2023 §12.5 (Unique, Unique0, Priority If/Case)
// Design Module: Runtime Decision Validator & Priority Arbiter
// ============================================================================

module v3_37_0_0_decision_core (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [3:0] req_vec,
    input  logic [1:0] mode_sel,
    output logic [1:0] grant_idx,
    output logic [7:0] mode_code,
    output logic       grant_valid
);

    // 1. Priority Case: First matching request wins (Priority Arbiter §12.5.4)
    always_comb begin
        grant_idx   = 2'd0;
        grant_valid = 1'b1;

        priority case (1'b1)
            req_vec[0]: grant_idx = 2'd0;
            req_vec[1]: grant_idx = 2'd1;
            req_vec[2]: grant_idx = 2'd2;
            req_vec[3]: grant_idx = 2'd3;
            default: begin
                grant_valid = 1'b0;
            end
        endcase
    end

    // 2. Unique Case: Mutually exclusive mode decoding (§12.5.3)
    always_comb begin
        mode_code = 8'h00;
        unique case (mode_sel)
            2'b00: mode_code = 8'h11;
            2'b01: mode_code = 8'h22;
            2'b10: mode_code = 8'h44;
            2'b11: mode_code = 8'h88;
        endcase
    end

endmodule