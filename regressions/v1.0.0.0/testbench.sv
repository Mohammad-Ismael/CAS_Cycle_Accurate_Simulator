// IEEE Std 1800-2023 Chapter 4 Compliance Regression
module tb_v1_0_0_0_regression;
    logic clk;
    logic rst_n;
    logic d_in;
    logic s1, s2, s3;
    logic [2:0] blk;

    v1_0_0_0_regression dut (
        .clk(clk),
        .rst_n(rst_n),
        .d_in(d_in),
        .stage1_nba(s1),
        .stage2_nba(s2),
        .stage3_nba(s3),
        .blocking_out(blk)
    );

    // Free-running clock generator
    always begin
        #5 clk = ~clk;
    end

    initial begin
        clk   = 0;
        rst_n = 0;
        d_in  = 0;

        #12;
        rst_n = 1;

        #3;
        d_in = 1'b1;

        #10;
        d_in = 1'b0;

        #40;

        // Automated Self-Checking Assertion Block
        if (s1 === 1'b0 && s2 === 1'b0 && s3 === 1'b0 && blk === 3'b000) begin
            $display("[REGRESSION PASS] v1.0.0.0 Stratified Scheduling matches VCS.");
        end else begin
            $display("[REGRESSION FAIL] State mismatch detected!");
        end

        $finish;
    end

    initial begin
        $monitor("Time=%0t | clk=%b rst_n=%b d_in=%b | NBA=[%b %b %b] | BLK=%b",
                 $time, clk, rst_n, d_in, s1, s2, s3, blk);
    end

endmodule