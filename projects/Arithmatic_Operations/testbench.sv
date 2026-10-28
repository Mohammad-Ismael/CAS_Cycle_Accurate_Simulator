module tb_counter;
    logic clk;
    logic rst_n;
    logic [3:0] d;
    logic [3:0] q;

    counter u_dut (.clk(clk), .rst_n(rst_n), .d(d), .q(q));

    initial begin
        $dumpfile("counter.vcd");
        $dumpvars;
        clk = 0; rst_n = 0; d = 4'd2;
        #5 rst_n = 1;
        #5 clk = 1; #5 clk = 0;
        #5 clk = 1; #5 clk = 0;
        #5 $finish;
    end
endmodule