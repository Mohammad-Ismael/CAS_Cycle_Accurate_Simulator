`timescale 1ns/1ps

module tb_v4_0_0_regression;

    logic        clk;
    logic        rst_n;
    logic        test_done;
    logic [31:0] error_count;

    unpacked_descending_slices_dut dut (
        .clk(clk),
        .rst_n(rst_n),
        .test_done(test_done),
        .error_count(error_count)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n = 0;
        #15;
        rst_n = 1;

        repeat (5) @(posedge clk);

        #1;
        if (error_count == 32'd0 && test_done) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
        end else begin
            $display("RESULT : >>> FAIL (%0d errors, done=%b) <<<", error_count, test_done);
        end
        $finish;
    end

endmodule
