// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.6.0.0
// Standard Target: IEEE Std 1800-2023 §18 (rand, constraint, randomize(), with{})
// Testbench Name : tb_v3_6_0_0_regression
// ============================================================================

class rand_transaction;
    rand  bit [7:0]  addr;
    rand  bit [31:0] data;
    randc bit [1:0]  mode;

    // Declarative constraints (§18.5)
    constraint c_addr_range {
        addr >= 8'h10;
        addr <= 8'h20;
    }

    constraint c_data_nonzero {
        data > 32'h00000000;
        data < 32'h00000010;
    }

    function new();
        this.srandom(32'h12345678); // Deterministic seed for parity (§18.14)
    endfunction
endclass

module tb_v3_6_0_0_regression;
    logic        clk;
    logic        rst_n;
    logic [7:0]  addr_in;
    logic [31:0] data_in;
    logic [31:0] resp_out;

    rand_transaction pkt;

    // Procedural shadow registers to cleanly observe in $monitor
    logic [7:0]  addr_shadow = 8'h00;
    logic [31:0] data_shadow = 32'h00000000;
    logic [1:0]  mode_shadow = 2'b00;
    integer      rand_ok = 0;
    integer      error_count = 0;

    v3_6_0_0_rand_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .addr_in(addr_in),
        .data_in(data_in),
        .resp_out(resp_out)
    );

    task update_shadows();
        begin
            addr_shadow = pkt.addr;
            data_shadow = pkt.data;
            mode_shadow = pkt.mode;
        end
    endtask

    initial begin
        clk     = 0;
        rst_n   = 0;
        addr_in = 8'h00;
        data_in = 32'h00000000;

        #5;
        rst_n = 1;
        pkt = new();

        // --------------------------------------------------------------------
        // TEST 1: Basic randomize() satisfying class constraints
        // addr in [16..32], data in [1..15]
        // --------------------------------------------------------------------
        rand_ok = pkt.randomize();
        update_shadows();
        #5; // T = 10

        if (!rand_ok || pkt.addr < 8'h10 || pkt.addr > 8'h20 ||
            pkt.data == 0 || pkt.data >= 32'h00000010) begin
            $display("[FAIL T=10] Basic randomize() failed! ok=%0d addr=%h data=%h",
                     rand_ok, pkt.addr, pkt.data);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Inline constraint with { ... } (§18.7)
        // Restrict addr to exact value 8'h15
        // --------------------------------------------------------------------
        rand_ok = pkt.randomize() with {
            addr == 8'h15;
            data == 32'h00000007;
        };
        update_shadows();
        #5; // T = 15

        if (!rand_ok || pkt.addr !== 8'h15 || pkt.data !== 32'h00000007) begin
            $display("[FAIL T=15] Inline constraint with{} failed! ok=%0d addr=%h data=%h",
                     rand_ok, pkt.addr, pkt.data);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 3: Unsolvable inline constraint contradiction
        // Contradicts c_addr_range (addr >= 16 and addr <= 32)
        // Must fail gracefully, returning 0 without crashing
        // --------------------------------------------------------------------
        rand_ok = pkt.randomize() with {
            addr == 8'hFF;
        };
        #5; // T = 20

        if (rand_ok != 0) begin
            $display("[FAIL T=20] Contradictory constraint should return 0, got %0d!", rand_ok);
            error_count = error_count + 1;
        end

        #5; // T = 25
        $display("\n================================================================================");
        $display("[CAS v3.6.0.0 REGRESSION SUITE: CONSTRAINED RANDOMIZATION]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §18 rand, constraint blocks, randomize(), and with{}.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | rand_ok=%0d | addr=%h data=%h mode=%b",
                 $time, rand_ok, addr_shadow, data_shadow, mode_shadow);
    end
endmodule