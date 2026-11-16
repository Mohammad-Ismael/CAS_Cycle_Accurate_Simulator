// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.82.0.0
// Standard Target: IEEE Std 1800-2023 §9.4.3 (Level-Sensitive wait Statement)
// Testbench Name : tb_v3_82_0_regression
// ============================================================================

module tb_v3_82_0_regression;
    logic        clk;
    logic        rst_n;
    logic        req;
    logic [15:0] data_in;
    logic        ack;
    logic [15:0] data_out;

    int pass_count  = 0;
    int error_count = 0;

    v3_82_0_0_wait_condition_handshake dut (
        .clk     (clk),
        .rst_n   (rst_n),
        .req     (req),
        .data_in (data_in),
        .ack     (ack),
        .data_out(data_out)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        req = 0;
        data_in = 16'h0;
        rst_n = 0;
        #15;
        rst_n = 1;
        #10;

        // Perform 4 transactions using level-sensitive wait(ack == 1) and wait(ack == 0)
        for (int i = 0; i < 4; i++) begin
            @(posedge clk);
            req = 1;
            data_in = 16'h0100 + i;

            // Wait for DUT to assert ack
            wait (ack == 1'b1);
            if (data_out === (data_in + 16'hA000)) begin
                $display("[PASS T=%0t] Transaction %0d handshake verified: data_out=0x%04x", $time, i, data_out);
                pass_count++;
            end else begin
                $display("[FAIL T=%0t] Transaction %0d failed: data_out=0x%04x (exp 0x%04x)",
                         $time, i, data_out, data_in + 16'hA000);
                error_count++;
            end

            @(posedge clk);
            req = 0;
            // Wait for DUT to deassert ack
            wait (ack == 1'b0);
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.82.0.0 REGRESSION SUITE: LEVEL-SENSITIVE WAIT §9.4.3]");
        if (error_count == 0 && pass_count == 4) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §9.4.3 Level-Sensitive wait statement.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 4, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule
