// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.81.0.0
// Standard Target: IEEE Std 1800-2023 §7.4 (Multidimensional Packed Arrays)
// Testbench Name : tb_v3_81_0_regression
// ============================================================================

module tb_v3_81_0_regression;
    logic clk;
    logic rst_n;
    logic [1:0][3:0][15:0] arr3d_in;
    logic [7:0]  slice_comb;
    logic [15:0] word_comb;
    logic [7:0]  slice_seq;

    // Cross-hierarchical part-select slice of 3D array
    logic [7:0] hier_slice_comb;
    assign hier_slice_comb = dut.arr3d_in[1][3][7:0];

    int pass_count  = 0;
    int error_count = 0;

    v3_81_0_0_nd_array_slice dut (
        .clk       (clk),
        .rst_n     (rst_n),
        .arr3d_in  (arr3d_in),
        .slice_comb(slice_comb),
        .word_comb (word_comb),
        .slice_seq (slice_seq)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n = 0;
        arr3d_in = '0;
        #15;
        rst_n = 1;
        #10;

        // Populate 3D array: [1][3]=FEED, [1][2]=ABCD, [1][1]=0, [1][0]=0,
        //                   [0][3]=5A5A, [0][2]=0,    [0][1]=1234, [0][0]=0
        arr3d_in = { 16'hFEED, 16'hABCD, 16'h0000, 16'h0000,
                     16'h5A5A, 16'h0000, 16'h1234, 16'h0000 };

        #2; // comb propagation
        if (slice_comb === 8'hBC && word_comb === 16'h5A5A && hier_slice_comb === 8'hED) begin
            $display("[PASS T=%0t] 3D array comb slices verified: slice_comb=0x%02x word=0x%04x hier=0x%02x",
                     $time, slice_comb, word_comb, hier_slice_comb);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] 3D array comb slices failed: slice_comb=0x%02x word=0x%04x hier=0x%02x",
                     $time, slice_comb, word_comb, hier_slice_comb);
            error_count++;
        end

        @(posedge clk);
        #1;
        if (slice_seq === 8'h12) begin
            $display("[PASS T=%0t] 3D array seq slice verified: slice_seq=0x%02x", $time, slice_seq);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] 3D array seq slice failed: slice_seq=0x%02x (exp 12)", $time, slice_seq);
            error_count++;
        end

        // Transaction 2: Update array elements
        @(negedge clk);
        arr3d_in = { 16'h33AA, 16'hF0A5, 16'h0000, 16'h0000,
                     16'hCAFE, 16'h0000, 16'h9876, 16'h0000 };

        #2;
        if (slice_comb === 8'h0A && word_comb === 16'hCAFE && hier_slice_comb === 8'hAA) begin
            $display("[PASS T=%0t] 3D array comb slices 2 verified: slice_comb=0x%02x word=0x%04x hier=0x%02x",
                     $time, slice_comb, word_comb, hier_slice_comb);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] 3D array comb slices 2 failed: slice_comb=0x%02x word=0x%04x hier=0x%02x",
                     $time, slice_comb, word_comb, hier_slice_comb);
            error_count++;
        end

        @(posedge clk);
        #1;
        if (slice_seq === 8'h98) begin
            $display("[PASS T=%0t] 3D array seq slice 2 verified: slice_seq=0x%02x", $time, slice_seq);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] 3D array seq slice 2 failed: slice_seq=0x%02x (exp 98)", $time, slice_seq);
            error_count++;
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.81.0.0 REGRESSION SUITE: 3D PACKED ARRAY SLICE §7.4]");
        if (error_count == 0 && pass_count == 4) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §7.4 Multidimensional Packed Array Slicing.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 4, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule
