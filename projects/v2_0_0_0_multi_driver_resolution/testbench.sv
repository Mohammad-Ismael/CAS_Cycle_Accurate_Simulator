// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v2.0.0.0
// Standard Target: IEEE Std 1800-2023 §6.5 & §10.3 (Multi-Driver Wire Resolution)
// Testbench Name : tb_v2_0_0_0_regression
// ============================================================================

module tb_v2_0_0_0_regression;
    logic [3:0] drv_a;
    logic [3:0] drv_b;
    logic       en_a;
    logic       en_b;
    logic [3:0] res_wire;
    logic [3:0] res_wand;
    logic [3:0] res_wor;
    logic [3:0] res_tri_bus;
    integer     error_count = 0;

    v2_0_0_0_multi_driver_core dut (
        .drv_a(drv_a),
        .drv_b(drv_b),
        .en_a(en_a),
        .en_b(en_b),
        .res_wire(res_wire),
        .res_wand(res_wand),
        .res_wor(res_wor),
        .res_tri_bus(res_tri_bus)
    );

    initial begin
        // Time 0: Initial states
        drv_a = 4'b0000;
        drv_b = 4'b0000;
        en_a  = 0;
        en_b  = 0;
        #5;

        // --------------------------------------------------------------------
        // TEST 1: Identical values (0 & 0)
        // --------------------------------------------------------------------
        #5;
        if (res_wire !== 4'b0000 || res_wand !== 4'b0000 || res_wor !== 4'b0000 || res_tri_bus !== 4'bzzzz) begin
            $display("[FAIL T=10] Identical 0 test failed!");
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Conflicting logic values (drv_a=1100, drv_b=1010)
        // --------------------------------------------------------------------
        drv_a = 4'b1100;
        drv_b = 4'b1010;
        #5;
        if (res_wire !== 4'b1xx0 || res_wand !== 4'b1000 || res_wor !== 4'b1110) begin
            $display("[FAIL T=15] Conflict resolution failed! wire=%b wand=%b wor=%b", res_wire, res_wand, res_wor);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 3: Tristate/High-Impedance resolution (drv_a=1010, drv_b=zzzz)
        // --------------------------------------------------------------------
        drv_a = 4'b1010;
        drv_b = 4'bzzzz;
        #5;
        if (res_wire !== 4'b1010 || res_wand !== 4'b1010 || res_wor !== 4'b1010) begin
            $display("[FAIL T=20] High-impedance resolution failed! wire=%b wand=%b wor=%b", res_wire, res_wand, res_wor);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 4: Tristate Bus Multiplexing
        // --------------------------------------------------------------------
        // Enable driver A only
        drv_a = 4'b1001;
        drv_b = 4'b0110;
        en_a  = 1;
        en_b  = 0;
        #5;
        if (res_tri_bus !== 4'b1001) begin
            $display("[FAIL T=25] Bus Driver A enable failed! res_tri_bus=%b (exp 1001)", res_tri_bus);
            error_count = error_count + 1;
        end

        // Enable driver B only
        en_a = 0;
        en_b = 1;
        #5;
        if (res_tri_bus !== 4'b0110) begin
            $display("[FAIL T=30] Bus Driver B enable failed! res_tri_bus=%b (exp 0110)", res_tri_bus);
            error_count = error_count + 1;
        end

        // Bus contention: Both enabled -> 1001 vs 0110 => xxxx
        en_a = 1;
        en_b = 1;
        #5;
        if (res_tri_bus !== 4'bxxxx) begin
            $display("[FAIL T=35] Bus contention resolution failed! res_tri_bus=%b (exp xxxx)", res_tri_bus);
            error_count = error_count + 1;
        end

        // Both disabled -> 4'bzzzz
        en_a = 0;
        en_b = 0;
        #5;
        if (res_tri_bus !== 4'bzzzz) begin
            $display("[FAIL T=40] Bus float resolution failed! res_tri_bus=%b (exp zzzz)", res_tri_bus);
            error_count = error_count + 1;
        end

        #5;
        $display("\n================================================================================");
        $display("[CAS v2.0.0.0 REGRESSION SUITE: MULTI-DRIVER WIRE RESOLUTION]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §6.5 & §10.3 wire, wand, wor, and tri resolution.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | drv_a=%b drv_b=%b en_a=%b en_b=%b | wire=%b wand=%b wor=%b tri_bus=%b",
                 $time, drv_a, drv_b, en_a, en_b, res_wire, res_wand, res_wor, res_tri_bus);
    end
endmodule