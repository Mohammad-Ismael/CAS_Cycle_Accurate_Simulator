// testbench.sv - Master Verification Harness (100% Pass Self-Checking)
`include "alu_defs.vh"
`include "top_mac.sv"

module testbench;
    logic       clk;
    logic       rst_n;
    logic [2:0] opcode;
    logic [3:0] op_a;
    logic [3:0] op_b;
    logic       cin;
    logic       mem_write;
    logic [3:0] mem_addr;
    logic [3:0] alu_result;
    logic       alu_cout;
    logic [7:0] mem_readback;

    // Instantiate Top-Level Device Under Test (DUT)
    top_mac dut (
        .clk(clk),
        .rst_n(rst_n),
        .opcode(opcode),
        .op_a(op_a),
        .op_b(op_b),
        .cin(cin),
        .mem_write(mem_write),
        .mem_addr(mem_addr),
        .alu_result(alu_result),
        .alu_cout(alu_cout),
        .mem_readback(mem_readback)
    );

    // Clock generator task
    task cycle_clock;
        begin
            #5 clk = 1;
            #5 clk = 0;
        end
    endtask

    initial begin
        $dumpfile("master_regression.vcd");
        $dumpvars;

        $display("================================================================");
        $display("       CAS MASTER REGRESSION HARNESS (SELF-CHECKING)            ");
        $display("================================================================");

        // 1. Initialize & Reset Phase
        clk = 0;
        rst_n = 0;
        opcode = 0;
        op_a = 0;
        op_b = 0;
        cin = 0;
        mem_write = 0;
        mem_addr = 0;

        #10 rst_n = 1;
        $display("[TEST 1/5] Reset sequence completed. DUT active.");

        // 2. Structural Gate Adder Test
        op_a = 4'd7;
        op_b = 4'd5;
        cin  = 1'b0;
        opcode = ALU_ADD;
        #1;
        if (alu_result == 4'd12 && alu_cout == 1'b0) begin
            $display("[TEST 2/5] Ripple Adder: 7 + 5 = %0d (cout=%0b) [PASS]", alu_result, alu_cout);
        end else begin
            $display("[FAIL] Adder mismatch: expected 12, got %0d", alu_result);
            $finish;
        end

        // 3. Ripple Adder with Carry-in and Overflow
        op_a = 4'd11;
        op_b = 4'd5;
        cin  = 1'b1;
        #1;
        if (alu_result == 4'd1 && alu_cout == 1'b1) begin
            $display("[TEST 3/5] Ripple Adder Overflow: 11 + 5 + 1 = %0d (cout=%0b) [PASS]", alu_result, alu_cout);
        end else begin
            $display("[FAIL] Adder overflow mismatch: expected 1, cout=1, got %0d, cout=%0b", alu_result, alu_cout);
            $finish;
        end

        // 4. Bitwise Logic & Shift Combinational Checks
        opcode = ALU_XOR;
        op_a = 4'b1010;
        op_b = 4'b1100;
        #1;
        if (alu_result == 4'b0110) begin
            $display("[TEST 4a/5] ALU Bitwise XOR: 1010 ^ 1100 = %0b [PASS]", alu_result);
        end else begin
            $display("[FAIL] XOR mismatch: expected 0110, got %0b", alu_result);
            $finish;
        end

        opcode = ALU_SHL;
        op_a = 4'b0011;
        op_b = 4'd2;
        #1;
        if (alu_result == 4'b1100) begin
            $display("[TEST 4b/5] ALU Shift Left: 0011 << 2 = %0b [PASS]", alu_result);
        end else begin
            $display("[FAIL] Shift mismatch: expected 1100, got %0b", alu_result);
            $finish;
        end

        // 5. Memory Array Write & Readback
        opcode = ALU_ADD;
        op_a = 4'd9;
        op_b = 4'd8;
        cin  = 1'b0;
        mem_addr = 4'hA;
        mem_write = 1'b1;
        #1;
        cycle_clock(); // Commit to RAM via posedge clk

        mem_write = 1'b0;
        #5;
        if (mem_readback == 8'h11) begin
            $display("[TEST 5/5] Register File RAM: Addr 0xA wrote & read back 0x%0h [PASS]", mem_readback);
        end else begin
            $display("[FAIL] RAM mismatch: expected 0x11, got 0x%0h", mem_readback);
            $finish;
        end

        #10;
        $display("================================================================");
        $display("   >>> VERDICT: 100%% ALL REGRESSION SUITES PASSED <<<         ");
        $display("================================================================");
        $finish;
    end

endmodule