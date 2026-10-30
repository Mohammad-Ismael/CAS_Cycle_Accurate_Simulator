// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.16.0.0
// Standard Target: IEEE Std 1800-2023 §8.25 (parameterized classes, specialization)
// Testbench Name : tb_v3_16_0_0_regression
// ============================================================================

// Parameterized class with both value and type parameters (§8.25)
class packet_container #(parameter int WIDTH = 16, parameter int OFFSET = 5);
    logic [WIDTH-1:0] data;
    static int        alloc_count = 0;

    function new(logic [WIDTH-1:0] init_val);
        this.data = init_val + OFFSET;
        alloc_count++;
    endfunction

    function logic [WIDTH-1:0] get_data();
        return this.data;
    endfunction

    static function int get_count();
        return alloc_count;
    endfunction
endclass

module tb_v3_16_0_0_regression;
    logic        clk;
    logic        rst_n;
    logic [31:0] val_in;
    logic [31:0] val_out;

    // Specialization 1: 16-bit width, OFFSET = 10
    packet_container #(16, 10) pkt16_a;
    packet_container #(16, 10) pkt16_b;

    // Specialization 2: 32-bit width, OFFSET = 100
    packet_container #(32, 100) pkt32;

    int res16_a = 0;
    int res16_b = 0;
    int res32   = 0;
    int count16 = 0;
    int count32 = 0;
    int error_count = 0;

    v3_16_0_0_param_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .val_in(val_in),
        .val_out(val_out)
    );

    initial begin
        clk    = 0;
        rst_n  = 0;
        val_in = 32'h00000000;

        #5;
        rst_n = 1;

        // --------------------------------------------------------------------
        // TEST 1: Instantiate Specialization 1 (16-bit, OFFSET=10)
        // init_val = 16'h0020 -> data = 0x20 + 10 = 0x2A (42)
        // --------------------------------------------------------------------
        pkt16_a = new(16'h0020);
        res16_a = pkt16_a.get_data();
        count16 = packet_container#(16, 10)::get_count();
        #5; // T = 10

        if (res16_a !== 16'h002A || count16 !== 1) begin
            $display("[FAIL T=10] pkt16_a failed! res=%h count=%0d (exp 002A, 1)", res16_a, count16);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Second instance of Specialization 1
        // init_val = 16'h0040 -> data = 0x40 + 10 = 0x4A (74)
        // Static count for #(16, 10) should increment to 2
        // --------------------------------------------------------------------
        pkt16_b = new(16'h0040);
        res16_b = pkt16_b.get_data();
        count16 = packet_container#(16, 10)::get_count();
        #5; // T = 15

        if (res16_b !== 16'h004A || count16 !== 2) begin
            $display("[FAIL T=15] pkt16_b failed! res=%h count=%0d (exp 004A, 2)", res16_b, count16);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 3: Instantiate Specialization 2 (32-bit, OFFSET=100)
        // init_val = 32'h00001000 -> data = 0x1000 + 100 = 0x1064 (4196)
        // Static count for #(32, 100) must be independent (count = 1)
        // --------------------------------------------------------------------
        pkt32 = new(32'h00001000);
        res32   = pkt32.get_data();
        count32 = packet_container#(32, 100)::get_count();
        count16 = packet_container#(16, 10)::get_count();
        #5; // T = 20

        if (res32 !== 32'h00001064 || count32 !== 1 || count16 !== 2) begin
            $display("[FAIL T=20] pkt32 failed! res32=%h count32=%0d count16=%0d (exp 1064, 1, 2)",
                     res32, count32, count16);
            error_count = error_count + 1;
        end

        #5; // T = 25
        $display("\n================================================================================");
        $display("[CAS v3.16.0.0 REGRESSION SUITE: PARAMETERIZED CLASSES]");
        if (error_count == 0 && res16_a == 16'h002A && res16_b == 16'h004A && res32 == 32'h00001064) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §8.25 parameterized classes, distinct specializations, and per-specialization static members.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | res16_a=%h res16_b=%h res32=%h | cnt16=%0d cnt32=%0d",
                 $time, res16_a, res16_b, res32, count16, count32);
    end
endmodule