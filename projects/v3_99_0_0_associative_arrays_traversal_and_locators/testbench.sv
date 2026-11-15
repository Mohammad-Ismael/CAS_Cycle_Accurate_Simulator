// IEEE 1800-2023 §7.9 & §7.12.1 Regression Testbench
module tb_v3_99_0_regression;

    logic        clk;
    logic        rst_n;
    logic [31:0] err_count;

    assoc_traversal_locators_dut dut (
        .clk(clk),
        .rst_n(rst_n),
        .err_count(err_count)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n = 0;
        #20;
        rst_n = 1;

        #20;
        @(posedge clk);
        #1;

        if (err_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
        end else begin
            $display("RESULT : >>> FAIL (%0d errors detected) <<<", err_count);
        end

        $finish;
    end

endmodule
