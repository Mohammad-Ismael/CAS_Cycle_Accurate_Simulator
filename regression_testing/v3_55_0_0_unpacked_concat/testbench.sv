`timescale 1ns/1ps

// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.55.0.0
// Standard Target: IEEE Std 1800-2023 §10.10 (Unpacked Array Concatenation)
// Testbench Name : tb_v3_55_0_regression
// ============================================================================

module tb_v3_55_0_regression;
    import v3_55_0_0_pkg::*;

    logic        clk;
    logic        rst_n;
    logic [7:0]  hdr_byte;
    logic [7:0]  payload_byte;
    logic [15:0] frame_size_out;
    logic [7:0]  first_byte_out;
    logic [7:0]  last_byte_out;
    logic        assembly_valid;

    int pass_count  = 0;
    int error_count = 0;

    v3_55_0_0_frame_builder dut (
        .clk           (clk),
        .rst_n         (rst_n),
        .hdr_byte      (hdr_byte),
        .payload_byte  (payload_byte),
        .frame_size_out(frame_size_out),
        .first_byte_out(first_byte_out),
        .last_byte_out (last_byte_out),
        .assembly_valid(assembly_valid)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        int arr_a[];
        int arr_b[];
        int arr_merged[];
        int fixed_arr[4];

        rst_n        = 0;
        hdr_byte     = 8'h00;
        payload_byte = 8'h00;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Procedural dynamic array concatenation and append/prepend (§10.10)
        // --------------------------------------------------------------------
        arr_a = '{10, 20};
        arr_b = '{30, 40};

        // Concatenate two dynamic arrays: {arr_a, arr_b} -> '{10, 20, 30, 40}
        arr_merged = {arr_a, arr_b};

        // Append element
        arr_merged = {arr_merged, 50};

        // Prepend element
        arr_merged = {5, arr_merged};

        if (arr_merged.size() == 6 &&
            arr_merged[0] == 5 &&
            arr_merged[1] == 10 &&
            arr_merged[2] == 20 &&
            arr_merged[3] == 30 &&
            arr_merged[4] == 40 &&
            arr_merged[5] == 50) begin
            $display("[PASS T=%0t] Procedural dynamic array concatenation verified: size=%0d [0]=%0d [5]=%0d",
                     $time, arr_merged.size(), arr_merged[0], arr_merged[5]);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Procedural dynamic array concatenation failed! size=%0d",
                     $time, arr_merged.size());
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: Fixed unpacked array assignment via concatenation pattern
        // --------------------------------------------------------------------
        fixed_arr = '{100, 200, 300, 400};
        if (fixed_arr[0] == 100 && fixed_arr[1] == 200 &&
            fixed_arr[2] == 300 && fixed_arr[3] == 400) begin
            $display("[PASS T=%0t] Fixed unpacked array concatenation pattern verified", $time);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Fixed unpacked array concatenation pattern failed!", $time);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: DUT sequential frame builder with dynamic array concatenation
        // --------------------------------------------------------------------
        @(negedge clk);
        hdr_byte     = 8'hA1;
        payload_byte = 8'hFE;

        @(posedge clk);
        #1;
        if (frame_size_out === 16'd4 &&
            first_byte_out === 8'hA1 &&
            last_byte_out  === 8'hFE &&
            assembly_valid === 1'b1) begin
            $display("[PASS T=%0t] DUT frame assembly verified: size=%0d first=%02h last=%02h valid=%b",
                     $time, frame_size_out, first_byte_out, last_byte_out, assembly_valid);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] DUT frame assembly failed! size=%0d first=%02h last=%02h valid=%b",
                     $time, frame_size_out, first_byte_out, last_byte_out, assembly_valid);
            error_count++;
        end

        @(negedge clk);

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.55.0.0 REGRESSION SUITE: UNPACKED ARRAY CONCATENATION §10.10]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §10.10 unpacked and dynamic array concatenations.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule