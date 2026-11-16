// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v1.9.0.0
// Standard Target: IEEE Std 1800-2023 §10.6.2 (Event-Based Intra-Assignments)
// Testbench Name : tb_v1_9_0_0_regression
// ============================================================================

module tb_v1_9_0_0_regression;
    logic       clk;
    logic [7:0] d_in;
    logic [7:0] q_nba;
    logic [7:0] q_blk;
    integer     error_count = 0;

    v1_9_0_0_intra_event_core dut (
        .clk(clk),
        .d_in(d_in),
        .q_nba(q_nba),
        .q_blk(q_blk)
    );

    // Clock generator: posedges at 5, 15, 25, 35...
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        d_in = 8'h10;

        #3; // T=3: Modify d_in. Sampled value from T=0 must remain intact.
        d_in = 8'h99;

        #3; // T=6: Shortly after posedge at T=5
        if (q_nba !== 8'h11) begin
            $display("[FAIL T=6] Intra-event NBA sampling failed! q_nba=%h (exp 11)", q_nba);
            error_count = error_count + 1;
        end
        if (q_blk !== 8'h20) begin
            $display("[FAIL T=6] Intra-event Blocking assignment failed! q_blk=%h (exp 20)", q_blk);
            error_count = error_count + 1;
        end

        #7; // T=13: Change d_in before posedge at T=15
        d_in = 8'h50;

        #3; // T=16: Shortly after posedge at T=15
        if (q_nba !== 8'h9a) begin
            $display("[FAIL T=16] Second cycle intra-event NBA failed! q_nba=%h (exp 9a)", q_nba);
            error_count = error_count + 1;
        end
        if (q_blk !== 8'ha9) begin
            $display("[FAIL T=16] Second cycle intra-event Blocking failed! q_blk=%h (exp a9)", q_blk);
            error_count = error_count + 1;
        end

        #10; // T=26: Shortly after posedge at T=25
        if (q_nba !== 8'h51 || q_blk !== 8'h60) begin
            $display("[FAIL T=26] Third cycle check failed! q_nba=%h q_blk=%h (exp 51, 60)", q_nba, q_blk);
            error_count = error_count + 1;
        end

        #9; // T=35
        $display("\n================================================================================");
        $display("[CAS v1.9.0.0 REGRESSION SUITE: INTRA-ASSIGNMENT EVENT CONTROLS]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §10.6.2 intra-assignment event controls.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | clk=%b d_in=%h | q_nba=%h q_blk=%h",
                 $time, clk, d_in, q_nba, q_blk);
    end
endmodule