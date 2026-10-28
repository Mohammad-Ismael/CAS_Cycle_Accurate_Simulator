module tb_random_stress;
    logic clk;
    logic [7:0] data_in;
    logic [7:0] pipe_out;
    logic [7:0] fast_out;
    logic [7:0] slow_out;
    integer err_cnt = 0;

    // 1. Clock generator using 'forever #delay'
    initial begin
        clk = 0;
        forever #5 clk = ~clk; // Period = 10ns, posedges at 5, 15, 25, 35...
    end

    // 2. Synchronous pipeline with edge suspension
    always @(posedge clk) begin
        // Intra-assignment delay inside an edge-triggered block:
        // Evaluates (data_in + 8'h01) at posedge, but commits 3ns after the edge!
        pipe_out <= #3 (data_in + 8'h01);
    end

    // 3. Driver & Checker Process
    initial begin
        data_in  = 8'h10;
        pipe_out = 8'h00;
        fast_out = 8'h00;
        slow_out = 8'h00;

        // Test A: Out-of-order delayed non-blocking transport retirement
        // Schedule #12 first, then #4 second.
        // slow_out should update at T=12, fast_out at T=4.
        slow_out <= #12 (data_in + 8'h20); // Samples 8'h10 + 8'h20 = 8'h30
        fast_out <= #4  (data_in + 8'h05); // Samples 8'h10 + 8'h05 = 8'h15

        #2; // T=2
        data_in = 8'hA0; // Change source; sampled values (8'h30, 8'h15) must not be corrupted

        #3; // T=5: First posedge clk
        // At T=5:
        // - fast_out already committed at T=4 (should be 8'h15)
        // - slow_out still pending until T=12 (should be 8'h00)
        // - pipe_out sampled (8'hA0 + 1 = 8'hA1) and scheduled for T=5+3 = T=8
        if (fast_out !== 8'h15 || slow_out !== 8'h00) begin
            $display("[FAIL T=5] Transport delay failure! fast_out=%h (exp 15), slow_out=%h (exp 00)", fast_out, slow_out);
            err_cnt = err_cnt + 1;
        end

        #2; // T=7: pipe_out should still be 8'h00 (scheduled for T=8)
        if (pipe_out !== 8'h00) begin
            $display("[FAIL T=7] pipe_out committed too early! pipe_out=%h (exp 00)", pipe_out);
            err_cnt = err_cnt + 1;
        end

        #2; // T=9: pipe_out should now be committed at T=8 (8'hA1)
        if (pipe_out !== 8'hA1) begin
            $display("[FAIL T=9] pipe_out failed! pipe_out=%h (exp A1)", pipe_out);
            err_cnt = err_cnt + 1;
        end

        #4; // T=13: slow_out should now be committed at T=12 (8'h30)
        if (slow_out !== 8'h30) begin
            $display("[FAIL T=13] slow_out failed! slow_out=%h (exp 30)", slow_out);
            err_cnt = err_cnt + 1;
        end

        // Test B: Wait for next clock edge and check suspension
        @(posedge clk); // Resumes at next posedge clk (T=15)
        if ($time !== 15) begin
            $display("[FAIL @(posedge clk)] Time mismatch! time=%0t (exp 15)", $time);
            err_cnt = err_cnt + 1;
        end

        #10; // T=25
        $display("\n================================================================================");
        $display("[RANDOM INTERACTION STRESS TEST]");
        if (err_cnt == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s))", err_cnt);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | clk=%b data_in=%h | fast=%h slow=%h pipe=%h",
                 $time, clk, data_in, fast_out, slow_out, pipe_out);
    end
endmodule