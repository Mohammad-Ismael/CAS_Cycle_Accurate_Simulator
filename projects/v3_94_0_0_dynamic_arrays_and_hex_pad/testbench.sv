// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.94.0.0
// Testbench Name : tb_v3_94_0_regression
// ============================================================================

`timescale 1ns/1ps

module tb_v3_94_0_regression;
    logic        clk;
    logic        rst_n;
    int          out_size_init;
    int          out_size_resized;
    int          out_size_deleted;
    logic [7:0]  out_elem_read;
    string       out_hex_pad_zero;
    string       out_hex_pad_byte;
    string       out_hex_pad_upper;
    string       out_hex_unpadded;

    v3_94_0_0_dynamic_arrays_and_hex_pad_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .out_size_init(out_size_init),
        .out_size_resized(out_size_resized),
        .out_size_deleted(out_size_deleted),
        .out_elem_read(out_elem_read),
        .out_hex_pad_zero(out_hex_pad_zero),
        .out_hex_pad_byte(out_hex_pad_byte),
        .out_hex_pad_upper(out_hex_pad_upper),
        .out_hex_unpadded(out_hex_unpadded)
    );

    int errors = 0;

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n = 0;
        #1;

        if (out_size_init !== 16) begin
            $display("[FAIL] out_size_init = %0d, expected 16", out_size_init);
            errors = errors + 1;
        end else begin
            $display("[PASS] out_size_init = %0d", out_size_init);
        end

        if (out_elem_read !== 8'hA5) begin
            $display("[FAIL] out_elem_read = %h, expected a5", out_elem_read);
            errors = errors + 1;
        end else begin
            $display("[PASS] out_elem_read = %h", out_elem_read);
        end

        if (out_size_resized !== 8) begin
            $display("[FAIL] out_size_resized = %0d, expected 8", out_size_resized);
            errors = errors + 1;
        end else begin
            $display("[PASS] out_size_resized = %0d", out_size_resized);
        end

        if (out_size_deleted !== 0) begin
            $display("[FAIL] out_size_deleted = %0d, expected 0", out_size_deleted);
            errors = errors + 1;
        end else begin
            $display("[PASS] out_size_deleted = %0d", out_size_deleted);
        end

        if (out_hex_pad_zero != "00") begin
            $display("[FAIL] out_hex_pad_zero = '%s', expected '00'", out_hex_pad_zero);
            errors = errors + 1;
        end else begin
            $display("[PASS] out_hex_pad_zero = '%s'", out_hex_pad_zero);
        end

        if (out_hex_pad_byte != "0f") begin
            $display("[FAIL] out_hex_pad_byte = '%s', expected '0f'", out_hex_pad_byte);
            errors = errors + 1;
        end else begin
            $display("[PASS] out_hex_pad_byte = '%s'", out_hex_pad_byte);
        end

        if (out_hex_pad_upper != "0A5" && out_hex_pad_upper != "0a5") begin
            $display("[FAIL] out_hex_pad_upper = '%s', expected '0A5' or '0a5'", out_hex_pad_upper);
            errors = errors + 1;
        end else begin
            $display("[PASS] out_hex_pad_upper = '%s'", out_hex_pad_upper);
        end

        if (out_hex_unpadded != "f") begin
            $display("[FAIL] out_hex_unpadded = '%s', expected 'f'", out_hex_unpadded);
            errors = errors + 1;
        end else begin
            $display("[PASS] out_hex_unpadded = '%s'", out_hex_unpadded);
        end

        #10;
        if (errors == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
        end else begin
            $display("RESULT : >>> FAIL (%0d errors) <<<", errors);
        end
        $finish;
    end
endmodule
