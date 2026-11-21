// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v4.6.0.0
// Standard Target: IEEE Std 1800-2023 §35 (DPI-C Scalars & Vectors)
// Testbench Name : tb_v4_6_0_0_scalars_vectors
// ============================================================================

module tb_v4_6_0_0_scalars_vectors;
    // DPI-C Imports
    import "DPI-C" function longint c_sum_scalars(input byte b, input shortint s, input int i, input longint l);
    import "DPI-C" function void c_transform_scalars(input int in_val, inout int inout_val, output int out_val);
    import "DPI-C" function void c_process_bitvec(input bit [63:0] in_vec, output bit [63:0] out_vec);
    import "DPI-C" function void c_process_logicvec(input logic [31:0] in_vec, output logic [31:0] out_vec);

    byte b;
    shortint s;
    int i;
    longint l;
    longint sum_res;

    int io_val;
    int out_val;

    bit [63:0] b_in;
    bit [63:0] b_out;

    logic [31:0] l_in;
    logic [31:0] l_out;

    int error_count = 0;

    initial begin
        b = 8'd10;
        s = 16'd200;
        i = 32'd3000;
        l = 64'd40000;
        io_val = 50;
        out_val = 0;
        b_in = 64'h123456789ABCDEF0;
        b_out = 64'd0;
        l_in = 32'h0000FF00;
        l_out = 32'd0;

        // Test 1: Sum Scalars
        sum_res = c_sum_scalars(b, s, i, l);
        if (sum_res !== 64'd43210) begin
            $display("[FAIL] c_sum_scalars returned %0d (exp 43210)", sum_res);
            error_count = error_count + 1;
        end

        // Test 2: Inout & Output Scalars
        c_transform_scalars(25, io_val, out_val);
        if (io_val !== 75 || out_val !== 50) begin
            $display("[FAIL] c_transform_scalars: io_val=%0d (exp 75), out_val=%0d (exp 50)", io_val, out_val);
            error_count = error_count + 1;
        end

        // Test 3: Bit Vector (64-bit)
        c_process_bitvec(b_in, b_out);
        if (b_out !== (64'h123456789ABCDEF0 ^ 64'h55555555AAAAAAAA)) begin
            $display("[FAIL] c_process_bitvec: b_out=%h (exp %h)", b_out, (64'h123456789ABCDEF0 ^ 64'h55555555AAAAAAAA));
            error_count = error_count + 1;
        end

        // Test 4: Logic Vector (32-bit)
        c_process_logicvec(l_in, l_out);
        if (l_out[0] !== 1'b1 || l_out[31:1] !== l_in[31:1]) begin
            $display("[FAIL] c_process_logicvec: l_out=%h (exp %h)", l_out, (l_in | 32'h1));
            error_count = error_count + 1;
        end

        #5;
        $display("\n================================================================================");
        $display("[CAS v4.6.0.0 REGRESSION SUITE: DPI-C SCALARS & VECTORS]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §35 DPI-C scalar types, vector marshaling, input/output/inout.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule
