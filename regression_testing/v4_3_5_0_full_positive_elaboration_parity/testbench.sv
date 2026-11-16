module testbench;
    logic clk = 0;
    logic rst_n = 0;
    logic en = 0;
    logic [7:0] in_a = 8'hA5;
    logic [7:0] in_b = 8'h5A;
    logic [7:0] out_concat_slice;
    int out_loop_sum;
    int out_dev_val;

    always #5 clk = ~clk;

    dut dut_inst (
        .clk(clk),
        .rst_n(rst_n),
        .en(en),
        .in_a(in_a),
        .in_b(in_b),
        .out_concat_slice(out_concat_slice),
        .out_loop_sum(out_loop_sum),
        .out_dev_val(out_dev_val)
    );

    initial begin
        rst_n = 0;
        en = 1;
        #15;
        rst_n = 1;
        #10;
        $display("[TB] out_concat_slice = 0x%02h (expected: 0x55)", out_concat_slice);
        $display("[TB] out_loop_sum     = %0d (expected: 10)", out_loop_sum);
        $display("[TB] out_dev_val      = %0d (expected: 141)", out_dev_val);

        if (out_concat_slice == 8'h55 && out_loop_sum == 10 && out_dev_val == 141) begin
            $display("TEST PASSED");
        end else begin
            $display("TEST FAILED");
        end
        $finish;
    end
endmodule
