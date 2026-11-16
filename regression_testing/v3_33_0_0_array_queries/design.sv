// ============================================================================
// CAS Milestone Regression Design
// Target Release : v3.33.0.0
// Standard Target: IEEE Std 1800-2023 §7.11 (Array Querying Functions)
// Module Name    : v3_33_0_0_array_queries
// ============================================================================

module v3_33_0_0_array_queries (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        trig,
    output int          cells,
    output logic        valid_out
);

    // Multi-dimensional array with 2 unpacked dimensions and 1 packed dimension
    // Dimension 1 (unpacked): [0:3]  -> left=0, right=3, size=4, increment=-1
    // Dimension 2 (unpacked): [7:0]  -> left=7, right=0, size=8, increment=1
    // Dimension 3 (packed):   [15:0] -> left=15, right=0, size=16, increment=1
    logic [15:0] buffer_matrix [0:3][7:0];

    // Compute total cells via $size queries: 4 * 8 = 32
    assign cells = $size(buffer_matrix, 1) * $size(buffer_matrix, 2);

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            valid_out <= 1'b0;
        end else begin
            if (trig) begin
                valid_out <= ($dimensions(buffer_matrix) == 3) && ($unpacked_dimensions(buffer_matrix) == 2) &&
                             ($increment(buffer_matrix, 1) == -1) && ($increment(buffer_matrix, 2) == 1);
            end else begin
                valid_out <= 1'b0;
            end
        end
    end

endmodule
