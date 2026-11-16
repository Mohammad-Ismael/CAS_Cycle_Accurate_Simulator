// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.4.0.0
// Standard Target: IEEE Std 1800-2023 §8 (Classes, Handles, Constructors, Methods)
//                  and §8.4 / §21.2.3 (Dynamic Type Context Restrictions [CAS-DTINPCIL])
// Testbench Name : tb_v3_4_0_0_regression
// ============================================================================

// Transaction class (§8.3)
class transaction;
    bit [7:0]  cmd;
    bit [15:0] addr;
    bit [31:0] data;

    // User-defined constructor (§8.7)
    function new(bit [7:0] c = 8'h00, bit [15:0] a = 16'h0000, bit [31:0] d = 32'h00000000);
        this.cmd  = c;
        this.addr = a;
        this.data = d;
    endfunction

    // Class member function (§8.5)
    function bit [31:0] calc_checksum();
        return this.data ^ {16'h0000, this.addr} ^ {24'h000000, this.cmd};
    endfunction

    // Class member task (§8.5)
    task update_payload(bit [31:0] new_data);
        this.data = new_data;
    endtask
endclass

module tb_v3_4_0_0_regression;
    logic        clk;
    logic        rst_n;
    logic [7:0]  cmd_in;
    logic [15:0] addr_in;
    logic [31:0] data_in;
    logic [31:0] result_out;

    // Class object handles (§8.4)
    transaction pkt1;
    transaction pkt2;

    bit [31:0] chksum1;
    bit [31:0] chksum2;
    logic      pkt1_null = 1'b1;
    logic      pkt2_null = 1'b1;
    integer    error_count = 0;

    v3_4_0_0_class_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .cmd_in(cmd_in),
        .addr_in(addr_in),
        .data_in(data_in),
        .result_out(result_out)
    );

    task update_shadows();
        begin
            pkt1_null = (pkt1 == null);
            pkt2_null = (pkt2 == null);
        end
    endtask

    initial begin
        clk     = 0;
        rst_n   = 0;
        cmd_in  = 8'h00;
        addr_in = 16'h0000;
        data_in = 32'h00000000;
        chksum1 = 32'h00000000;
        chksum2 = 32'h00000000;

        update_shadows();
        #5;
        rst_n = 1;

        // --------------------------------------------------------------------
        // TEST 1: Null handle check (§8.4)
        // --------------------------------------------------------------------
        if (pkt1 !== null) begin
            $display("[FAIL T=5] Uninstantiated handle pkt1 should be null!");
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Object instantiation via constructor with arguments (§8.7)
        // --------------------------------------------------------------------
        pkt1 = new(8'h01, 16'h1234, 32'hA5A50000);
        update_shadows();
        #5; // T = 10
        if (pkt1 == null || pkt1.cmd !== 8'h01 || pkt1.addr !== 16'h1234 || pkt1.data !== 32'hA5A50000) begin
            $display("[FAIL T=10] pkt1 constructor failed! cmd=%h addr=%h data=%h",
                     pkt1.cmd, pkt1.addr, pkt1.data);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 3: Method invocation & return value (§8.5)
        // Checksum = 32'hA5A50000 ^ 32'h00001234 ^ 32'h00000001 = 32'hA5A51235
        // --------------------------------------------------------------------
        chksum1 = pkt1.calc_checksum();
        if (chksum1 !== 32'hA5A51235) begin
            $display("[FAIL T=10] calc_checksum() failed! chksum1=%h (exp A5A51235)", chksum1);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 4: Member task modification & second independent object
        // --------------------------------------------------------------------
        pkt1.update_payload(32'hDEADBEEF);
        pkt2 = new(8'hFF, 16'hFFFF, 32'h00000000);
        update_shadows();
        #5; // T = 15

        if (pkt1.data !== 32'hDEADBEEF) begin
            $display("[FAIL T=15] update_payload task failed on pkt1! data=%h", pkt1.data);
            error_count = error_count + 1;
        end

        if (pkt2.cmd !== 8'hFF || pkt2.addr !== 16'hFFFF || pkt2.data !== 32'h00000000) begin
            $display("[FAIL T=15] pkt2 independent allocation failed!");
            error_count = error_count + 1;
        end

        chksum2 = pkt2.calc_checksum();
        // 32'h00000000 ^ 32'h0000FFFF ^ 32'h000000FF = 32'h0000FF00
        if (chksum2 !== 32'h0000FF00) begin
            $display("[FAIL T=15] pkt2 calc_checksum failed! chksum2=%h (exp 0000FF00)", chksum2);
            error_count = error_count + 1;
        end

        #5; // T = 20
        $display("\n================================================================================");
        $display("[CAS v3.4.0.0 REGRESSION SUITE: OOP CLASSES & OBJECTS]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §8 class declarations, handles, new(), and methods.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | pkt1_null=%b pkt2_null=%b | chksum1=%h chksum2=%h",
                 $time, pkt1_null, pkt2_null, chksum1, chksum2);
    end
endmodule