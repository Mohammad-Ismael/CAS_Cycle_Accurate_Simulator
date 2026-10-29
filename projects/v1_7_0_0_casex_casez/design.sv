// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v1.7.0.0
// Standard Target: IEEE Std 1800-2023 §12.5.1 (casez) and §12.5.2 (casex)
// Module Name    : v1_7_0_0_casex_casez_core
// ============================================================================

module v1_7_0_0_casex_casez_core (
    input  logic [3:0] in_val,
    output logic [3:0] out_case,
    output logic [3:0] out_casez,
    output logic [3:0] out_casex,
    output logic [3:0] out_prio
);
    // ------------------------------------------------------------------------
    // 1. Standard Case Statement: Exact 4-State Matching (§12.5)
    // ------------------------------------------------------------------------
    always_comb begin
        case (in_val)
            4'b0001: out_case = 4'd1;
            4'b0010: out_case = 4'd2;
            4'b0100: out_case = 4'd3;
            4'b1000: out_case = 4'd4;
            4'b00xx: out_case = 4'd8; // Exact 4-state 'x' match
            default: out_case = 4'd0; // Default fallthrough
        endcase
    end

    // ------------------------------------------------------------------------
    // 2. casez Statement: 'z' and '?' Wildcards, 'x' Exact (§12.5.1)
    // ------------------------------------------------------------------------
    always_comb begin
        casez (in_val)
            4'b1???: out_casez = 4'd1; // MSB 1, bits [2:0] don't-care via '?'
            4'b01zz: out_casez = 4'd2; // bits [3:2]=01, bits [1:0] don't-care via 'z'
            4'b001?: out_casez = 4'd3; // bits [3:1]=001, bit 0 don't-care via '?'
            4'b0001: out_casez = 4'd4; // Exact match
            4'b00xx: out_casez = 4'd7; // Explicit 'x' match (NOT wildcarded in casez)
            default: out_casez = 4'd0; // Default fallthrough
        endcase
    end

    // ------------------------------------------------------------------------
    // 3. casex Statement: 'x', 'z', and '?' All Treated as Don't-Cares (§12.5.2)
    // ------------------------------------------------------------------------
    always_comb begin
        casex (in_val)
            4'b1xxx: out_casex = 4'd1; // MSB 1, bits [2:0] don't-care via 'x'
            4'b01zz: out_casex = 4'd2; // bits [3:2]=01, bits [1:0] don't-care via 'z'
            4'b001?: out_casex = 4'd3; // bits [3:1]=001, bit 0 don't-care via '?'
            4'b0001: out_casex = 4'd4; // Exact match
            default: out_casex = 4'd0; // Default fallthrough
        endcase
    end

    // ------------------------------------------------------------------------
    // 4. Lexical Priority Order & Overlap Resolution
    // ------------------------------------------------------------------------
    always_comb begin
        casez (in_val)
            4'b1???: out_prio = 4'd1;  // Primary match takes precedence
            4'b11??: out_prio = 4'd2;  // Overlapping branch; must be shadowed by 4'b1???
            4'b011?: out_prio = 4'd3;
            default: out_prio = 4'd15; // Default when no branch matches
        endcase
    end

endmodule
