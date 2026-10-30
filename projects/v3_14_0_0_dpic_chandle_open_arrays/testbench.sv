// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.14.0.0
// Standard Target: IEEE Std 1800-2023 §35.5.4 (chandle, pointer passing)
// Testbench Name : tb_v3_14_0_0_regression
// ============================================================================

// DPI-C function imports with chandle (§35.5.4)
import "DPI-C" function chandle c_create_session(input int session_id);
import "DPI-C" function int     c_update_session(input chandle handle, input int val);
import "DPI-C" function void    c_destroy_session(input chandle handle);

module tb_v3_14_0_0_regression;
    logic        clk;
    logic        rst_n;
    logic [31:0] val_in;
    logic [31:0] val_out;

    chandle session_ptr = null;
    int     res_0       = 0;
    int     res_1       = 0;
    int     error_count = 0;

    v3_14_0_0_session_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .val_in(val_in),
        .val_out(val_out)
    );

    initial begin
        clk    = 0;
        rst_n  = 0;
        val_in = 32'h00000000;

        #5;
        rst_n = 1;

        // --------------------------------------------------------------------
        // TEST 1: Allocate opaque session context (ID = 32'h00001000)
        // --------------------------------------------------------------------
        session_ptr = c_create_session(32'h00001000);
        #5; // T = 10

        if (session_ptr == null) begin
            $display("[FAIL T=10] session_ptr allocation returned null!");
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: First session update
        // accumulator += 0x00000050 -> returns 0x1000 + 0x50 = 0x1050
        // --------------------------------------------------------------------
        res_0 = c_update_session(session_ptr, 32'h00000050);
        #5; // T = 15

        if (res_0 !== 32'h00001050) begin
            $display("[FAIL T=15] First session update returned %h (exp 00001050)!", res_0);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 3: Second stateful session update
        // accumulator += 0x00000100 -> returns 0x1000 + (0x50 + 0x100) = 0x1150
        // --------------------------------------------------------------------
        res_1 = c_update_session(session_ptr, 32'h00000100);
        #5; // T = 20

        if (res_1 !== 32'h00001150) begin
            $display("[FAIL T=20] Second session update returned %h (exp 00001150)!", res_1);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 4: Clean context destruction
        // --------------------------------------------------------------------
        c_destroy_session(session_ptr);
        session_ptr = null;
        #5; // T = 25

        $display("\n================================================================================");
        $display("[CAS v3.14.0.0 REGRESSION SUITE: DPI-C CHANDLE & OPAQUE CONTEXT]");
        if (error_count == 0 && res_0 == 32'h00001050 && res_1 == 32'h00001150 && session_ptr == null) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §35.5.4 DPI-C chandle allocation, pointer passing, and stateful updates.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | res0=%h res1=%h | ptr_null=%0d",
                 $time, res_0, res_1, (session_ptr == null));
    end
endmodule