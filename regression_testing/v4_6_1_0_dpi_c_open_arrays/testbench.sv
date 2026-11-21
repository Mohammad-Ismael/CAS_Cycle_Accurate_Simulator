// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v4.6.1.0
// Standard Target: IEEE Std 1800-2023 §35.5.6.1, Annex I (DPI-C Open Arrays)
// Testbench Name : tb_v4_6_1_0_open_arrays
// ============================================================================

module tb_v4_6_1_0_open_arrays;
    import "DPI-C" function int sv_sum_array(input int arr[]);
    import "DPI-C" function void sv_scale_array(inout int arr[], input int factor);
    import "DPI-C" function int sv_check_2d(input int arr[][]);

    int a1[4];
    int a2[2][3];
    int sum_res;
    int check2d_res;

    initial begin
        a1[0] = 10;
        a1[1] = 20;
        a1[2] = 30;
        a1[3] = 40;

        sum_res = sv_sum_array(a1);
        $display("[SV] sv_sum_array returned: %0d", sum_res);
        if (sum_res !== 100) begin
            $display("[FATAL] Expected 100, got %0d", sum_res);
            $finish;
        end

        sv_scale_array(a1, 3);
        $display("[SV] after sv_scale_array(a1, 3): %0d %0d %0d %0d", a1[0], a1[1], a1[2], a1[3]);
        if (a1[0] !== 30 || a1[1] !== 60 || a1[2] !== 90 || a1[3] !== 120) begin
            $display("[FATAL] Open array inout write-back failed!");
            $finish;
        end

        a2[0][0] = 1; a2[0][1] = 2; a2[0][2] = 3;
        a2[1][0] = 4; a2[1][1] = 5; a2[1][2] = 6;
        check2d_res = sv_check_2d(a2);
        $display("[SV] sv_check_2d returned: %0d", check2d_res);
        if (check2d_res !== 21) begin
            $display("[FATAL] Expected 21, got %0d", check2d_res);
            $finish;
        end

        $display(">>> PASS (100%% VCS COMPLIANT) <<<");
        $finish;
    end
endmodule
