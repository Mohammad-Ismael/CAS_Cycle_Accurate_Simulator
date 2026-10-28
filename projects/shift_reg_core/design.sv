module shift_reg_core (
    input  logic clk,
    input  logic rst_n,
    input  logic d_in,
    output logic stage1_nba,
    output logic stage2_nba,
    output logic stage3_nba,
    output logic [2:0] blocking_out
);

    // IEEE 1800-compliant NBA Shift Register
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            stage1_nba <= 1'b0;
            stage2_nba <= 1'b0;
            stage3_nba <= 1'b0;
        end else begin
            stage1_nba <= d_in;
            stage2_nba <= stage1_nba;
            stage3_nba <= stage2_nba;
        end
    end

    // Blocking assignment chain
    logic b1, b2, b3;
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            b1 <= 1'b0;
            b2 <= 1'b0;
            b3 <= 1'b0;
        end else begin
            b1 = d_in;
            b2 = b1;
            b3 = b2;
        end
    end
    assign blocking_out = {b3, b2, b1};

endmodule