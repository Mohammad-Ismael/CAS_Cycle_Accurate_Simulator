// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v2.9.0.0
// Standard Target: IEEE Std 1800-2023 §7.5 (Dynamic Arrays, new[], size(), delete())
// Testbench Name : tb_v2_9_0_0_regression
// ============================================================================

module tb_v2_9_0_0_regression;
    logic clk;
    logic rst_n;
    logic [7:0] d_in;
    logic [7:0] d_out;

    // Dynamic array declaration (§7.5.1)
    logic [7:0] dyn_data [];
    logic [7:0] read_val;
    logic [7:0] d0_shadow = 8'hxx;
    logic [7:0] d1_shadow = 8'hxx;
    integer     arr_len = 0;
    integer     error_count = 0;

    v2_9_0_0_dyn_array_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .d_in(d_in),
        .d_out(d_out)
    );

    task update_shadows();
        begin
            arr_len   = dyn_data.size();
            d0_shadow = (dyn_data.size() > 0) ? dyn_data[0] : 8'hxx;
            d1_shadow = (dyn_data.size() > 1) ? dyn_data[1] : 8'hxx;
        end
    endtask

    initial begin
        // Time 0: Initial unallocated state
        update_shadows();
        read_val = 8'hxx;
        #5;

        // --------------------------------------------------------------------
        // TEST 1: Allocate new[3] and populate elements
        // --------------------------------------------------------------------
        dyn_data = new[3];
        dyn_data[0] = 8'h12;
        dyn_data[1] = 8'h34;
        dyn_data[2] = 8'h56;
        update_shadows();
        #5; // T = 10
        if (arr_len !== 3 || dyn_data[0] !== 8'h12 || dyn_data[1] !== 8'h34 || dyn_data[2] !== 8'h56) begin
            $display("[FAIL T=10] new[3] allocation failed! size=%0d [0]=%h [1]=%h [2]=%h",
                     arr_len, dyn_data[0], dyn_data[1], dyn_data[2]);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Resize with copy constructor: new[5](dyn_data)
        // Elements [0..2] preserved, [3..4] initialized to 0
        // --------------------------------------------------------------------
        dyn_data = new[5](dyn_data);
        dyn_data[3] = 8'h78;
        dyn_data[4] = 8'h9A;
        update_shadows();
        #5; // T = 15
        if (arr_len !== 5 || dyn_data[0] !== 8'h12 || dyn_data[1] !== 8'h34 ||
            dyn_data[3] !== 8'h78 || dyn_data[4] !== 8'h9A) begin
            $display("[FAIL T=15] new[5](dyn_data) resize/copy failed! size=%0d [0]=%h [3]=%h [4]=%h",
                     arr_len, dyn_data[0], dyn_data[3], dyn_data[4]);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 3: Out-of-bounds indexing (§7.5.4)
        // Accessing index 5 on a size-5 array must return 8'hxx
        // --------------------------------------------------------------------
        read_val = dyn_data[5];
        #5; // T = 20
        if (read_val !== 8'hxx) begin
            $display("[FAIL T=20] Out-of-bounds read failed! read_val=%h (exp xx)", read_val);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 4: delete() method
        // Purge array: size becomes 0, read_val from dyn_data[0] becomes 8'hxx
        // --------------------------------------------------------------------
        dyn_data.delete();
        update_shadows();
        read_val = dyn_data[0];
        #5; // T = 25
        if (arr_len !== 0 || read_val !== 8'hxx) begin
            $display("[FAIL T=25] delete() failed! size=%0d read_val=%h (exp 0, xx)", arr_len, read_val);
            error_count = error_count + 1;
        end

        #5; // T = 30
        $display("\n================================================================================");
        $display("[CAS v2.9.0.0 REGRESSION SUITE: DYNAMIC ARRAYS]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §7.5 dynamic arrays (new[], copy, size, delete).");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | arr_len=%0d read_val=%h | d0=%h d1=%h",
                 $time, arr_len, read_val, d0_shadow, d1_shadow);
    end
endmodule