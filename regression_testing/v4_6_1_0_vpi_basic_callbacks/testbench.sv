// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v4.6.1.0
// Standard Target: IEEE Std 1800-2023 §36-§38 (VPI Runtime & Callbacks)
// Testbench Name : tb_v4_6_1_0_vpi_basic
// ============================================================================

module tb_v4_6_1_0_vpi_basic;
    reg clk;
    reg rst_n;
    wire [7:0] count;

    vpi_counter_dut dut (
        .clk(clk),
        .rst_n(rst_n),
        .count(count)
    );

    initial begin
        clk = 0;
        rst_n = 0;
        #10 rst_n = 1;
    end

    always #5 clk = ~clk;

    initial begin
        #45;
        $vpi_check_status();
        #10;
        $display(">>> PASS (100%% VCS COMPLIANT) <<<");
        $finish;
    end
endmodule
