// IEEE 1800-2023 §7.10 Queues: Methods, Dollar Indexing, Slices & Ref Persistence
module queues_dollar_slices_dut (
    input  logic        clk,
    input  logic        rst_n,
    output logic [31:0] err_count
);

    int q[$];
    int q_sub[$];
    int r;
    int v_front, v_back;

    task automatic check_ref(ref int e, output int obs);
        obs = e;
    endtask

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            err_count <= 0;
            q.delete();
        end else begin
            int local_err;
            int ref_observed;
            local_err = 0;

            // 1. IEEE 1800 §7.10.2.2 q.insert & methods
            q = { 1, 2, 3 };
            q.insert(1, 99); // { 1, 99, 2, 3 }
            if (q.size != 4 || q[0] != 1 || q[1] != 99 || q[2] != 2 || q[3] != 3) begin
                $display("[ERROR] q.insert failed: size=%0d, q[1]=%0d", q.size, q[1]);
                local_err++;
            end

            // 2. Parenthesis-free pop_front & pop_back
            v_front = q.pop_front; // returns 1, q becomes { 99, 2, 3 }
            if (v_front != 1 || q.size != 3 || q[0] != 99) begin
                $display("[ERROR] pop_front failed: v_front=%0d, q.size=%0d, q[0]=%0d", v_front, q.size, q[0]);
                local_err++;
            end

            v_back = q.pop_back; // returns 3, q becomes { 99, 2 }
            if (v_back != 3 || q.size != 2 || q[1] != 2) begin
                $display("[ERROR] pop_back failed: v_back=%0d, q.size=%0d", v_back, q.size);
                local_err++;
            end

            // 3. Queue reset with empty concatenation {}
            q = {};
            if (q.size != 0) begin
                $display("[ERROR] empty queue reset failed: size=%0d", q.size);
                local_err++;
            end

            // 4. IEEE 1800 §7.10.1 $ end-of-queue indexing & splicing
            q = { 2, 3, 4 };
            r = q[$]; // r = 4
            if (r != 4) begin
                $display("[ERROR] q[$] indexing failed: expected 4, got %0d", r);
                local_err++;
            end

            q = q[0:$-1]; // { 2, 3 }
            if (q.size != 2 || q[0] != 2 || q[1] != 3) begin
                $display("[ERROR] q[0:$-1] failed: size=%0d, q[0]=%0d, q[1]=%0d", q.size, q[0], q[1]);
                local_err++;
            end

            q = q[1:$]; // { 3 }
            if (q.size != 1 || q[0] != 3) begin
                $display("[ERROR] q[1:$] failed: size=%0d, q[0]=%0d", q.size, q[0]);
                local_err++;
            end

            // 5. Concatenation splicing: q = { q[0:1], 10, q[2:$] }
            q = { 1, 2, 3, 4 };
            q = { q[0:1], 10, q[2:$] }; // { 1, 2, 10, 3, 4 }
            if (q.size != 5 || q[0] != 1 || q[1] != 2 || q[2] != 10 || q[3] != 3 || q[4] != 4) begin
                $display("[ERROR] concatenation splicing failed: size=%0d, q[2]=%0d", q.size, q[2]);
                local_err++;
            end

            // 6. Queue slice bounds & asymmetric empty slice
            q_sub = q[4:2]; // a > b yields empty queue {}
            if (q_sub.size != 0) begin
                $display("[ERROR] q[4:2] empty slice failed: size=%0d", q_sub.size);
                local_err++;
            end

            q_sub = q[-2:2]; // clamped to [0:2] -> { 1, 2, 10 }
            if (q_sub.size != 3 || q_sub[0] != 1 || q_sub[1] != 2 || q_sub[2] != 10) begin
                $display("[ERROR] q[-2:2] clamped slice failed: size=%0d", q_sub.size);
                local_err++;
            end

            q_sub = q[3:10]; // clamped to [3:4] -> { 3, 4 }
            if (q_sub.size != 2 || q_sub[0] != 3 || q_sub[1] != 4) begin
                $display("[ERROR] q[3:10] clamped slice failed: size=%0d", q_sub.size);
                local_err++;
            end

    // 7. Subroutine ref argument to queue element
`ifndef VERILATOR
            check_ref(q[2], ref_observed);
            if (ref_observed != 10) begin
                $display("[ERROR] check_ref failed: expected 10, got %0d", ref_observed);
                local_err++;
            end
`else
            ref_observed = q[2];
`endif

            err_count <= local_err;
        end
    end

endmodule
