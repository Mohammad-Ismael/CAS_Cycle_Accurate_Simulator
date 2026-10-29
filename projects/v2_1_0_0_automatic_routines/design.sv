// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v2.1.0.0
// Standard Target: IEEE Std 1800-2023 §13.4.2 & §13.5.1 (automatic routines)
// Module Name    : v2_1_0_0_automatic_core
// ============================================================================

module v2_1_0_0_automatic_core (
    input  logic [3:0] n_in,
    output logic [31:0] fact_out
);
    // Recursive automatic function: dynamic stack allocation per activation frame
    function automatic [31:0] factorial(input [3:0] val);
        logic [31:0] temp;
        begin
            if (val <= 1) begin
                factorial = 32'd1;
            end else begin
                temp = factorial(val - 1);
                factorial = val * temp;
            end
        end
    endfunction

    always_comb begin
        fact_out = factorial(n_in);
    end

endmodule