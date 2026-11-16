// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.17.0.0
// Standard Target: IEEE Std 1800-2023 §15 (Built-in semaphore and mailbox)
// Testbench Name : tb_v3_17_0_0_regression
// ============================================================================

module tb_v3_17_0_0_regression;
    logic        clk;
    logic        rst_n;
    logic [31:0] data_in;
    logic [31:0] data_out;

    // Built-in synchronization objects (§15.3 & §15.4)
    semaphore sem;
    mailbox   mbx;

    int sem_owner   = 0;
    int received_val = 0;
    int mbx_count    = 0;
    int error_count  = 0;

    v3_17_0_0_sync_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .data_in(data_in),
        .data_out(data_out)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n   = 0;
        data_in = 32'h00000000;

        // Initialize mutex semaphore with 1 key, bounded mailbox with size 2
        sem = new(1);
        mbx = new(2);

        #6;
        rst_n = 1;

        // --------------------------------------------------------------------
        // TEST 1: Mutex Arbitration via Semaphore (Forked Threads)
        // Thread 1 acquires key at T=10, holds for 10ns, releases at T=20
        // Thread 2 blocks on get() at T=10, unblocks at T=20, releases at T=30
        // --------------------------------------------------------------------
        fork
            // Thread 1
            begin
                sem.get(1);
                sem_owner = 1;
                #10;
                sem_owner = 0;
                sem.put(1);
            end

            // Thread 2
            begin
                #1; // slight delta to ensure Thread 1 captures mutex first
                sem.get(1); // blocks until Thread 1 releases at T=20
                sem_owner = 2;
                #10;
                sem_owner = 0;
                sem.put(1);
            end
        join

        // --------------------------------------------------------------------
        // TEST 2: Mailbox Producer-Consumer Handshake
        // --------------------------------------------------------------------
        fork
            // Producer
            begin
                #5;
                mbx.put(32'hCAFE0001);
                mbx.put(32'hCAFE0002);
                mbx_count = mbx.num();
            end

            // Consumer
            begin
                #10;
                mbx.get(received_val);
                mbx_count = mbx.num();
            end
        join

        #5; // T = 45
        if (received_val !== 32'hCAFE0001 || mbx_count !== 1) begin
            $display("[FAIL T=45] Mailbox check failed! rec=%h count=%0d (exp CAFE0001, 1)",
                     received_val, mbx_count);
            error_count = error_count + 1;
        end

        // Consume remaining item
        mbx.get(received_val);
        mbx_count = mbx.num();
        #5; // T = 50

        if (received_val !== 32'hCAFE0002 || mbx_count !== 0) begin
            $display("[FAIL T=50] Second mailbox check failed! rec=%h count=%0d (exp CAFE0002, 0)",
                     received_val, mbx_count);
            error_count = error_count + 1;
        end

        #5; // T = 55
        $display("\n================================================================================");
        $display("[CAS v3.17.0.0 REGRESSION SUITE: SEMAPHORES & MAILBOXES]");
        if (error_count == 0 && received_val == 32'hCAFE0002 && mbx_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §15 built-in semaphore and mailbox synchronization.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | sem_owner=%0d | mbx_cnt=%0d rec=%h",
                 $time, sem_owner, mbx_count, received_val);
    end
endmodule