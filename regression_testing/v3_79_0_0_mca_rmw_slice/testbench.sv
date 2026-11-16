// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.79.0.0
// Standard Target: IEEE Std 1800-2023 §10.3 (Continuous Assignment to Memory Slices)
// Testbench Name : tb_v3_79_0_regression
// ============================================================================

module tb_v3_79_0_regression;
    logic        clk;
    logic        rst_n;
    logic        drv_bit32;
    logic        drv_bit33;
    logic [7:0]  drv_byte2;
    logic [7:0]  drv_byte3;
    logic [63:0] mem_out0;
    logic [63:0] mem_out1;

    int pass_count  = 0;
    int error_count = 0;

    v3_79_0_0_mca_rmw_slice dut (
        .clk       (clk),
        .rst_n     (rst_n),
        .drv_bit32 (drv_bit32),
        .drv_bit33 (drv_bit33),
        .drv_byte2 (drv_byte2),
        .drv_byte3 (drv_byte3),
        .mem_out0  (mem_out0),
        .mem_out1  (mem_out1)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        drv_bit32 = 0;
        drv_bit33 = 0;
        drv_byte2 = 8'h00;
        drv_byte3 = 8'h00;
        rst_n = 0;
        #15;
        rst_n = 1;
        #10;

        // Test 1: Set bit 32, bit 33 should remain 0
        drv_bit32 = 1;
        #10;
        if (mem_out0[32] === 1'b1 && mem_out0[33] === 1'b0) begin
            $display("[PASS T=%0t] MCA single bit slice 32 set verified", $time);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] MCA bit 32 set failed: bit32=%b bit33=%b", $time, mem_out0[32], mem_out0[33]);
            error_count++;
        end

        // Test 2: Set bit 33, bit 32 MUST NOT be zeroed out (RMW verification)
        drv_bit33 = 1;
        #10;
        if (mem_out0[32] === 1'b1 && mem_out0[33] === 1'b1) begin
            $display("[PASS T=%0t] MCA adjacent bit 33 set with bit 32 preserved", $time);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] MCA adjacent bit 33 set failed: bit32=%b bit33=%b", $time, mem_out0[32], mem_out0[33]);
            error_count++;
        end

        // Test 3: Clear bit 32, bit 33 MUST remain 1
        drv_bit32 = 0;
        #10;
        if (mem_out0[32] === 1'b0 && mem_out0[33] === 1'b1) begin
            $display("[PASS T=%0t] MCA clear bit 32 with bit 33 preserved", $time);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] MCA clear bit 32 failed: bit32=%b bit33=%b", $time, mem_out0[32], mem_out0[33]);
            error_count++;
        end

        // Test 4: Set byte 2, byte 3 should remain 0
        drv_byte2 = 8'hAA;
        #10;
        if (mem_out1[23:16] === 8'hAA && mem_out1[31:24] === 8'h00) begin
            $display("[PASS T=%0t] MCA byte slice 2 verified", $time);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] MCA byte slice 2 failed: %h", $time, mem_out1[31:16]);
            error_count++;
        end

        // Test 5: Set byte 3, byte 2 preserved
        drv_byte3 = 8'h55;
        #10;
        if (mem_out1[23:16] === 8'hAA && mem_out1[31:24] === 8'h55) begin
            $display("[PASS T=%0t] MCA adjacent byte slice 3 verified with byte 2 preserved", $time);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] MCA adjacent byte slice 3 failed: %h", $time, mem_out1[31:16]);
            error_count++;
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.79.0.0 REGRESSION SUITE: MCA RMW SLICES §10.3]");
        if (error_count == 0 && pass_count == 5) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §10.3 Memory Continuous Assignment RMW slices.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 5, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule
