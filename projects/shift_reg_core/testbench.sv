module tb_shift_reg;
    logic clk;
    logic rst_n;
    logic d_in;
    logic s1, s2, s3;
    logic [2:0] blk;

    shift_reg_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .d_in(d_in),
        .stage1_nba(s1),
        .stage2_nba(s2),
        .stage3_nba(s3),
        .blocking_out(blk)
    );

    // Clock generator: 10ns period (5ns half-period)
    always begin
        #5 clk = ~clk;
    end

    initial begin
        clk   = 0;
        rst_n = 0;
        d_in  = 0;

        // Release reset at 12ns
        #12;
        rst_n = 1;

        // Align with falling edge at 15ns and assert d_in
        #3;
        d_in = 1'b1;

        // Next falling edge at 25ns: de-assert d_in (single-cycle pulse)
        #10;
        d_in = 1'b0;

        // Wait 4 clock cycles (40ns) to observe pulse shift through all 3 stages
        #40;

        $display("\n==========================================");
        $display("[TB FINISH] Final NBA Shift Register: s1=%b, s2=%b, s3=%b", s1, s2, s3);
        $display("[TB FINISH] Final Blocking Output: %b", blk);
        $display("==========================================\n");
        $finish;
    end

    // Monitor output at Postponed region
    initial begin
        $monitor("Time=%0t | clk=%b rst_n=%b d_in=%b | NBA=[%b %b %b] | BLK=%b",
                 $time, clk, rst_n, d_in, s1, s2, s3, blk);
    end

endmodule