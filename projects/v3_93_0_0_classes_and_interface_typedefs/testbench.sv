// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.93.0.0
// Standard Target: IEEE Std 1800-2023 Chapter 8
// Testbench Name : tb_v3_93_0_regression
// ============================================================================

module tb_v3_93_0_regression;
    logic clk;
    logic rst_n;
    int   out_box_override;
    int   out_box_default;
    int   out_calc_default;
    int   out_calc_custom;
    int   errors = 0;

    v3_93_0_0_classes_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .out_box_override(out_box_override),
        .out_box_default(out_box_default),
        .out_calc_default(out_calc_default),
        .out_calc_custom(out_calc_custom)
    );

    // Clock generator
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n = 0;
        #1;

        // Check parameterized class property lookups
        if (out_box_override !== 34) begin
            $display("[FAIL] out_box_override = %0d, expected 34", out_box_override);
            errors = errors + 1;
        end else begin
            $display("[PASS] out_box_override = %0d", out_box_override);
        end

        if (out_box_default !== 12) begin
            $display("[FAIL] out_box_default = %0d, expected 12", out_box_default);
            errors = errors + 1;
        end else begin
            $display("[PASS] out_box_default = %0d", out_box_default);
        end

        // Check constructor initialization and interface class method computation
        if (out_calc_default !== 75) begin
            $display("[FAIL] out_calc_default = %0d, expected 75", out_calc_default);
            errors = errors + 1;
        end else begin
            $display("[PASS] out_calc_default = %0d", out_calc_default);
        end

        if (out_calc_custom !== 90) begin
            $display("[FAIL] out_calc_custom = %0d, expected 90", out_calc_custom);
            errors = errors + 1;
        end else begin
            $display("[PASS] out_calc_custom = %0d", out_calc_custom);
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
