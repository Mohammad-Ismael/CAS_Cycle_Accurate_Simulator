// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v2.0.0.0
// Standard Target: IEEE Std 1800-2023 §6.5 & §10.3 (Net Types & Multi-Driver Resolution)
// Module Name    : v2_0_0_0_multi_driver_core
// ============================================================================

module v2_0_0_0_multi_driver_core (
    input  logic [3:0] drv_a,
    input  logic [3:0] drv_b,
    input  logic       en_a,
    input  logic       en_b,
    output logic [3:0] res_wire,
    output logic [3:0] res_wand,
    output logic [3:0] res_wor,
    output logic [3:0] res_tri_bus
);
    // 1. Multiple continuous drivers on a standard 'wire'
    wire [3:0] w_wire;
    assign w_wire = drv_a;
    assign w_wire = drv_b;
    assign res_wire = w_wire;

    // 2. Multiple continuous drivers on a wired-AND 'wand'
    wand [3:0] w_wand;
    assign w_wand = drv_a;
    assign w_wand = drv_b;
    assign res_wand = w_wand;

    // 3. Multiple continuous drivers on a wired-OR 'wor'
    wor  [3:0] w_wor;
    assign w_wor = drv_a;
    assign w_wor = drv_b;
    assign res_wor = w_wor;

    // 4. Tristate bus multiplexing
    tri  [3:0] bus;
    assign bus = en_a ? drv_a : 4'bzzzz;
    assign bus = en_b ? drv_b : 4'bzzzz;
    assign res_tri_bus = bus;

endmodule