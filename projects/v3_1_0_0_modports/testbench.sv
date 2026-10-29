// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.1.0.0
// Standard Target: IEEE Std 1800-2023 §25.5.3 (Interface Modports)
// Testbench Name : tb_v3_1_0_0_regression
// ============================================================================

module tb_v3_1_0_0_regression;
    logic clk;
    logic rst_n;
    logic send_req;
    logic [7:0] send_data;
    logic [7:0] received_data;
    logic data_valid;
    integer error_count = 0;

    // Interface instance
    simple_bus_if bus (.clk(clk));

    // Connect modules through modport views
    bus_producer u_producer (
        .bus(bus.master),
        .rst_n(rst_n),
        .send_req(send_req),
        .send_data(send_data)
    );

    bus_consumer u_consumer (
        .bus(bus.slave),
        .rst_n(rst_n),
        .received_data(received_data),
        .data_valid(data_valid)
    );

    // Clock generator: posedges at 5, 15, 25, 35, 45...
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n     = 0;
        send_req  = 0;
        send_data = 8'h00;

        #6; // T = 6 (after reset posedge at T = 5)
        rst_n = 1;
        if (bus.valid !== 1'b0 || bus.ready !== 1'b1 || bus.data !== 8'h00) begin
            $display("[FAIL T=6] Modport interface reset failed! valid=%b ready=%b data=%h",
                     bus.valid, bus.ready, bus.data);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 1: Producer transmits 8'h5C via master modport
        // --------------------------------------------------------------------
        #4; // T = 10
        send_req  = 1;
        send_data = 8'h5C;

        #5; // T = 15 (posedge clk) -> producer asserts valid=1, data=5C
        #1; // T = 16
        send_req = 0;
        if (bus.valid !== 1'b1 || bus.data !== 8'h5C) begin
            $display("[FAIL T=16] Master modport drive failed! valid=%b data=%h (exp 1, 5C)",
                     bus.valid, bus.data);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Consumer captures transaction via slave modport
        // --------------------------------------------------------------------
        #9; // T = 25 (posedge clk) -> consumer samples valid & ready
        #1; // T = 26
        if (received_data !== 8'h5C || data_valid !== 1'b1 || bus.valid !== 1'b0) begin
            $display("[FAIL T=26] Slave modport capture failed! rx=%h valid=%b (exp 5C, 1)",
                     received_data, data_valid);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 3: Second transaction (8'hE8)
        // --------------------------------------------------------------------
        #4; // T = 30
        send_req  = 1;
        send_data = 8'hE8;

        #5; // T = 35 (posedge clk)
        #1; // T = 36
        send_req = 0;
        if (bus.valid !== 1'b1 || bus.data !== 8'hE8) begin
            $display("[FAIL T=36] Second transaction drive failed! valid=%b data=%h",
                     bus.valid, bus.data);
            error_count = error_count + 1;
        end

        #9; // T = 45 (posedge clk)
        #1; // T = 46
        if (received_data !== 8'hE8 || data_valid !== 1'b1) begin
            $display("[FAIL T=46] Second transaction capture failed! rx=%h valid=%b",
                     received_data, data_valid);
            error_count = error_count + 1;
        end

        #4; // T = 50
        $display("\n================================================================================");
        $display("[CAS v3.1.0.0 REGRESSION SUITE: INTERFACE MODPORTS]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §25.5.3 interface modports (master/slave).");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | clk=%b rst_n=%b | b_valid=%b b_ready=%b b_data=%h | rx_data=%h rx_valid=%b",
                 $time, clk, rst_n, bus.valid, bus.ready, bus.data, received_data, data_valid);
    end
endmodule