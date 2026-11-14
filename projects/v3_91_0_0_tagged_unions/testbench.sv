`timescale 1ns/1ps

module tb_v3_91_0_regression;
    logic clk;
    int   valid_val;
    logic [7:0] pkt_packed;
    logic [6:0] pkt_data;
    int   anon_mod_val;
    int   anon_proc_val;
    int   status_errors;

    tagged_union_tester dut (
        .clk(clk),
        .out_valid_val(valid_val),
        .out_pkt_packed(pkt_packed),
        .out_pkt_data(pkt_data),
        .out_anon_mod_val(anon_mod_val),
        .out_anon_proc_val(anon_proc_val),
        .status_errors(status_errors)
    );

    initial begin
        clk = 0;
        #1;

        if (status_errors == 0 &&
            valid_val == -99 &&
            pkt_packed == 8'hD5 &&
            pkt_data == 7'b1010101 &&
            anon_mod_val == 12345 &&
            anon_proc_val == 67890) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
        end else begin
            $display("RESULT : >>> FAIL <<< (status_errors=%0d)", status_errors);
        end
        $finish;
    end
endmodule
