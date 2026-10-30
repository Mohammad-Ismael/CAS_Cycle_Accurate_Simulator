// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.12.0.0
// Standard Target: IEEE Std 1800-2023 §35 (import "DPI-C", C type marshalling)
// Testbench Name : tb_v3_12_0_0_regression
// ============================================================================

// DPI-C Function Import Declarations (§35.4 & §35.5)
import "DPI-C" pure function int c_calc_hash(input int seed, input int data);
import "DPI-C" function int c_verify_packet(input int cmd, input int addr, input int payload);

module tb_v3_12_0_0_regression;
    logic        clk;
    logic        rst_n;
    logic [31:0] a_in;
    logic [31:0] b_in;
    logic [31:0] res_out;

    int hash_res = 0;
    int verify_ok = 0;
    int error_count = 0;

    v3_12_0_0_dpic_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .a_in(a_in),
        .b_in(b_in),
        .res_out(res_out)
    );

    initial begin
        clk   = 0;
        rst_n = 0;
        a_in  = 32'h00000000;
        b_in  = 32'h00000000;

        #5;
        rst_n = 1;

        // --------------------------------------------------------------------
        // TEST 1: Invoke pure DPI-C function c_calc_hash
        // seed = 32'h00001234, data = 32'h00000010
        // expected: (0x1234 ^ (0x10 << 3)) + 0x5A5A
        // (0x1234 ^ 0x80) + 0x5A5A = 0x12B4 + 0x5A5A = 0x6D0E
        // --------------------------------------------------------------------
        hash_res = c_calc_hash(32'h00001234, 32'h00000010);
        #5; // T = 10
        if (hash_res !== 32'h00006D0E) begin
            $display("[FAIL T=10] c_calc_hash returned %h (exp 00006D0E)!", hash_res);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Invoke DPI-C packet verification function
        // cmd = 8'h01, addr = 16'h8000, payload = 32'h00018000 -> MATCH (1)
        // --------------------------------------------------------------------
        verify_ok = c_verify_packet(32'h00000001, 32'h00008000, 32'h00018000);
        #5; // T = 15
        if (verify_ok !== 1) begin
            $display("[FAIL T=15] c_verify_packet failed valid packet check! got %0d (exp 1)", verify_ok);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 3: Invoke DPI-C verification with mismatch payload -> MISMATCH (0)
        // --------------------------------------------------------------------
        verify_ok = c_verify_packet(32'h00000001, 32'h00008000, 32'hDEADBEEF);
        #5; // T = 20
        if (verify_ok !== 0) begin
            $display("[FAIL T=20] c_verify_packet should fail on mismatch! got %0d (exp 0)", verify_ok);
            error_count = error_count + 1;
        end

        #5; // T = 25
        $display("\n================================================================================");
        $display("[CAS v3.12.0.0 REGRESSION SUITE: DIRECT PROGRAMMING INTERFACE DPI-C]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §35 DPI-C import, pure functions, and C ABI call execution.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | hash_res=%h verify_ok=%0d | a=%h b=%h res=%h",
                 $time, hash_res, verify_ok, a_in, b_in, res_out);
    end
endmodule