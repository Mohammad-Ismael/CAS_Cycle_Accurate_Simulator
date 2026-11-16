// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v1.2.0.0
// Standard Target: IEEE Std 1800-2023 Chapter 11 (Expression Width & Signedness)
// Module Name    : v1_2_0_0_width_sign_core
// ============================================================================

module v1_2_0_0_width_sign_core (
    input  logic [3:0]           u_a,
    input  logic [3:0]           u_b,
    input  logic signed [3:0]    s_a,
    input  logic signed [3:0]    s_b,
    input  logic [1:0]           sh_amt,

    // Context-determined width results
    output logic [7:0]           out_context_add, // 4-bit + 4-bit -> 8-bit target
    output logic [3:0]           out_self_add,    // Self-determined addition

    // Signed arithmetic and extension
    output logic signed [7:0]    out_signed_ext,  // 4-bit signed -> 8-bit signed
    output logic signed [7:0]    out_signed_add,  // Signed addition widened
    output logic signed [7:0]    out_signed_ashr, // Arithmetic right shift (>>>)
    output logic [7:0]           out_unsigned_ashr// Arithmetic right shift on unsigned
);

    // 1. Context-determined: operands widened to 8 bits before addition (§11.6.1)
    assign out_context_add  = u_a + u_b;

    // 2. Self-determined: addition evaluated at 4 bits, then assigned (§11.6.2)
    logic [3:0] temp_sum;
    assign temp_sum         = u_a + u_b;
    assign out_self_add     = temp_sum;

    // 3. Sign extension from 4-bit to 8-bit (§11.8.1)
    assign out_signed_ext   = s_a;

    // 4. Signed addition with full sign preservation
    assign out_signed_add   = s_a + s_b;

    // 5. Arithmetic right shift comparison (§11.4.10)
    assign out_signed_ashr  = s_a >>> sh_amt;
    assign out_unsigned_ashr= u_a >>> sh_amt;

endmodule