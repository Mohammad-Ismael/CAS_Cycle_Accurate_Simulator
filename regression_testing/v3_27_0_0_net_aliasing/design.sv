// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.27.0.0
// Standard Target: IEEE Std 1800-2023 §10.6 (Net Aliasing & Bidirectional Nets)
// Design Core: Bidirectional Crossbar Pad with Net Aliasing
// ============================================================================

module v3_27_0_0_pad_crossbar (
    inout wire [7:0] port_north,
    inout wire [7:0] port_south,
    inout wire [7:0] tap_monitor
);
    // IEEE 1800-2023 §10.6: Bidirectional net equivalence across 3 nets
    alias port_north = port_south = tap_monitor;

endmodule

module v3_27_0_0_bidir_core (
    input  logic       clk,
    input  logic       rst_n,
    input  logic       drive_north_en,
    input  logic [7:0] data_north,
    input  logic       drive_south_en,
    input  logic [7:0] data_south,
    output logic [7:0] captured_tap
);
    wire [7:0] bus_n;
    wire [7:0] bus_s;
    wire [7:0] bus_tap;

    // Tri-state drivers feeding opposite sides of the crossbar
    assign bus_n = drive_north_en ? data_north : 8'hzz;
    assign bus_s = drive_south_en ? data_south : 8'hzz;

    // Crossbar instantiation containing the alias construct
    v3_27_0_0_pad_crossbar xbar (
        .port_north (bus_n),
        .port_south (bus_s),
        .tap_monitor(bus_tap)
    );

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            captured_tap <= 8'h00;
        end else begin
            captured_tap <= bus_tap;
        end
    end
endmodule