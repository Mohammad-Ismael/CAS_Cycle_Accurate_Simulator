`timescale 1ns/1ps

//==============================================================================
// Testbench: tb_v3_87_0_regression
// Standard: IEEE 1800-2023 SystemVerilog (§11.3.6 assign-in-expression, §6.8 2-state defaults)
//==============================================================================

module tb_v3_87_0_regression;

    logic        clk;
    logic        rst_n;

    logic [31:0] def_int_val;
    logic [7:0]  def_bit_val;
    logic [7:0]  def_byte_val;
    logic [15:0] def_shortint_val;
    logic [63:0] def_longint_val;

    logic [31:0] chain_a;
    logic [31:0] chain_b;
    logic [31:0] chain_c;

    logic [31:0] comp_r1;
    logic [31:0] comp_r2;
    logic [31:0] comp_r3;
    logic [31:0] comp_x;
    logic [31:0] comp_y;
    logic [31:0] comp_z;
    logic [31:0] shift_s1;
    logic [31:0] shift_s2;
    logic [31:0] shift_s;

    logic [31:0] inc_p1;
    logic [31:0] inc_p2;
    logic [31:0] inc_p3;
    logic [31:0] inc_p4;
    logic [31:0] inc_k;

    logic [31:0] stmt_cnt;

    integer errors = 0;

    v3_87_0_0_assign_in_expression dut (.*);

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n = 0;
        #10;
        rst_n = 1;
        #10;

        // 1. Verify 2-state default zero initialization
        if (def_int_val !== 32'd0) begin
            $display("[FAIL T=%0t] def_int_val mismatch: exp=0 got=%0d", $time, def_int_val);
            errors++;
        end else begin
            $display("[PASS T=%0t] def_int_val = %0d (2-state default zero verified)", $time, def_int_val);
        end

        if (def_bit_val !== 8'd0) begin
            $display("[FAIL T=%0t] def_bit_val mismatch: exp=0 got=%0d", $time, def_bit_val);
            errors++;
        end else begin
            $display("[PASS T=%0t] def_bit_val = %0d (2-state default zero verified)", $time, def_bit_val);
        end

        if (def_byte_val !== 8'd0) begin
            $display("[FAIL T=%0t] def_byte_val mismatch: exp=0 got=%0d", $time, def_byte_val);
            errors++;
        end else begin
            $display("[PASS T=%0t] def_byte_val = %0d (2-state default zero verified)", $time, def_byte_val);
        end

        if (def_shortint_val !== 16'd0) begin
            $display("[FAIL T=%0t] def_shortint_val mismatch: exp=0 got=%0d", $time, def_shortint_val);
            errors++;
        end else begin
            $display("[PASS T=%0t] def_shortint_val = %0d (2-state default zero verified)", $time, def_shortint_val);
        end

        if (def_longint_val !== 64'd0) begin
            $display("[FAIL T=%0t] def_longint_val mismatch: exp=0 got=%0d", $time, def_longint_val);
            errors++;
        end else begin
            $display("[PASS T=%0t] def_longint_val = %0d (2-state default zero verified)", $time, def_longint_val);
        end

        // 2. Verify chained assignments
        if (chain_a !== 32'd42 || chain_b !== 32'd42 || chain_c !== 32'd42) begin
            $display("[FAIL T=%0t] chained assignment mismatch: exp=42 got a=%0d b=%0d c=%0d", $time, chain_a, chain_b, chain_c);
            errors++;
        end else begin
            $display("[PASS T=%0t] chained assignment a=b=c=42 verified", $time);
        end

        // 3. Verify compound assignments
        if (comp_r1 !== 32'd15 || comp_r2 !== 32'd12 || comp_x !== 32'd12) begin
            $display("[FAIL T=%0t] compound assignment mismatch: exp r1=15 r2=12 x=12 got r1=%0d r2=%0d x=%0d", $time, comp_r1, comp_r2, comp_x);
            errors++;
        end else begin
            $display("[PASS T=%0t] compound assignments (x+=5)=15 and (x-=3)=12 verified", $time);
        end

        if (comp_r3 !== 32'd17 || comp_y !== 32'd17 || comp_z !== 32'd6) begin
            $display("[FAIL T=%0t] nested compound assignment mismatch: exp r3=17 y=17 z=6 got r3=%0d y=%0d z=%0d", $time, comp_r3, comp_y, comp_z);
            errors++;
        end else begin
            $display("[PASS T=%0t] nested compound assignment ((y += (z += 2) + 1))=17 verified", $time);
        end

        if (shift_s1 !== 32'd16 || shift_s2 !== 32'd8 || shift_s !== 32'd8) begin
            $display("[FAIL T=%0t] shift compound assignment mismatch: exp s1=16 s2=8 s=8 got s1=%0d s2=%0d s=%0d", $time, shift_s1, shift_s2, shift_s);
            errors++;
        end else begin
            $display("[PASS T=%0t] shift compound assignments (s<<=2)=16 and (s>>=1)=8 verified", $time);
        end

        // 4. Verify prefix and postfix inc/dec in expressions
        if (inc_p1 !== 32'd11 || inc_p2 !== 32'd11 || inc_p3 !== 32'd11 || inc_p4 !== 32'd11 || inc_k !== 32'd10) begin
            $display("[FAIL T=%0t] inc/dec in expr mismatch: exp p1..p4=11 k=10 got p1=%0d p2=%0d p3=%0d p4=%0d k=%0d", $time, inc_p1, inc_p2, inc_p3, inc_p4, inc_k);
            errors++;
        end else begin
            $display("[PASS T=%0t] prefix/postfix inc/dec in expr p1=%0d p2=%0d p3=%0d p4=%0d k=%0d verified", $time, inc_p1, inc_p2, inc_p3, inc_p4, inc_k);
        end

        // 5. Verify standalone inc/dec statements
        if (stmt_cnt !== 32'd5) begin
            $display("[FAIL T=%0t] stmt_cnt mismatch: exp=5 got=%0d", $time, stmt_cnt);
            errors++;
        end else begin
            $display("[PASS T=%0t] stmt_cnt = %0d (standalone ++/-- statements verified)", $time, stmt_cnt);
        end

        #10;
        if (errors == 0) begin
            $display("\n================================================================================");
            $display("[CAS v3.87.0.0 REGRESSION SUITE: ASSIGN IN EXPRESSION]");
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800 assignment in expression, ++/--, and 2-state default 0.");
            $display("================================================================================\n");
        end else begin
            $display("\n[FATAL] v3_87_0_0_assign_in_expression failed with %0d errors!\n", errors);
        end

        $finish;
    end

endmodule
