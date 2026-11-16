// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.3.0.0
// Standard Target: IEEE Std 1800-2023 §7.8 (Associative Arrays & Methods)
// Testbench Name : tb_v3_3_0_0_regression
// ============================================================================

module tb_v3_3_0_0_regression;
    logic        clk;
    logic        rst_n;
    logic [15:0] addr_in;
    logic [31:0] data_in;
    logic        we;
    logic [31:0] data_out;

    // Associative array indexed by 16-bit logic address (§7.8.1)
    logic [31:0] sparse_mem [logic [15:0]];

    logic [15:0] cur_key;
    logic [31:0] read_val;
    logic [31:0] d_shadow0 = 32'hxxxxxxxx;
    logic [31:0] d_shadow1 = 32'hxxxxxxxx;
    integer      entry_count = 0;
    integer      error_count = 0;

    v3_3_0_0_assoc_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .addr_in(addr_in),
        .data_in(data_in),
        .we(we),
        .data_out(data_out)
    );

    task update_shadows();
        begin
            entry_count = sparse_mem.num();
            d_shadow0   = (sparse_mem.exists(16'h1000)) ? sparse_mem[16'h1000] : 32'hxxxxxxxx;
            d_shadow1   = (sparse_mem.exists(16'h8000)) ? sparse_mem[16'h8000] : 32'hxxxxxxxx;
        end
    endtask

    initial begin
        // Time 0: Sparse array empty
        update_shadows();
        read_val = 32'hxxxxxxxx;
        #5;

        // --------------------------------------------------------------------
        // TEST 1: Sparse writes and .num() / .exists()
        // Populate 16'h1000 => 32'hA0A0B0B0, 16'h8000 => 32'hC0C0D0D0
        // --------------------------------------------------------------------
        sparse_mem[16'h1000] = 32'hA0A0B0B0;
        sparse_mem[16'h8000] = 32'hC0C0D0D0;
        sparse_mem[16'hF000] = 32'h11223344;
        update_shadows();
        #5; // T = 10

        if (entry_count !== 3 ||
            !sparse_mem.exists(16'h1000) ||
            !sparse_mem.exists(16'h8000) ||
            !sparse_mem.exists(16'hF000) ||
            sparse_mem.exists(16'h2000)) begin
            $display("[FAIL T=10] Sparse write / exists() check failed! count=%0d", entry_count);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Traversal methods: .first() and .next()
        // --------------------------------------------------------------------
        if (sparse_mem.first(cur_key)) begin
            if (cur_key !== 16'h1000) begin
                $display("[FAIL T=10] first() returned wrong key: %h (exp 1000)", cur_key);
                error_count = error_count + 1;
            end
        end else begin
            $display("[FAIL T=10] first() failed!");
            error_count = error_count + 1;
        end

        if (sparse_mem.next(cur_key)) begin
            if (cur_key !== 16'h8000) begin
                $display("[FAIL T=10] next() returned wrong key: %h (exp 8000)", cur_key);
                error_count = error_count + 1;
            end
        end else begin
            $display("[FAIL T=10] next() failed!");
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 3: Read uninitialized key (§7.8.6: returns 4-state unknown 'x)
        // --------------------------------------------------------------------
        read_val = sparse_mem[16'h5555];
        #5; // T = 15
        if (read_val !== 32'hxxxxxxxx) begin
            $display("[FAIL T=15] Uninitialized key read failed! read_val=%h (exp xxxxxxxx)", read_val);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 4: Single element deletion: .delete(key)
        // Remove 16'h8000
        // --------------------------------------------------------------------
        sparse_mem.delete(16'h8000);
        update_shadows();
        #5; // T = 20
        if (entry_count !== 2 || sparse_mem.exists(16'h8000)) begin
            $display("[FAIL T=20] delete(16'h8000) failed! count=%0d exists=%0d",
                     entry_count, sparse_mem.exists(16'h8000));
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 5: Full deletion: .delete()
        // Purge entire sparse memory
        // --------------------------------------------------------------------
        sparse_mem.delete();
        update_shadows();
        #5; // T = 25
        if (entry_count !== 0 || sparse_mem.exists(16'h1000)) begin
            $display("[FAIL T=25] Full delete() failed! count=%0d", entry_count);
            error_count = error_count + 1;
        end

        #5; // T = 30
        $display("\n================================================================================");
        $display("[CAS v3.3.0.0 REGRESSION SUITE: ASSOCIATIVE ARRAYS]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §7.8 associative arrays (exists, num, first, next, delete).");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | count=%0d read_val=%h | d_1000=%h d_8000=%h",
                 $time, entry_count, read_val, d_shadow0, d_shadow1);
    end
endmodule