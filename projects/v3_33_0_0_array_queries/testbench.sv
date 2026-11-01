// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.33.0.0
// Standard Target: IEEE Std 1800-2023 §7.11 (Array Querying Functions)
// Testbench Name : tb_v3_33_0_0_regression
// ============================================================================

module tb_v3_33_0_0_regression;

    logic clk;
    logic rst_n;
    logic trig;
    int   cells;
    logic valid;

    int dyn[];
    int q[$];

    v3_33_0_0_array_queries dut (
        .clk(clk),
        .rst_n(rst_n),
        .trig(trig),
        .cells(cells),
        .valid_out(valid)
    );

    // Clock generator: 10ns period (5ns toggle)
    always #5 clk = ~clk;

    initial begin
        clk   = 0;
        rst_n = 0;
        trig  = 0;

        $monitor("Time=%0t | rst_n=%b trig=%b | cells=%0d valid=%b",
                 $time, rst_n, trig, cells, valid);

        #12;
        rst_n = 1;

        #1; // T=13
        if (cells == 32 && $size(dut.buffer_matrix, 1) == 4 && $size(dut.buffer_matrix, 2) == 8 && $size(dut.buffer_matrix, 3) == 16) begin
            $display("[PASS T=%0t] DUT array size check: cells=%0d (d1=%0d, d2=%0d, d3=%0d)",
                     $time, cells, $size(dut.buffer_matrix, 1), $size(dut.buffer_matrix, 2), $size(dut.buffer_matrix, 3));
        end

        #7; // T=20
        trig = 1;

        #5; // T=25: posedge clk triggers dut, valid becomes 1

        #1; // T=26
        if (valid === 1'b1) begin
            $display("[PASS T=%0t] DUT dimensions and increment verified: valid_out=%b", $time, valid);
        end

        // Test dynamic array and queue queries (§7.11.6)
        dyn = new[6];
        q.push_back(10);
        q.push_back(20);
        q.push_back(30);

        if ($size(dyn) == 6 && $high(dyn) == 5 && $size(q) == 3 && $high(q) == 2) begin
            $display("[PASS T=%0t] Dynamic array & queue queries: dyn_size=%0d dyn_high=%0d q_size=%0d q_high=%0d",
                     $time, $size(dyn), $high(dyn), $size(q), $high(q));
        end

        trig = 0;

        #10; // T=36
        $finish;
    end

endmodule
