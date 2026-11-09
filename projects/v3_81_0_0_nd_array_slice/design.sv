// ============================================================================
// CAS Milestone Regression Design
// Target Release : v3.81.0.0
// Standard Target: IEEE Std 1800-2023 §7.4 (Multidimensional Packed Arrays)
// Design Name    : v3_81_0_0_nd_array_slice
// ============================================================================

package v3_81_0_0_pkg;
endpackage

module v3_81_0_0_nd_array_slice (
    input  logic clk,
    input  logic rst_n,
    input  logic [1:0][3:0][15:0] arr3d_in,
    output logic [7:0]  slice_comb,
    output logic [15:0] word_comb,
    output logic [7:0]  slice_seq
);
    // 3D packed array indexing and part-select slicing
    assign slice_comb = arr3d_in[1][2][11:4];
    assign word_comb  = arr3d_in[0][3];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            slice_seq <= 8'h0;
        end else begin
            slice_seq <= arr3d_in[0][1][15:8];
        end
    end
endmodule
