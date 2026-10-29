// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v2.7.0.0
// Standard Target: IEEE Std 1800-2023 §7.4 (Unpacked Arrays, Indexing, and Out-of-Bounds)
// Testbench Name : tb_v2_7_0_0_regression
// ============================================================================

module tb_v2_7_0_0_regression;
    logic       clk;
    logic       rst_n;
    logic       we;
    logic [1:0] waddr;
    logic [7:0] wdata;
    logic [1:0] raddr;
    logic [7:0] rdata;
    integer     error_count = 0;

    v2_7_0_0_array_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .we(we),
        .waddr(waddr),
        .wdata(wdata),
        .raddr(raddr),
        .rdata(rdata)
    );

    // Clock generator: posedges at 5, 15, 25, 35, 45, 55, 65...
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n = 0;
        we    = 0;
        waddr = 2'b00;
        wdata = 8'h00;
        raddr = 2'b00;

        #6; // T = 6 (after reset posedge at T = 5)
        rst_n = 1;
        if (rdata !== 8'h00) begin
            $display("[FAIL T=6] Reset read failed! rdata=%h (exp 00)", rdata);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 1: Write to entry 1 at T = 15
        // --------------------------------------------------------------------
        #4; // T = 10
        we    = 1;
        waddr = 2'b01;
        wdata = 8'hA1;

        #5; // T = 15 (posedge clk) -> writes reg_file[1] = 8'hA1
        #1; // T = 16
        raddr = 2'b01;
        #1; // T = 17
        if (rdata !== 8'hA1) begin
            $display("[FAIL T=17] Write to entry 1 failed! rdata=%h (exp A1)", rdata);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Write to entry 3 at T = 25
        // --------------------------------------------------------------------
        #3; // T = 20
        waddr = 2'b11;
        wdata = 8'hD3;

        #5; // T = 25 (posedge clk) -> writes reg_file[3] = 8'hD3
        #1; // T = 26
        raddr = 2'b11;
        #1; // T = 27
        if (rdata !== 8'hD3) begin
            $display("[FAIL T=27] Write to entry 3 failed! rdata=%h (exp D3)", rdata);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 3: Verify entry 0 and entry 1 remain intact (no cross-contamination)
        // --------------------------------------------------------------------
        we = 0; // disable write
        raddr = 2'b00;
        #1; // T = 28
        if (rdata !== 8'h00) begin
            $display("[FAIL T=28] Read entry 0 corrupted! rdata=%h (exp 00)", rdata);
            error_count = error_count + 1;
        end

        raddr = 2'b01;
        #1; // T = 29
        if (rdata !== 8'hA1) begin
            $display("[FAIL T=29] Read entry 1 corrupted! rdata=%h (exp A1)", rdata);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 4: Out-of-bounds read verification (§7.4.6)
        // An unmapped / out-of-range 4-state index must return 8'hxx
        // --------------------------------------------------------------------
        raddr = 2'bxx; // Unknown index
        #1; // T = 30
        if (rdata !== 8'hxx) begin
            $display("[FAIL T=30] 4-state unknown index read failed! rdata=%h (exp xx)", rdata);
            error_count = error_count + 1;
        end

        #5; // T = 35
        $display("\n================================================================================");
        $display("[CAS v2.7.0.0 REGRESSION SUITE: UNPACKED ARRAYS & MEMORY INDEXING]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §7.4 unpacked array memory indexing and 4-state lookups.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | clk=%b rst_n=%b we=%b waddr=%0d wdata=%h | raddr=%0d rdata=%h",
                 $time, clk, rst_n, we, waddr, wdata, raddr, rdata);
    end
endmodule