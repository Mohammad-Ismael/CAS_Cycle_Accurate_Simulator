`timescale 1ns/100ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.29.0.0
// Standard Target: IEEE Std 1800-2023 §32 (Specify Blocks & Module Path Delays)
// Design Module: Standard-Cell Style Buffer & Multiplexer with Pin Delays
// ============================================================================

module v3_29_0_0_delay_cell (
    input  wire in_a,
    input  wire in_b,
    input  wire sel,
    output wire out_y
);
    wire mux_internal;

    // Functional structural logic
    assign mux_internal = sel ? in_b : in_a;
    assign out_y        = mux_internal;

    // IEEE Std 1800-2023 §32: Module Path Delays
    specify
        // Parallel path: select to output delay = 3 time units
        (sel => out_y) = 3;

        // State-dependent conditional paths (§32.6):
        // When sel=0, in_a propagates to out_y with delay 4
        // When sel=1, in_b propagates to out_y with delay 2
        if (!sel) (in_a => out_y) = 4;
        if (sel)  (in_b => out_y) = 2;
    endspecify

endmodule