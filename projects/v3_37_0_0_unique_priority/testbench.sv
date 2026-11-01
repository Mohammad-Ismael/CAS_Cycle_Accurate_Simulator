// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.37.0.0
// Standard Target: IEEE Std 1800-2023 §12.5 (Unique, Unique0, Priority If/Case)
// Testbench Name : tb_v3_37_0_0_regression
// ============================================================================

module tb_v3_37_0_0_regression;
    logic       clk;
    logic       rst_n;
    logic [3:0] req_vec;
    logic [1:0] mode_sel;
    logic [1:0] grant_idx;
    logic [7:0] mode_code;
    logic       grant_valid;

    int pass_count  = 0;
    int error_count = 0;

    v3_37_0_0_decision_core dut (
        .clk        (clk),
        .rst_n      (rst_n),
        .req_vec    (req_vec),
        .mode_sel   (mode_sel),
        .grant_idx  (grant_idx),
        .mode_code  (mode_code),
        .grant_valid(grant_valid)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    // Procedural helper verifying 'unique if' and 'unique0 if' at testbench level
    function automatic int check_unique_decisions(int val);
        int score = 0;

        // Unique if: mutually exclusive branches
        unique if (val == 1) score = 10;
        else if (val == 2)   score = 20;
        else if (val == 3)   score = 30;
        else                 score = 0;

        return score;
    endfunction

    function automatic int check_unique0_decisions(int val);
        int out_val = 100;

        // Unique0 if: allows zero matches without warning
        unique0 if (val == 1) out_val = 1;
        else if (val == 2)    out_val = 2;

        return out_val;
    endfunction

    initial begin
        rst_n    = 0;
        req_vec  = 4'b0000;
        mode_sel = 2'b00;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Priority Case Evaluation
        // req_vec = 4'b1010 -> both req[1] and req[3] asserted.
        // Priority case selects first matching branch: req[1] -> grant_idx = 1
        // --------------------------------------------------------------------
        #1;
        req_vec = 4'b1010;
        #1;
        if (grant_idx === 2'd1 && grant_valid === 1'b1) begin
            $display("[PASS T=%0t] Priority case arbitration verified: req=4'b1010 -> grant=%0d",
                     $time, grant_idx);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Priority case failed! got grant=%0d valid=%b (exp grant=1)",
                     $time, grant_idx, grant_valid);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: Unique Case Decoding
        // mode_sel = 2'b10 -> mode_code = 8'h44
        // --------------------------------------------------------------------
        mode_sel = 2'b10;
        #1;
        if (mode_code === 8'h44) begin
            $display("[PASS T=%0t] Unique case decoding verified: mode_sel=2'b10 -> code=%02h",
                     $time, mode_code);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Unique case failed! got code=%02h (exp 44)",
                     $time, mode_code);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Standalone Unique & Unique0 If Execution
        // --------------------------------------------------------------------
        begin
            int s_u, s_u0;
            s_u  = check_unique_decisions(2);   // 20
            s_u0 = check_unique0_decisions(99); // No match -> remains 100

            if (s_u == 20 && s_u0 == 100) begin
                $display("[PASS T=%0t] Procedural unique/unique0 if verified: s_u=%0d s_u0=%0d",
                         $time, s_u, s_u0);
                pass_count++;
            end else begin
                $display("[FAIL T=%0t] Procedural unique/unique0 if failed! s_u=%0d (exp 20) s_u0=%0d (exp 100)",
                     $time, s_u, s_u0);
                error_count++;
            end
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.37.0.0 REGRESSION SUITE: UNIQUE & PRIORITY DECISIONS §12.5]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §12.5 priority case, unique case, unique if, and unique0 if.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | rst_n=%b req=%b mode=%b | grant=%0d valid=%b code=%02h",
                 $time, rst_n, req_vec, mode_sel, grant_idx, grant_valid, mode_code);
    end
endmodule