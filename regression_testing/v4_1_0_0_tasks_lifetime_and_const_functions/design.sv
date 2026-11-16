`timescale 1ns/1ps

module tasks_lifetime_and_const_functions_dut (
    input  logic        clk,
    output logic [31:0] const_out
);

    // 1. Constant functions in localparam evaluation (IEEE 1800-2023 13.4.3)
    function automatic int const_calc(int val);
        return val * 3 + 7;
    endfunction

    localparam int CONST_VAL = const_calc(5); // 5 * 3 + 7 = 22

    assign const_out = CONST_VAL;

endmodule
