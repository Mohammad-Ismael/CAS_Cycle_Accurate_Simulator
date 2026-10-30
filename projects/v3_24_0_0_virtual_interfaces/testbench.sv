// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.24.0.0
// Standard Target: IEEE Std 1800-2023 §25.9 (Virtual Interfaces in OOP Classes)
// Testbench Name : tb_v3_24_0_0_regression
// ============================================================================

// ----------------------------------------------------------------------------
// OOP Driver Class containing a Virtual Interface (§25.9)
// ----------------------------------------------------------------------------
class mem_driver;
    virtual mem_bus_if.drv_mp vif;
    int tx_count = 0;

    function new(virtual mem_bus_if.drv_mp target_vif);
        this.vif = target_vif;
    endfunction

    task automatic write_and_verify(input logic [15:0] a, input logic [15:0] d, output logic [15:0] rb);
        @(posedge vif.clk);
        vif.req   <= 1'b1;
        vif.addr  <= a;
        vif.wdata <= d;

        @(posedge vif.clk);
        vif.req   <= 1'b0;
        vif.addr  <= 16'h0000;
        vif.wdata <= 16'h0000;

        #1; // Allow NBA updates from this clock edge to settle
        rb = vif.rdata;
        tx_count++;
    endtask
endclass

module tb_v3_24_0_0_regression;
    logic clk;

    // Physical Interface Instance
    mem_bus_if intf (clk);

    // Design Core Instance
    v3_24_0_0_mem_core dut (intf.dut_mp);

    mem_driver drv;
    logic [15:0] rdata_captured;
    int pass_count  = 0;
    int error_count = 0;

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        intf.rst_n = 0;
        intf.req   = 0;
        intf.addr  = 0;
        intf.wdata = 0;

        // Instantiate OOP Driver passing physical interface modport (§25.9.1)
        drv = new(intf.drv_mp);

        #12;
        intf.rst_n = 1;

        // --------------------------------------------------------------------
        // Transaction 1 via Virtual Interface: addr=1, wdata=16'h1000
        // Expected rdata = 16'h1005
        // --------------------------------------------------------------------
        drv.write_and_verify(16'h0001, 16'h1000, rdata_captured);

        if (rdata_captured !== 16'h1005) begin
            $display("[FAIL T=%0t] Tx1 rdata mismatch! got %h (exp 1005)", $time, rdata_captured);
            error_count++;
        end else begin
            pass_count++;
        end

        // --------------------------------------------------------------------
        // Transaction 2 via Virtual Interface: addr=2, wdata=16'h2000
        // Expected rdata = 16'h2005
        // --------------------------------------------------------------------
        drv.write_and_verify(16'h0002, 16'h2000, rdata_captured);

        if (rdata_captured !== 16'h2005) begin
            $display("[FAIL T=%0t] Tx2 rdata mismatch! got %h (exp 2005)", $time, rdata_captured);
            error_count++;
        end else begin
            pass_count++;
        end

        #5;
        $display("\n================================================================================");
        $display("[CAS v3.24.0.0 REGRESSION SUITE: VIRTUAL INTERFACES IN OOP]");
        if (error_count == 0 && pass_count == 2 && drv.tx_count == 2) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §25.9 virtual interfaces in OOP classes.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 2, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | rst_n=%b | req=%b addr=%h wdata=%h | rdy=%b rdata=%h",
                 $time, intf.rst_n, intf.req, intf.addr, intf.wdata, intf.ready, intf.rdata);
    end
endmodule