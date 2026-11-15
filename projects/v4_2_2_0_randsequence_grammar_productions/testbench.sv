`timescale 1ns/1ps

module tb_v4_2_2_regression;
    import v4_2_2_0_pkg::*;

    logic [31:0] mode;
    logic [31:0] out_val;

    randsequence_dut dut (
        .mode(mode),
        .out_val(out_val)
    );

    int errors = 0;

    task check_mode(int m, int expected);
        mode = m;
        #1;
        if (out_val !== expected) begin
            $display("ERROR: Mode %0d expected %0d, got %0d", m, expected, out_val);
            errors++;
        end
    endtask

    initial begin
        #1;
        check_mode(0, 6);
        check_mode(1, 2);
        check_mode(2, 10);
        check_mode(3, 5);
        check_mode(4, 10);
        check_mode(5, 3);
        check_mode(6, 10);
        check_mode(7, 10);
        check_mode(8, 10);
        check_mode(9, 25);
        check_mode(10, 17);

        if (errors == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
        end else begin
            $display("RESULT : >>> FAIL (%0d errors) <<<", errors);
        end
        $finish;
    end

endmodule
