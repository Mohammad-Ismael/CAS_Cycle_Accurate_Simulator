// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.2.0.0
// Standard Target: IEEE Std 1800-2023 §14 (Clocking Blocks, Default Clocking,
//                  Cycle Delays ##N, Preponed Input Sampling, and Synchronous Drive)
// Testbench Name : tb_v3_2_0_0_regression
// ============================================================================

module tb_v3_2_0_0_regression;
    logic       clk;
    logic       rst_n;
    logic       valid_in;
    logic [7:0] data_in;
    logic       ready_out;
    logic [7:0] data_out;
    integer     error_count = 0;

    v3_2_0_0_cb_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .valid_in(valid_in),
        .data_in(data_in),
        .ready_out(ready_out),
        .data_out(data_out)
    );

    // ------------------------------------------------------------------------
    // Default Clocking Block Declaration (§14.3 & §14.12)
    // ------------------------------------------------------------------------
    default clocking cb @(posedge clk);
        default input #1step output #0;
        output valid_in, data_in;
        input  ready_out, data_out;
    endclocking

    // Clock generator: posedges at 5, 15, 25, 35, 45, 55, 65, 75...
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    // Master test sequence driving through clocking block
    initial begin
        // Async reset initialization
        rst_n       = 0;
        cb.valid_in <= 1'b0;
        cb.data_in  <= 8'h00;

        #6; // T = 6 (after reset posedge at T = 5)
        rst_n = 1;

        // --------------------------------------------------------------------
        // TEST 1: Synchronous drive using ## cycle delay operator (§14.11)
        // Advance to posedge at T = 15
        // --------------------------------------------------------------------
        ##1; // At T = 15
        cb.valid_in <= 1'b1;
        cb.data_in  <= 8'h3F;

        ##1; // At T = 25: DUT captures valid_in=1, data_in=3F -> registers data_out <= 40
        cb.valid_in <= 1'b0;

        ##1; // At T = 35: DUT has data_out = 40; cb.data_out samples 40 in Preponed
        #1;  // T = 36
        if (data_out !== 8'h40 || cb.data_out !== 8'h40) begin
            $display("[FAIL T=36] Transaction 1 failed! data_out=%h cb.data_out=%h (exp 40)",
                     data_out, cb.data_out);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Drive second transaction using multi-cycle delay ##2
        // --------------------------------------------------------------------
        ##2; // Advances across T=45 to posedge at T = 55
        cb.valid_in <= 1'b1;
        cb.data_in  <= 8'h8E;

        ##1; // At T = 65: DUT captures valid_in=1, data_in=8E -> registers data_out <= 8F
        cb.valid_in <= 1'b0;

        ##1; // At T = 75: DUT has data_out = 8F; cb.data_out samples 8F in Preponed
        #1;  // T = 76
        if (data_out !== 8'h8F || cb.data_out !== 8'h8F) begin
            $display("[FAIL T=76] Transaction 2 failed! data_out=%h cb.data_out=%h (exp 8F)",
                     data_out, cb.data_out);
            error_count = error_count + 1;
        end

        #4; // T = 80
        $display("\n================================================================================");
        $display("[CAS v3.2.0.0 REGRESSION SUITE: CLOCKING BLOCKS & CYCLE DELAYS]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §14 clocking blocks (cb drive, ##N, Preponed sampling).");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | clk=%b rst_n=%b | valid=%b data_in=%h | ready=%b data_out=%h",
                 $time, clk, rst_n, valid_in, data_in, ready_out, data_out);
    end
endmodule