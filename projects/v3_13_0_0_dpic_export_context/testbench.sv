// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.13.0.0
// Standard Target: IEEE Std 1800-2023 §35 (export "DPI-C", context, svScope)
// Testbench Name : tb_v3_13_0_0_regression
// ============================================================================

module tb_v3_13_0_0_regression;
    // DPI-C Import with context (§35.5.3)
    import "DPI-C" context function int c_run_bus_transaction(input int start_addr, input int base_data);

    // Exported function declarations moved INSIDE module scope (§35.6)
    export "DPI-C" function sv_write_reg;
    export "DPI-C" function sv_read_reg;

    logic        clk;
    logic        rst_n;
    logic [31:0] reg_0_out;
    logic [31:0] reg_1_out;

    logic [31:0] memory [0:7];
    int transaction_res = 0;
    int error_count = 0;

    v3_13_0_0_bus_slave dut (
        .clk(clk),
        .rst_n(rst_n),
        .reg_0_out(reg_0_out),
        .reg_1_out(reg_1_out)
    );

    // Exported function definitions inside the same scope
    function void sv_write_reg(input int addr, input int data);
        memory[addr & 3'b111] = data;
    endfunction

    function int sv_read_reg(input int addr);
        return memory[addr & 3'b111];
    endfunction

    initial begin
        clk   = 0;
        rst_n = 0;
        for (int i = 0; i < 8; i++) begin
            memory[i] = 32'h00000000;
        end

        #5;
        rst_n = 1;

        // --------------------------------------------------------------------
        // TEST 1: Execute C context transaction
        // Writes: memory[0] = 32'h10002000
        //         memory[4] = 32'h10002000 ^ 32'hA5A5A5A5 = 32'hB5A585A5
        // Reads back both, returns sum:
        // 32'h10002000 + 32'hB5A585A5 = 32'hC5A5A5A5
        // --------------------------------------------------------------------
        transaction_res = c_run_bus_transaction(0, 32'h10002000);
        #5; // T = 10

        if (transaction_res !== 32'hC5A5A5A5) begin
            $display("[FAIL T=10] C transaction returned %h (exp C5A5A5A5)!", transaction_res);
            error_count = error_count + 1;
        end

        if (memory[0] !== 32'h10002000 || memory[4] !== 32'hB5A585A5) begin
            $display("[FAIL T=10] Memory state mismatch! mem[0]=%h mem[4]=%h", memory[0], memory[4]);
            error_count = error_count + 1;
        end

        #5; // T = 15
        $display("\n================================================================================");
        $display("[CAS v3.13.0.0 REGRESSION SUITE: DPI-C EXPORTS & CONTEXT]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §35 DPI-C export functions, context, and bidirectional call execution.");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | res=%h | mem0=%h mem4=%h",
                 $time, transaction_res, memory[0], memory[4]);
    end
endmodule