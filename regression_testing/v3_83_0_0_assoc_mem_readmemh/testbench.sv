`timescale 1ns/1ps

//==============================================================================
// Testbench: tb_v3_83_0_regression
// Standard: IEEE 1800-2023 SystemVerilog Sparse Associative Array $readmemh
// Description:
//   Verification of associative array initialization via $readmemh with
//   non-contiguous address gaps (@0000, @0004, @0010, @0020).
//==============================================================================

module tb_v3_83_0_regression;

    logic        clk;
    logic        rst_n;
    logic        req;
    logic [31:0] addr;
    logic [31:0] rdata;
    logic        hit;

    integer errors = 0;

    v3_83_0_0_assoc_mem_readmemh dut (
        .clk(clk),
        .rst_n(rst_n),
        .req(req),
        .addr(addr),
        .rdata(rdata),
        .hit(hit)
    );

    // Clock generator: 10 ns period
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    // Verification task
    task check_access(input [31:0] target_addr, input [31:0] exp_data, input exp_hit);
        begin
            @(posedge clk);
            addr <= target_addr;
            req  <= 1'b1;
            @(posedge clk);
            req  <= 1'b0;
            #1;
            if (hit !== exp_hit) begin
                $display("[FAIL T=%0t] Addr 0x%08x: expected hit=%b, got hit=%b", $time, target_addr, exp_hit, hit);
                errors++;
            end else if (exp_hit && rdata !== exp_data) begin
                $display("[FAIL T=%0t] Addr 0x%08x: expected rdata=0x%08x, got 0x%08x", $time, target_addr, exp_data, rdata);
                errors++;
            end else begin
                $display("[PASS T=%0t] Addr 0x%08x -> data=0x%08x hit=%b", $time, target_addr, rdata, hit);
            end
        end
    endtask

    initial begin
        rst_n = 0;
        req   = 0;
        addr  = 0;
        #15;
        rst_n = 1;
        #10;

        // Verify loaded sparse addresses
        check_access(32'h0000_0000, 32'h1122_3344, 1'b1);
        check_access(32'h0000_0001, 32'h0000_0000, 1'b0); // Unmapped gap
        check_access(32'h0000_0004, 32'h5566_7788, 1'b1);
        check_access(32'h0000_0008, 32'h0000_0000, 1'b0); // Unmapped gap
        check_access(32'h0000_0010, 32'hAABB_CCDD, 1'b1);
        check_access(32'h0000_0018, 32'h0000_0000, 1'b0); // Unmapped gap
        check_access(32'h0000_0020, 32'hDEAD_BEEF, 1'b1);
        check_access(32'h0000_0030, 32'h0000_0000, 1'b0); // Unmapped gap

        #10;
        if (errors == 0) begin
            $display("\n================================================================================");
            $display("[CAS v3.83.0.0 REGRESSION SUITE: ASSOC MEM $READMEMH §21.4]");
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 Sparse Associative Array $readmemh with gaps.");
            $display("================================================================================\n");
        end else begin
            $display("\n[FATAL] v3_83_0_0_assoc_mem_readmemh failed with %0d errors!\n", errors);
        end

        $finish;
    end

endmodule
