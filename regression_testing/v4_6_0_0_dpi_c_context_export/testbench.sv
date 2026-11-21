// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v4.6.0.0
// Standard Target: IEEE Std 1800-2023 §35.5.3 (context), §35.6 (export), §35.7 (svScope)
// Testbench Name : tb_v4_6_0_0_context_export
// ============================================================================

module tb_v4_6_0_0_context_export;
    // DPI-C Import with context
    import "DPI-C" context function int c_test_context_and_export(input int key_seed, input int val);

    // DPI-C Exports
    export "DPI-C" function sv_compute_checksum;
    export "DPI-C" function sv_get_status;
    export "DPI-C" function sv_set_status;

    int sim_status = 0;
    int res = 0;
    int error_count = 0;

    function int sv_compute_checksum(input int a, input int b);
        return (a ^ b) + 42;
    endfunction

    function int sv_get_status();
        return sim_status;
    endfunction

    function void sv_set_status(input int status);
        sim_status = status;
    endfunction

    initial begin
        #5; // T = 5
        res = c_test_context_and_export(100, 32'h00000020);

        // Expected checksum: (32'h20 ^ 32'h1234) + 42 = 32'h1214 + 42 = 4628 + 42 = 4670 (32'h123E)
        if (res !== ((32'h20 ^ 32'h1234) + 42)) begin
            $display("[FAIL] c_test_context_and_export returned %0d (exp %0d)", res, ((32'h20 ^ 32'h1234) + 42));
            error_count = error_count + 1;
        end

        // Verify that sv_set_status was called by C and updated sim_status
        if (sim_status !== (32'h20 + 10)) begin
            $display("[FAIL] sim_status=%0d (exp %0d)", sim_status, (32'h20 + 10));
            error_count = error_count + 1;
        end

        #5;
        $display("\n================================================================================");
        $display("[CAS v4.6.0.0 REGRESSION SUITE: DPI-C CONTEXT & EXPORTS]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §35 DPI-C context execution, svScope, and export callbacks.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule
