`timescale 1ns/1ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.50.0.0
// Standard Target: IEEE Std 1800-2023 §20.9 (Vector Intrinsic Query Functions)
// Design Module: Arbiter Monitor and Bus State Integrity Checker
// ============================================================================

package v3_50_0_0_pkg;

    typedef struct packed {
        logic [3:0] grant_vec;
        logic [3:0] req_vec;
    } arb_status_s;

endpackage

module v3_50_0_0_arbiter_checker (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [3:0] req,
    input  logic [3:0] gnt,
    output logic [2:0] active_req_count,
    output logic       single_grant_valid,
    output logic       grant_or_idle_valid,
    output logic       bus_has_unknowns
);
    import v3_50_0_0_pkg::*;

    arb_status_s status_reg;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            active_req_count    <= 3'd0;
            single_grant_valid  <= 1'b0;
            grant_or_idle_valid <= 1'b0;
            bus_has_unknowns    <= 1'b0;
            status_reg          <= '0;
        end else begin
            status_reg.grant_vec <= gnt;
            status_reg.req_vec   <= req;

            // 1. $countones: count asserted request lines
            active_req_count <= 3'($countones(req));

            // 2. $onehot: verify exactly one grant is active
            single_grant_valid <= $onehot(gnt);

            // 3. $onehot0: verify at most one grant is active (idle or single grant)
            grant_or_idle_valid <= $onehot0(gnt);

            // 4. $isunknown: verify no floating or uninitialized bits on status
            bus_has_unknowns <= $isunknown({req, gnt});
        end
    end

endmodule