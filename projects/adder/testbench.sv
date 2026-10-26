module tb_counter;

    logic       clk;
    logic       rst_n;
    logic [3:0] d;
    logic [3:0] q;

    counter u_dut (
        .clk(clk),
        .rst_n(rst_n),
        .d(d),
        .q(q)
    );

    initial begin
        $dumpfile("counter_wave2.vcd");
        $dumpvars;

        clk = 0;
        rst_n = 0;
        d = 4'd3;

        #5;
        $display("[Time=%0t] Reset active -> q=%d", $time, q);

        rst_n = 1;
        #5;

        clk = 1;
        #5;
        $display("[Time=%0t] Clock Edge 1 -> q=%d", $time, q);
        clk = 0;
        #5;

        d = 4'd2;
        clk = 1;
        #5;
        $display("[Time=%0t] Clock Edge 2 -> q=%d", $time, q);
        clk = 0;
        #5;

        $finish;
    end

endmodule