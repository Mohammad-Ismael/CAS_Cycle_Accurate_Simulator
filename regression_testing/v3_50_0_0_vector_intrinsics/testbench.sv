`timescale 1ns/1ps

// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.50.0.0
// Standard Target: IEEE Std 1800-2023 §20.9 (Vector Intrinsic Query Functions)
// Testbench Name : tb_v3_50_0_regression
// ============================================================================

module tb_v3_50_0_regression;
    import v3_50_0_0_pkg::*;

    logic       clk;
    logic       rst_n;
    logic [3:0] req;
    logic [3:0] gnt;
    logic [2:0] active_req_count;
    logic       single_grant_valid;
    logic       grant_or_idle_valid;
    logic       bus_has_unknowns;

    int pass_count  = 0;
    int error_count = 0;

    v3_50_0_0_arbiter_checker dut (
        .clk                (clk),
        .rst_n              (rst_n),
        .req                (req),
        .gnt                (gnt),
        .active_req_count   (active_req_count),
        .single_grant_valid (single_grant_valid),
        .grant_or_idle_valid(grant_or_idle_valid),
        .bus_has_unknowns   (bus_has_unknowns)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n = 0;
        req   = 4'b0000;
        gnt   = 4'b0000;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Procedural evaluation of all four intrinsics
        // --------------------------------------------------------------------
        begin
            logic [7:0] vec_test;
            vec_test = 8'b0010_1001; // three ones

            if ($countones(vec_test) == 3 &&
                $onehot(8'b0000_0100) == 1'b1 &&
                $onehot(8'b0000_0000) == 1'b0 &&
                $onehot0(8'b0000_0000) == 1'b1 &&
                $onehot0(8'b0000_0010) == 1'b1 &&
                $onehot0(8'b0000_0011) == 1'b0 &&
                $isunknown(8'b0000_0000) == 1'b0 &&
                $isunknown(8'b0000_001x) == 1'b1 &&
                $isunknown(8'b0000_001z) == 1'b1) begin
                $display("[PASS T=%0t] Procedural vector intrinsics verified: countones, onehot, onehot0, isunknown", $time);
                pass_count++;
            end else begin
                $display("[FAIL T=%0t] Procedural vector intrinsics failed!", $time);
                error_count++;
            end
        end

        // --------------------------------------------------------------------
        // Test 2: DUT sequential execution - single grant (valid one-hot)
        // req = 4'b1011 (3 active requests), gnt = 4'b0010 (valid single grant)
        // --------------------------------------------------------------------
        @(negedge clk);
        req = 4'b1011;
        gnt = 4'b0010;

        @(posedge clk);
        #1;
        if (active_req_count === 3'd3 &&
            single_grant_valid === 1'b1 &&
            grant_or_idle_valid === 1'b1 &&
            bus_has_unknowns === 1'b0) begin
            $display("[PASS T=%0t] DUT single grant state verified: req_cnt=%0d onehot=%b onehot0=%b unknown=%b",
                     $time, active_req_count, single_grant_valid, grant_or_idle_valid, bus_has_unknowns);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] DUT single grant state failed! req_cnt=%0d onehot=%b onehot0=%b unknown=%b",
                     $time, active_req_count, single_grant_valid, grant_or_idle_valid, bus_has_unknowns);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: DUT sequential execution - idle bus clear then unknown check
        // Cycle A: drive idle 4'b0000, 4'b0000
        // --------------------------------------------------------------------
        @(negedge clk);
        req = 4'b0000;
        gnt = 4'b0000;

        @(posedge clk);
        #1;
        // Cycle B: drive unknown {req=4'b0001, gnt=4'b000x}
        @(negedge clk);
        req = 4'b0001;
        gnt = 4'b000x;

        @(posedge clk);
        #1;
        if (bus_has_unknowns === 1'b1 &&
            single_grant_valid === 1'b0 &&
            grant_or_idle_valid === 1'b1 && // <-- expects 1
            active_req_count === 3'd1) begin
            $display("[PASS T=%0t] DUT 4-state unknown detection verified: unknown=%b onehot=%b onehot0=%b",
                     $time, bus_has_unknowns, single_grant_valid, grant_or_idle_valid);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] DUT 4-state unknown detection failed! req=%b gnt=%b unknown=%b onehot=%b onehot0=%b cnt=%0d",
                     $time, req, gnt, bus_has_unknowns, single_grant_valid, grant_or_idle_valid, active_req_count);
            error_count++;
        end

        @(negedge clk);

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.50.0.0 REGRESSION SUITE: VECTOR INTRINSICS §20.9]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §20.9 $countones, $onehot, $onehot0, and $isunknown.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule