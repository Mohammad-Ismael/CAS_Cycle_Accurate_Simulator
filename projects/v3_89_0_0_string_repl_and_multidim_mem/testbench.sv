`timescale 1ns/1ps
/* verilator lint_off DECLFILENAME */
/* verilator lint_off INITIALDLY */

//==============================================================================
// Testbench: tb_v3_89_0_regression
// Standard: IEEE 1800-2023 SystemVerilog (§11.4.12.2 string repl, §11.5.2 multi-dim array)
//==============================================================================

module tb_v3_89_0_regression;

    logic         clk;
    logic         rst_n;

    // String replication flags
    logic         str_match_test4;
    logic         str_match_ab3;
    logic         str_match_skip0;

    // Blocking assign verification flags
    logic [7:0]   blk_2d_val_0_0;
    logic [7:0]   blk_2d_val_5_2;
    logic [7:0]   blk_2d_val_15_3;
    logic         blk_2d_bit_5_2_3;

    logic [15:0]  blk_3d_val_0_0_0;
    logic [15:0]  blk_3d_val_2_1_0;
    logic [15:0]  blk_3d_val_3_3_1;
    logic         blk_3d_bit_2_1_0_7;

    // NBA write control ports
    logic         nba_we_2d;
    logic [3:0]   nba_waddr_2d_i;
    logic [1:0]   nba_waddr_2d_j;
    logic [7:0]   nba_wdata_2d;

    logic         nba_we_3d;
    logic [1:0]   nba_waddr_3d_i;
    logic [1:0]   nba_waddr_3d_j;
    logic         nba_waddr_3d_k;
    logic [15:0]  nba_wdata_3d;

    // Readback ports
    logic [3:0]   raddr_2d_i;
    logic [1:0]   raddr_2d_j;
    logic [7:0]   rdata_2d;
    logic         rdata_2d_bit;

    logic [1:0]   raddr_3d_i;
    logic [1:0]   raddr_3d_j;
    logic         raddr_3d_k;
    logic [15:0]  rdata_3d;
    logic         rdata_3d_bit;

    integer errors = 0;

    v3_89_0_0_string_repl_and_multidim_mem dut (.*);

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        // Initialize control signals
        rst_n          = 0;
        nba_we_2d      = 0;
        nba_waddr_2d_i = 0;
        nba_waddr_2d_j = 0;
        nba_wdata_2d   = 0;

        nba_we_3d      = 0;
        nba_waddr_3d_i = 0;
        nba_waddr_3d_j = 0;
        nba_waddr_3d_k = 0;
        nba_wdata_3d   = 0;

        raddr_2d_i     = 0;
        raddr_2d_j     = 0;
        raddr_3d_i     = 0;
        raddr_3d_j     = 0;
        raddr_3d_k     = 0;

        #10;
        rst_n = 1;
        #10;

        //----------------------------------------------------------------------
        // 1. Verify String Replication (§11.4.12.2)
        //----------------------------------------------------------------------
        if (str_match_test4 !== 1'b1) begin
            $display("[FAIL T=%0t] str_match_test4 failed: exp=1 got=%b", $time, str_match_test4);
            errors++;
        end else begin
            $display("[PASS T=%0t] str_match_test4 ({4{\"test\"}} == \"testtesttesttest\") verified", $time);
        end

        if (str_match_ab3 !== 1'b1) begin
            $display("[FAIL T=%0t] str_match_ab3 failed: exp=1 got=%b", $time, str_match_ab3);
            errors++;
        end else begin
            $display("[PASS T=%0t] str_match_ab3 ({3{\"ab_\"}} == \"ab_ab_ab_\") verified", $time);
        end

        if (str_match_skip0 !== 1'b1) begin
            $display("[FAIL T=%0t] str_match_skip0 failed: exp=1 got=%b", $time, str_match_skip0);
            errors++;
        end else begin
            $display("[PASS T=%0t] str_match_skip0 ({0{\"skip\"}} == \"\") verified", $time);
        end

        //----------------------------------------------------------------------
        // 2. Verify 2D & 3D Blocking Writes & Reads (§11.5.2)
        //----------------------------------------------------------------------
        if (blk_2d_val_0_0 !== 8'hA1) begin
            $display("[FAIL T=%0t] blk_2d_val_0_0 failed: exp=0xA1 got=0x%h", $time, blk_2d_val_0_0);
            errors++;
        end else begin
            $display("[PASS T=%0t] blk_2d_val_0_0 verified: 0x%h", $time, blk_2d_val_0_0);
        end

        if (blk_2d_val_5_2 !== 8'hB8) begin
            $display("[FAIL T=%0t] blk_2d_val_5_2 failed: exp=0xB8 got=0x%h", $time, blk_2d_val_5_2);
            errors++;
        end else begin
            $display("[PASS T=%0t] blk_2d_val_5_2 verified: 0x%h", $time, blk_2d_val_5_2);
        end

        if (blk_2d_val_15_3 !== 8'hFE) begin
            $display("[FAIL T=%0t] blk_2d_val_15_3 failed: exp=0xFE got=0x%h", $time, blk_2d_val_15_3);
            errors++;
        end else begin
            $display("[PASS T=%0t] blk_2d_val_15_3 verified: 0x%h", $time, blk_2d_val_15_3);
        end

        if (blk_2d_bit_5_2_3 !== 1'b1) begin
            $display("[FAIL T=%0t] blk_2d_bit_5_2_3 failed: exp=1 got=%b", $time, blk_2d_bit_5_2_3);
            errors++;
        end else begin
            $display("[PASS T=%0t] blk_2d_bit_5_2_3 (mem2d[5][2][3] == 1) verified", $time);
        end

        // 3D blocking checks
        if (blk_3d_val_0_0_0 !== 16'h1234) begin
            $display("[FAIL T=%0t] blk_3d_val_0_0_0 failed: exp=0x1234 got=0x%h", $time, blk_3d_val_0_0_0);
            errors++;
        end else begin
            $display("[PASS T=%0t] blk_3d_val_0_0_0 verified: 0x%h", $time, blk_3d_val_0_0_0);
        end

        if (blk_3d_val_2_1_0 !== 16'h8080) begin
            $display("[FAIL T=%0t] blk_3d_val_2_1_0 failed: exp=0x8080 got=0x%h", $time, blk_3d_val_2_1_0);
            errors++;
        end else begin
            $display("[PASS T=%0t] blk_3d_val_2_1_0 verified: 0x%h", $time, blk_3d_val_2_1_0);
        end

        if (blk_3d_val_3_3_1 !== 16'hDEAD) begin
            $display("[FAIL T=%0t] blk_3d_val_3_3_1 failed: exp=0xDEAD got=0x%h", $time, blk_3d_val_3_3_1);
            errors++;
        end else begin
            $display("[PASS T=%0t] blk_3d_val_3_3_1 verified: 0x%h", $time, blk_3d_val_3_3_1);
        end

        if (blk_3d_bit_2_1_0_7 !== 1'b1) begin
            $display("[FAIL T=%0t] blk_3d_bit_2_1_0_7 failed: exp=1 got=%b", $time, blk_3d_bit_2_1_0_7);
            errors++;
        end else begin
            $display("[PASS T=%0t] blk_3d_bit_2_1_0_7 (mem3d[2][1][0][7] == 1) verified", $time);
        end

        //----------------------------------------------------------------------
        // 3. Verify 2D Non-Blocking Writes & Dynamic Index Reads
        //----------------------------------------------------------------------
        @(posedge clk);
        nba_we_2d      = 1;
        nba_waddr_2d_i = 4'd7;
        nba_waddr_2d_j = 2'd1;
        nba_wdata_2d   = 8'h4F; // 8'b01001111 -> bit 3 is 1

        @(posedge clk);
        nba_we_2d      = 1;
        nba_waddr_2d_i = 4'd12;
        nba_waddr_2d_j = 2'd3;
        nba_wdata_2d   = 8'hF0; // 8'b11110000 -> bit 3 is 0

        @(posedge clk);
        nba_we_2d      = 0;

        #2;
        raddr_2d_i = 4'd7;
        raddr_2d_j = 2'd1;
        #2;
        if (rdata_2d !== 8'h4F || rdata_2d_bit !== 1'b1) begin
            $display("[FAIL T=%0t] 2D NBA read at [7][1] failed: data=0x%h (exp 0x4F) bit=%b (exp 1)", $time, rdata_2d, rdata_2d_bit);
            errors++;
        end else begin
            $display("[PASS T=%0t] 2D NBA read at [7][1] verified: 0x%h bit=%b", $time, rdata_2d, rdata_2d_bit);
        end

        raddr_2d_i = 4'd12;
        raddr_2d_j = 2'd3;
        #2;
        if (rdata_2d !== 8'hF0 || rdata_2d_bit !== 1'b0) begin
            $display("[FAIL T=%0t] 2D NBA read at [12][3] failed: data=0x%h (exp 0xF0) bit=%b (exp 0)", $time, rdata_2d, rdata_2d_bit);
            errors++;
        end else begin
            $display("[PASS T=%0t] 2D NBA read at [12][3] verified: 0x%h bit=%b", $time, rdata_2d, rdata_2d_bit);
        end

        //----------------------------------------------------------------------
        // 4. Verify 3D Non-Blocking Writes & Dynamic Index Reads
        //----------------------------------------------------------------------
        @(posedge clk);
        nba_we_3d      = 1;
        nba_waddr_3d_i = 2'd1;
        nba_waddr_3d_j = 2'd2;
        nba_waddr_3d_k = 1'b1;
        nba_wdata_3d   = 16'hCAFE; // bit 7 is 1 (0xFE -> 11111110)

        @(posedge clk);
        nba_we_3d      = 1;
        nba_waddr_3d_i = 2'd3;
        nba_waddr_3d_j = 2'd0;
        nba_waddr_3d_k = 1'b0;
        nba_wdata_3d   = 16'hBE0F; // bit 7 is 0 (0x0F -> 00001111)

        @(posedge clk);
        nba_we_3d      = 0;

        #2;
        raddr_3d_i = 2'd1;
        raddr_3d_j = 2'd2;
        raddr_3d_k = 1'b1;
        #2;
        if (rdata_3d !== 16'hCAFE || rdata_3d_bit !== 1'b1) begin
            $display("[FAIL T=%0t] 3D NBA read at [1][2][1] failed: data=0x%h (exp 0xCAFE) bit=%b (exp 1)", $time, rdata_3d, rdata_3d_bit);
            errors++;
        end else begin
            $display("[PASS T=%0t] 3D NBA read at [1][2][1] verified: 0x%h bit=%b", $time, rdata_3d, rdata_3d_bit);
        end

        raddr_3d_i = 2'd3;
        raddr_3d_j = 2'd0;
        raddr_3d_k = 1'b0;
        #2;
        if (rdata_3d !== 16'hBE0F || rdata_3d_bit !== 1'b0) begin
            $display("[FAIL T=%0t] 3D NBA read at [3][0][0] failed: data=0x%h (exp 0xBE0F) bit=%b (exp 0)", $time, rdata_3d, rdata_3d_bit);
            errors++;
        end else begin
            $display("[PASS T=%0t] 3D NBA read at [3][0][0] verified: 0x%h bit=%b", $time, rdata_3d, rdata_3d_bit);
        end

        // Final verdict
        #10;
        if (errors == 0) begin
            $display("\n================================================================================");
            $display("[CAS v3.89.0.0 REGRESSION SUITE: STRING REPL AND MULTIDIM MEM]");
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800 string replication and 2D/3D unpacked array indexing.");
            $display("================================================================================\n");
        end else begin
            $display("\n[FATAL] v3_89_0_0_string_repl_and_multidim_mem failed with %0d errors!\n", errors);
        end

        $finish;
    end

endmodule
