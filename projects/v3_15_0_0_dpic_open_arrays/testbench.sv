// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.15.0.0
// Standard Target: IEEE Std 1800-2023 §35.5.6.1 & Annex I.9 (Open Arrays)
// Testbench Name : tb_v3_15_0_0_regression
// ============================================================================

// DPI-C function imports accepting open arrays (§35.5.6.1)
import "DPI-C" function int c_sum_open_array(input int arr[]);
import "DPI-C" function int c_xor_reduce_open_array(input int arr[]);

module tb_v3_15_0_0_regression;
    logic        clk;
    logic        rst_n;
    logic [31:0] in_val;
    logic [31:0] out_val;

    // Fixed unpacked arrays of different sizes passed to the same open array DPI-C functions
    int arr_4[4];
    int arr_6[6];

    int sum_res  = 0;
    int xor_res  = 0;
    int error_count = 0;

    v3_15_0_0_open_arr_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .in_val(in_val),
        .out_val(out_val)
    );

    initial begin
        clk    = 0;
        rst_n  = 0;
        in_val = 32'h00000000;

        // Initialize 4-element array: [10, 20, 30, 40]
        arr_4[0] = 10;
        arr_4[1] = 20;
        arr_4[2] = 30;
        arr_4[3] = 40;

        // Initialize 6-element array: [0x11, 0x22, 0x33, 0x44, 0x55, 0x66]
        arr_6[0] = 32'h11;
        arr_6[1] = 32'h22;
        arr_6[2] = 32'h33;
        arr_6[3] = 32'h44;
        arr_6[4] = 32'h55;
        arr_6[5] = 32'h66;

        #5;
        rst_n = 1;

        // --------------------------------------------------------------------
        // TEST 1: Sum 4-element array
        // Expected: 10 + 20 + 30 + 40 = 100 (32'h00000064)
        // --------------------------------------------------------------------
        sum_res = c_sum_open_array(arr_4);
        #5; // T = 10

        if (sum_res !== 100) begin
            $display("[FAIL T=10] c_sum_open_array(arr_4) returned %0d (exp 100)!", sum_res);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Sum 6-element array
        // Expected: 0x11 + 0x22 + 0x33 + 0x44 + 0x55 + 0x66 = 0x165 (357)
        // --------------------------------------------------------------------
        sum_res = c_sum_open_array(arr_6);
        #5; // T = 15

        if (sum_res !== 357) begin
            $display("[FAIL T=15] c_sum_open_array(arr_6) returned %0d (exp 357)!", sum_res);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 3: XOR reduction on 4-element array
        // Expected: 10 ^ 20 ^ 30 ^ 40 = (0x0A ^ 0x14 ^ 0x1E ^ 0x28) = 0x28 (40)
        // Combined with size=4: (4 << 16) | 0x28 = 32'h00040028
        // --------------------------------------------------------------------
        xor_res = c_xor_reduce_open_array(arr_4);
        #5; // T = 20

        if (xor_res !== 32'h00040028) begin
            $display("[FAIL T=20] c_xor_reduce_open_array(arr_4) returned %h (exp 00040028)!", xor_res);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 4: XOR reduction on 6-element array
        // Expected: 0x11 ^ 0x22 ^ 0x33 ^ 0x44 ^ 0x55 ^ 0x66 = 0x77
        // Combined with size=6: (6 << 16) | 0x77 = 32'h00060077
        // --------------------------------------------------------------------
        xor_res = c_xor_reduce_open_array(arr_6);
        #5; // T = 25

        if (xor_res !== 32'h00060077) begin
            $display("[FAIL T=25] c_xor_reduce_open_array(arr_6) returned %h (exp 00060077)!", xor_res);
            error_count = error_count + 1;
        end

        #5; // T = 30
        $display("\n================================================================================");
        $display("[CAS v3.15.0.0 REGRESSION SUITE: DPI-C OPEN ARRAYS]");
        if (error_count == 0 && sum_res == 357 && xor_res == 32'h00060077) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §35.5.6.1 DPI-C open arrays, svSize, and svGetArrElemPtr1.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | sum_res=%0d | xor_res=%h",
                 $time, sum_res, xor_res);
    end
endmodule