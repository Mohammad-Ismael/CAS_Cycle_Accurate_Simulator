// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.36.0.0
// Standard Target: IEEE Std 1800-2023 §27 (Generate Constructs)
// Testbench Name : tb_v3_36_0_0_regression
// ============================================================================

module tb_v3_36_0_0_regression;
    logic        clk;
    logic        rst_n;
    logic [7:0]  piped_operands [0:3];
    logic [11:0] piped_result;

    logic [7:0]  comb_operands [0:3];
    logic [11:0] comb_result;

    int pass_count  = 0;
    int error_count = 0;

    // Instance 1: Pipelined adder tree (ENABLE_PIPE=1, MODE=0: ADD)
    v3_36_0_0_param_adder_tree #(
        .WIDTH      (8),
        .NUM_STAGES (4),
        .ENABLE_PIPE(1'b1),
        .ARITH_MODE (0)
    ) dut_piped (
        .clk        (clk),
        .rst_n      (rst_n),
        .operand_in (piped_operands),
        .result_out (piped_result)
    );

    // Instance 2: Combinational XOR tree (ENABLE_PIPE=0, MODE=2: XOR)
    v3_36_0_0_param_adder_tree #(
        .WIDTH      (8),
        .NUM_STAGES (4),
        .ENABLE_PIPE(1'b0),
        .ARITH_MODE (2)
    ) dut_comb (
        .clk        (clk),
        .rst_n      (rst_n),
        .operand_in (comb_operands),
        .result_out (comb_result)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n = 0;
        for (int k = 0; k < 4; k++) begin
            piped_operands[k] = 8'h0;
            comb_operands[k]  = 8'h0;
        end

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Combinational generate unroll verification (dut_comb)
        // comb_operands = {8'h11, 8'h22, 8'h33, 8'h44}
        // XOR mode: 11 ^ 22 ^ 33 ^ 44 = 44
        // --------------------------------------------------------------------
        comb_operands[0] = 8'h11;
        comb_operands[1] = 8'h22;
        comb_operands[2] = 8'h33;
        comb_operands[3] = 8'h44;

        #1;
        if (comb_result === 12'h044) begin
            $display("[PASS T=%0t] Combinational generate (if-else + case) verified: result=%03h",
                     $time, comb_result);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Combinational generate mismatch! got %03h (exp 044)",
                     $time, comb_result);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: Pipelined generate stages propagation (dut_piped)
        // piped_operands = {10, 20, 30, 40}
        // Sum = 10 + 20 + 30 + 40 = 100 (12'h064)
        // Latency: 3 registered stages (i = 0, 1, 2)
        // --------------------------------------------------------------------
        @(negedge clk);
        piped_operands[0] = 8'd10;
        piped_operands[1] = 8'd20;
        piped_operands[2] = 8'd30;
        piped_operands[3] = 8'd40;

        // Cycle 1: stage_data[1] = 10 + 20 = 30
        @(posedge clk);
        // Cycle 2: stage_data[2] = 30 + 30 = 60
        @(posedge clk);
        // Cycle 3: stage_data[3] = 60 + 40 = 100
        @(posedge clk);
        #1;

        if (piped_result === 12'd100) begin
            $display("[PASS T=%0t] Pipelined loop generate stages verified: result=%0d",
                     $time, piped_result);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Pipelined generate stages mismatch! got %0d (exp 100)",
                     $time, piped_result);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Hierarchical reference access to generate scope signals
        // Inspect dut_piped.gen_stages[0].partial_accum
        // At T = current, stage 0 partial_accum is 10 + 20 = 30
        // --------------------------------------------------------------------
        if (dut_piped.gen_stages[0].partial_accum === 12'd30) begin
            $display("[PASS T=%0t] Hierarchical access to genblk scope verified: gen_stages[0].partial_accum=%0d",
                     $time, dut_piped.gen_stages[0].partial_accum);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Hierarchical access to genblk scope failed! got %0d (exp 30)",
                     $time, dut_piped.gen_stages[0].partial_accum);
            error_count++;
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.36.0.0 REGRESSION SUITE: GENERATE CONSTRUCTS §27]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §27 loop generate, if-generate, case-generate, and hierarchical genblk scopes.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | rst_n=%b | comb_res=%03h piped_res=%0d",
                 $time, rst_n, comb_result, piped_result);
    end
endmodule