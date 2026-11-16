// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.95.0.0
// Standard Target: IEEE Std 1800-2023 §7.4.5 Multidimensional Arrays & §13.4/§13.5 Subroutine Array Ports
// Module Name    : v3_95_0_0_multidim_subarrays_and_subroutine_arrays_core
// ============================================================================

`timescale 1ns/1ps

module v3_95_0_0_multidim_subarrays_and_subroutine_arrays_core (
    input  logic        clk,
    input  logic        rst_n,
    output logic [31:0] out_word_copy,
    output int          out_sub_0,
    output int          out_sub_1,
    output int          out_sub_2,
    output int          out_sub_3,
    output int          out_task_sum,
    output int          out_func_sum
);
    // 1. Multi-packed + 1-indexed unpacked array (§7.4.5)
    bit [3:0][7:0] arr_a [1:10];
    bit [3:0][7:0] arr_b [1:10];

    // 2. 3D unpacked array subarray copy (§7.4.5)
    int A[2][3][4];
    int B[2][3][4];

    // 3. Task & Function formal ports with unpacked dimensions (§13.4, §13.5)
    task automatic check_task(input int a [2:0], output int sum_out);
        sum_out = a[0] + a[1] + a[2];
    endtask

    function automatic int sum_fn(int v [2:0]);
        return v[0] + v[1] + v[2];
    endfunction

    int test_arr [2:0];
    int t_sum_tmp;

    initial begin
        out_word_copy = 32'd0;
        out_sub_0     = 0;
        out_sub_1     = 0;
        out_sub_2     = 0;
        out_sub_3     = 0;
        out_task_sum  = 0;
        out_func_sum  = 0;
        t_sum_tmp     = 0;

        // 1. Exercise multi-packed word copy with non-zero start index [1:10]
        arr_a[1] = 32'hDEADBEEF;
        arr_b[2] = arr_a[1];
        out_word_copy = arr_b[2];

        // 2. Exercise 3D unpacked subarray copy
        A[0][2][0] = 5;
        A[0][2][1] = 6;
        A[0][2][2] = 7;
        A[0][2][3] = 8;
        B[1][1] = A[0][2];
        out_sub_0 = B[1][1][0];
        out_sub_1 = B[1][1][1];
        out_sub_2 = B[1][1][2];
        out_sub_3 = B[1][1][3];

        // 3. Exercise subroutine unpacked array ports
        test_arr[0] = 10;
        test_arr[1] = 20;
        test_arr[2] = 30;

        check_task(test_arr, t_sum_tmp);
        out_task_sum = t_sum_tmp;

        out_func_sum = sum_fn(test_arr);
    end

endmodule
