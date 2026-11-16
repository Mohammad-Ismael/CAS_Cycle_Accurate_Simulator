`resetall
`timescale 1ns/1ps

module tb_v4_4_0_regression;
    logic clk = 0;
    logic rst_n = 0;
    logic start = 0;
    integer in_fork_val = 0;
    logic [31:0] out_val;
    logic done;

    always #5 clk = ~clk;

    dut_v4_4 dut (
        .clk(clk),
        .rst_n(rst_n),
        .start(start),
        .in_fork_val(in_fork_val),
        .out_val(out_val),
        .done(done)
    );

    // Task containing fork-join block (§9.3.2)
    task automatic run_fork_task();
        fork
            begin
                #2;
                in_fork_val = in_fork_val + 20;
            end
            begin
                #5;
                in_fork_val = in_fork_val + 30;
            end
        join
    endtask

    initial begin
        rst_n = 0;
        start = 0;
        in_fork_val = 0;

        // Exercise fork-join task (§9.3.2)
        run_fork_task(); // in_fork_val becomes 0 + 20 + 30 = 50 at T=5

        #15; // T = 20
        rst_n = 1;
        #10; // T = 30
        start = 1;
        #10; // T = 40
        start = 0;

        // Wait cycles for state machine
        #50; // T = 90

        $display("[TB] in_fork_val = %0d (expected: 50)", in_fork_val);
        $display("[TB] out_val     = %0d (expected: 125)", out_val);
        $display("[TB] done        = %0b (expected: 1)", done);

        if (out_val == 32'd125 && done == 1'b1 && in_fork_val == 50) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
        end else begin
            $display("RESULT : >>> FAIL <<<");
        end
        $finish;
    end
endmodule
