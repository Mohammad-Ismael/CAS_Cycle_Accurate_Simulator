// ============================================================================
// CAS Milestone Regression Design
// Target Release : v3.79.0.0
// Standard Target: IEEE Std 1800-2023 §10.3 (Continuous Assignment to Memory Slices)
// Design Name    : v3_79_0_0_mca_rmw_slice
// ============================================================================

package v3_79_0_0_pkg;
endpackage

module v3_79_0_0_mca_rmw_slice (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        drv_bit32,
    input  logic        drv_bit33,
    input  logic [7:0]  drv_byte2,
    input  logic [7:0]  drv_byte3,
    output logic [63:0] mem_out0,
    output logic [63:0] mem_out1
);
    // Unpacked memory array of 64-bit words
    logic [63:0] mem [0:3];

    // Continuous assignments writing adjacent slices into mem[0]
    assign mem[0][32+:1] = drv_bit32;
    assign mem[0][33+:1] = drv_bit33;

    // Continuous assignments writing adjacent byte slices into mem[1]
    assign mem[1][16+:8] = drv_byte2;
    assign mem[1][24+:8] = drv_byte3;

    assign mem_out0 = mem[0];
    assign mem_out1 = mem[1];
endmodule
