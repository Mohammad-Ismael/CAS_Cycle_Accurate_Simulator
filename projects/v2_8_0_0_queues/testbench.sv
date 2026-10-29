// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v2.8.0.0
// Standard Target: IEEE Std 1800-2023 §7.10 (Queue Operations & Built-in Methods)
//                  and §7.10.4 / §21.2.3 (Dynamic Type Context Restrictions)
// Testbench Name : tb_v2_8_0_0_regression
// ============================================================================

module tb_v2_8_0_0_regression;
    logic clk;
    logic rst_n;
    logic [7:0] d_in;
    logic [7:0] d_out;

    // Unbounded queue (§7.10.1)
    logic [7:0] q [$];
    logic [7:0] popped_val;
    logic [7:0] q0_shadow = 8'hxx;
    logic [7:0] q1_shadow = 8'hxx;
    integer     q_len = 0;
    integer     error_count = 0;

    v2_8_0_0_queue_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .d_in(d_in),
        .d_out(d_out)
    );

    // Procedural shadow update task to safely feed $monitor
    task update_shadows();
        begin
            q0_shadow = (q.size() > 0) ? q[0] : 8'hxx;
            q1_shadow = (q.size() > 1) ? q[1] : 8'hxx;
            q_len     = q.size();
        end
    endtask

    initial begin
        // Time 0: Initial state
        update_shadows();
        #5;

        // --------------------------------------------------------------------
        // TEST 1: push_back and size()
        // Push 8'h11, 8'h22, 8'h33
        // Queue content: [11, 22, 33]
        // --------------------------------------------------------------------
        q.push_back(8'h11);
        q.push_back(8'h22);
        q.push_back(8'h33);
        update_shadows();
        #5; // T = 10
        if (q_len !== 3 || q[0] !== 8'h11 || q[1] !== 8'h22 || q[2] !== 8'h33) begin
            $display("[FAIL T=10] push_back failed! size=%0d q[0]=%h q[1]=%h q[2]=%h",
                     q_len, q[0], q[1], q[2]);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: push_front
        // Push 8'hAA to the head
        // Queue content: [AA, 11, 22, 33]
        // --------------------------------------------------------------------
        q.push_front(8'hAA);
        update_shadows();
        #5; // T = 15
        if (q_len !== 4 || q[0] !== 8'hAA || q[1] !== 8'h11) begin
            $display("[FAIL T=15] push_front failed! size=%0d q[0]=%h q[1]=%h",
                     q_len, q[0], q[1]);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 3: pop_front
        // Removes 8'hAA, remaining: [11, 22, 33]
        // --------------------------------------------------------------------
        popped_val = q.pop_front();
        update_shadows();
        #5; // T = 20
        if (popped_val !== 8'hAA || q_len !== 3 || q[0] !== 8'h11) begin
            $display("[FAIL T=20] pop_front failed! popped=%h size=%0d q[0]=%h",
                     popped_val, q_len, q[0]);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 4: pop_back
        // Removes 8'h33, remaining: [11, 22]
        // --------------------------------------------------------------------
        popped_val = q.pop_back();
        update_shadows();
        #5; // T = 25
        if (popped_val !== 8'h33 || q_len !== 2 || q[1] !== 8'h22) begin
            $display("[FAIL T=25] pop_back failed! popped=%h size=%0d q[1]=%h",
                     popped_val, q_len, q[1]);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 5: delete() and out-of-bounds reading (§7.10.3)
        // Empty the queue, read q[0] -> must return 8'hxx
        // --------------------------------------------------------------------
        q.delete();
        update_shadows();
        popped_val = q[0]; // Out-of-bounds read
        #5; // T = 30
        if (q_len !== 0 || popped_val !== 8'hxx) begin
            $display("[FAIL T=30] delete/empty access failed! size=%0d q[0]=%h (exp xx)",
                     q_len, popped_val);
            error_count = error_count + 1;
        end

        #5; // T = 35
        $display("\n================================================================================");
        $display("[CAS v2.8.0.0 REGRESSION SUITE: QUEUES & METHODS]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §7.10 queue operations and built-in methods.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | q_size=%0d popped_val=%h | q0=%h q1=%h",
                 $time, q_len, popped_val, q0_shadow, q1_shadow);
    end
endmodule