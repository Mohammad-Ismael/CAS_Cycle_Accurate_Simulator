// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.25.0.0
// Standard Target: IEEE Std 1800-2023 §7.12 (Array Reduction & Locator Methods)
// Testbench Name : tb_v3_25_0_0_regression
// ============================================================================

module tb_v3_25_0_0_regression;
    logic        clk;
    logic        rst_n;
    logic        start;
    logic [7:0]  byte_in;
    logic [15:0] checksum_out;
    logic [7:0]  max_byte_out;
    logic        done;

    // Dynamic arrays and queues for verification algorithms
    int unsigned dyn_payload[];
    int unsigned res_sum;
    int unsigned res_xor;
    int unsigned min_val[$];
    int unsigned max_val[$];
    int unsigned filtered[$];

    int pass_count  = 0;
    int error_count = 0;

    v3_25_0_0_packet_eval dut (
        .clk(clk),
        .rst_n(rst_n),
        .start(start),
        .byte_in(byte_in),
        .checksum_out(checksum_out),
        .max_byte_out(max_byte_out),
        .done(done)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n   = 0;
        start   = 0;
        byte_in = 8'h00;

        // Allocate and populate dynamic array: '{10, 30, 20, 40}
        dyn_payload = new[4];
        dyn_payload[0] = 32'd10;
        dyn_payload[1] = 32'd30;
        dyn_payload[2] = 32'd20;
        dyn_payload[3] = 32'd40;

        // --------------------------------------------------------------------
        // TEST 1: Array Reduction Methods (§7.12.3)
        // sum = 10 + 30 + 20 + 40 = 100
        // xor = 10 ^ 30 ^ 20 ^ 40 = 40
        // --------------------------------------------------------------------
        res_sum = dyn_payload.sum();
        res_xor = dyn_payload.xor();

        if (res_sum !== 32'd100) begin
            $display("[FAIL T=%0t] dyn_payload.sum() mismatch! got %0d (exp 100)", $time, res_sum);
            error_count++;
        end else begin
            pass_count++;
        end

        if (res_xor !== 32'd40) begin
            $display("[FAIL T=%0t] dyn_payload.xor() mismatch! got %0d (exp 40)", $time, res_xor);
            error_count++;
        end else begin
            pass_count++;
        end

        // --------------------------------------------------------------------
        // TEST 2: Array Locator Methods (§7.12.1)
        // min() returns queue containing {10}
        // max() returns queue containing {40}
        // find() with (item > 25) returns {30, 40}
        // --------------------------------------------------------------------
        min_val = dyn_payload.min();
        max_val = dyn_payload.max();
        filtered = dyn_payload.find() with (item > 25);

        if (min_val.size() !== 1 || min_val[0] !== 32'd10) begin
            $display("[FAIL T=%0t] dyn_payload.min() mismatch! got %p (exp {10})", $time, min_val);
            error_count++;
        end else begin
            pass_count++;
        end

        if (max_val.size() !== 1 || max_val[0] !== 32'd40) begin
            $display("[FAIL T=%0t] dyn_payload.max() mismatch! got %p (exp {40})", $time, max_val);
            error_count++;
        end else begin
            pass_count++;
        end

        if (filtered.size() !== 2 || filtered[0] !== 32'd30 || filtered[1] !== 32'd40) begin
            $display("[FAIL T=%0t] dyn_payload.find() mismatch! got %p (exp {30, 40})", $time, filtered);
            error_count++;
        end else begin
            pass_count++;
        end

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // TEST 3: Feed payload stream to DUT and check checksum vs reduction
        // --------------------------------------------------------------------
        for (int i = 0; i < dyn_payload.size(); i++) begin
            @(negedge clk);
            start   = 1'b1;
            byte_in = dyn_payload[i][7:0];
        end

        @(negedge clk);
        start   = 1'b0;
        byte_in = 8'h00;

        @(posedge clk);
        #1;
        if (checksum_out !== 16'd100 || max_byte_out !== 8'd40) begin
            $display("[FAIL T=%0t] Hardware accelerator mismatch! checksum=%0d (exp 100), max=%0d (exp 40)",
                     $time, checksum_out, max_byte_out);
            error_count++;
        end else begin
            pass_count++;
        end

        #5;
        $display("\n================================================================================");
        $display("[CAS v3.25.0.0 REGRESSION SUITE: ARRAY REDUCTION & LOCATOR METHODS]");
        if (error_count == 0 && pass_count == 6) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §7.12 array reduction (sum, xor) and locator (min, max, find) methods.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 6, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | rst_n=%b start=%b byte=%0d | chk=%0d max=%0d done=%b",
                 $time, rst_n, start, byte_in, checksum_out, max_byte_out, done);
    end
endmodule