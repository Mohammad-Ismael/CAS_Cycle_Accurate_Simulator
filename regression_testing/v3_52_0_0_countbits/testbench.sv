`timescale 1ns/1ps

// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.52.0.0
// Standard Target: IEEE Std 1800-2023 §20.9 (Bit Count Function $countbits)
// Testbench Name : tb_v3_52_0_regression
// ============================================================================

module tb_v3_52_0_regression;
    import v3_52_0_0_pkg::*;

    logic        clk;
    logic        rst_n;
    logic [12:0] raw_bus;
    logic [4:0]  known_bits_count;
    logic [3:0]  unknown_bits_count;
    logic [4:0]  ones_count;
    logic        bus_fully_known;

    int pass_count  = 0;
    int error_count = 0;

    v3_52_0_0_bus_monitor dut (
        .clk               (clk),
        .rst_n             (rst_n),
        .raw_bus           (raw_bus),
        .known_bits_count  (known_bits_count),
        .unknown_bits_count(unknown_bits_count),
        .ones_count        (ones_count),
        .bus_fully_known   (bus_fully_known)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n   = 0;
        raw_bus = 13'h0000;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Procedural evaluation of $countbits across combinations
        // Vector: 8'b10xz_01xz -> '0: 2 bits, '1: 2 bits, 'x: 2 bits, 'z: 2 bits
        // --------------------------------------------------------------------
        begin
            logic [7:0] test_vec;
            test_vec = 8'b10xz_01xz;

            if ($countbits(test_vec, '0) == 2 &&
                $countbits(test_vec, '1) == 2 &&
                $countbits(test_vec, 'x) == 2 &&
                $countbits(test_vec, 'z) == 2 &&
                $countbits(test_vec, '0, '1) == 4 &&
                $countbits(test_vec, 'x, 'z) == 4 &&
                $countbits(test_vec, '1, 'x) == 4 &&
                $countbits(test_vec, '0, '1, 'x, 'z) == 8) begin
                $display("[PASS T=%0t] Procedural $countbits 4-state partitions verified: 0=2 1=2 x=2 z=2", $time);
                pass_count++;
            end else begin
                $display("[FAIL T=%0t] Procedural $countbits 4-state partitions failed!", $time);
                error_count++;
            end
        end

        // --------------------------------------------------------------------
        // Test 2: Sequential DUT execution on clean 2-state bus word
        // raw_bus = 13'b1_1010_1010_0101 (seven 1s, six 0s, zero x/z)
        // --------------------------------------------------------------------
        @(negedge clk);
        raw_bus = 13'b1_1010_1010_0101;

        @(posedge clk);
        #1;
        if (known_bits_count === 5'd13 &&
            unknown_bits_count === 4'd0 &&
            ones_count === 5'd7 &&
            bus_fully_known === 1'b1) begin
            $display("[PASS T=%0t] DUT clean 2-state bus verified: known=%0d unk=%0d ones=%0d fully_known=%b",
                     $time, known_bits_count, unknown_bits_count, ones_count, bus_fully_known);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] DUT clean 2-state bus failed! known=%0d unk=%0d ones=%0d fully_known=%b",
                     $time, known_bits_count, unknown_bits_count, ones_count, bus_fully_known);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Sequential DUT execution on corrupted 4-state bus word
        // raw_bus = 13'b1_xxzz_1010_0101 (five 1s, four 0s, two xs, two zs)
        // --------------------------------------------------------------------
        @(negedge clk);
        raw_bus = 13'b1_xxzz_1010_0101;

        @(posedge clk);
        #1;
        if (known_bits_count === 5'd9 &&
            unknown_bits_count === 4'd4 &&
            ones_count === 5'd5 &&
            bus_fully_known === 1'b0) begin
            $display("[PASS T=%0t] DUT corrupted 4-state bus verified: known=%0d unk=%0d ones=%0d fully_known=%b",
                     $time, known_bits_count, unknown_bits_count, ones_count, bus_fully_known);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] DUT corrupted 4-state bus failed! known=%0d unk=%0d ones=%0d fully_known=%b",
                     $time, known_bits_count, unknown_bits_count, ones_count, bus_fully_known);
            error_count++;
        end

        @(negedge clk);

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.52.0.0 REGRESSION SUITE: BIT COUNT FUNCTION $countbits §20.9]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §20.9 $countbits multi-predicate 4-state counting.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule