`timescale 1ns/1ps

package math_pkg;
    // Sticky formal port type inheritance: b inherits int from a (IEEE 1800-2023 26.3)
    function int add(int a, b);
        return a + b;
    endfunction
endpackage

module sys_tasks_file_io_and_packages_dut #(
    parameter int P_UNB = $
)(
    input  logic        clk,
    output logic [31:0] pkg_add_out,
    output logic [31:0] itor_out,
    output logic [63:0] real_bits_out,
    output logic [31:0] shortreal_bits_out,
    output logic        unb_out,
    output logic        isunk_out,
    output logic [31:0] math_res_out
);
    import math_pkg::*;

    // 1. Package function call with sticky port inheritance
    assign pkg_add_out = add(15, 27); // 42

    // 2. Unbounded parameter check
    assign unb_out = $isunbounded(P_UNB);

    // 3. Real and conversion functions
    real r_val;
    shortreal sr_val;
    logic [63:0] r_bits;
    logic [31:0] sr_bits;
    logic is_unk;
    int math_val;

    always_comb begin
        r_val = $itor(20);
        r_bits = $realtobits(12.45);
        sr_val = 12.45;
        sr_bits = $shortrealtobits(sr_val);

        // Procedural parameter declaration with $isunknown
        begin
            parameter [3:0] proc_val = 4'b000x;
            is_unk = $isunknown(proc_val);
        end

        // Math functions: ceil(3.7)=4, floor(2.1)=2, sqrt(9.0)=3, ln(1.0)=0, log10(100.0)=2
        math_val = int'($ceil(3.7)) + int'($floor(2.1)) + int'($sqrt(9.0)) + int'($ln(1.0)) + int'($log10(100.0)); // 4+2+3+0+2 = 11
    end

    assign itor_out = int'(r_val);
    assign real_bits_out = r_bits;
    assign shortreal_bits_out = sr_bits;
    assign isunk_out = is_unk;
    assign math_res_out = math_val;

endmodule
