// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.31.0.0
// Standard Target: IEEE Std 1800-2023 §13.5 & §13.6 (Subroutine Defaults & Named Args)
// Testbench Name : tb_v3_31_0_0_regression
// ============================================================================

module tb_v3_31_0_0_regression;
    logic        clk;
    logic        rst_n;
    logic [31:0] base_addr;
    logic        trigger;
    logic [31:0] computed_addr;
    logic [7:0]  tag_out;

    int pass_count  = 0;
    int error_count = 0;

    v3_31_0_0_addr_engine dut (
        .clk          (clk),
        .rst_n        (rst_n),
        .base_addr    (base_addr),
        .trigger      (trigger),
        .computed_addr(computed_addr),
        .tag_out      (tag_out)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    // Testbench standalone helper functions
    function automatic int pack_metrics(int latency = 1, int burst = 4, int qos = 0);
        return (qos << 16) | (burst << 8) | latency;
    endfunction

    initial begin
        rst_n     = 0;
        trigger   = 0;
        base_addr = 32'h1000;

        #12;
        rst_n = 1;

        // --------------------------------------------------------------------
        // Test 1: Empty slot call in positional arguments (§13.5.3)
        // pack_metrics(2, , 3) -> latency=2, burst default=4, qos=3
        // exp = (3 << 16) | (4 << 8) | 2 = 32'h00030402
        // --------------------------------------------------------------------
        begin
            int res;
            res = pack_metrics(2, , 3);
            if (res !== 32'h0003_0402) begin
                $display("[FAIL T=%0t] Empty argument slot call failed! got %h (exp 00030402)", $time, res);
                error_count++;
            end else begin
                pass_count++;
            end
        end

        // --------------------------------------------------------------------
        // Test 2: Full default call
        // pack_metrics() -> latency=1, burst=4, qos=0 -> 32'h00000401
        // --------------------------------------------------------------------
        begin
            int res;
            res = pack_metrics();
            if (res !== 32'h0000_0401) begin
                $display("[FAIL T=%0t] Full default call failed! got %h (exp 00000401)", $time, res);
                error_count++;
            end else begin
                pass_count++;
            end
        end

        // --------------------------------------------------------------------
        // Test 3: Named argument invocation on DUT task
        // At posedge clk with trigger=1, base=32'h1000, multiplier=4.
        // offset default = 16, tag default = 8'hA0.
        // res_addr = 32'h1000 + (16 * 4) = 32'h1040.
        // tag_out = 8'hA0.
        // --------------------------------------------------------------------
        @(negedge clk);
        base_addr = 32'h1000;
        trigger   = 1;

        @(posedge clk);
        #1;
        trigger   = 0;
        if (computed_addr !== 32'h0000_1040 || tag_out !== 8'hA0) begin
            $display("[FAIL T=%0t] DUT named task call failed! addr=%h (exp 1040) tag=%h (exp A0)",
                     $time, computed_addr, tag_out);
            error_count++;
        end else begin
            pass_count++;
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.31.0.0 REGRESSION SUITE: SUBROUTINE DEFAULTS & NAMED ARGS]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §13.5 & §13.6 subroutine defaults, named args, and empty slots.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | rst_n=%b trig=%b | addr=%h tag=%h",
                 $time, rst_n, trigger, computed_addr, tag_out);
    end
endmodule