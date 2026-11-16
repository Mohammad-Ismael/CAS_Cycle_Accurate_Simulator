// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v1.3.0.0
// Standard Target: IEEE Std 1800-2023 Chapters 12.7 (Loops) & 13.4 (Functions)
// Module Name    : v1_3_0_0_loop_func_core
// ============================================================================

module v1_3_0_0_loop_func_core (
    input  logic [7:0] in_data,
    input  logic [3:0] in_count,
    output logic [7:0] out_for_accum,
    output logic [7:0] out_while_shift,
    output logic [7:0] out_repeat_inc,
    output logic [7:0] out_func_result
);

    // IEEE 1800-2023 §13.4: Pure value-returning function
    function automatic logic [7:0] parity_byte(input logic [7:0] data);
        integer idx;
        logic [7:0] p_val;
        begin
            p_val = data;
            // Invert even bits via bitwise operation
            for (idx = 0; idx < 8; idx = idx + 2) begin
                p_val[idx] = ~p_val[idx];
            end
            parity_byte = p_val;
        end
    endfunction

    always_comb begin
        integer i;
        logic [7:0] temp_accum;
        logic [7:0] temp_shift;
        logic [7:0] temp_inc;
        integer cnt;

        // 1. For Loop: Accumulate sum over loop count
        temp_accum = 8'd0;
        for (i = 0; i < 4; i = i + 1) begin
            temp_accum = temp_accum + in_data;
        end
        out_for_accum = temp_accum;

        // 2. While Loop: Shift register until count decrements to 0
        temp_shift = in_data;
        cnt = in_count;
        while (cnt > 0) begin
            temp_shift = temp_shift << 1;
            cnt = cnt - 1;
        end
        out_while_shift = temp_shift;

        // 3. Repeat Loop: Incremental counter
        temp_inc = 8'd0;
        repeat (5) begin
            temp_inc = temp_inc + 8'd3;
        end
        out_repeat_inc = temp_inc;

        // 4. Function call invocation
        out_func_result = parity_byte(in_data);
    end

endmodule