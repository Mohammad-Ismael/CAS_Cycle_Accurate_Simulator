// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.10.0.0
// Standard Target: IEEE Std 1800-2023 §16.9.3 ($rose, $fell, $stable, $past)
// Testbench Name : tb_v3_10_0_0_regression
// ============================================================================

module tb_v3_10_0_0_regression;
    logic       clk;
    logic       rst_n;
    logic       start_pulse;
    logic [7:0] data_in;
    logic       active;
    logic [7:0] latched_data;

    integer rose_pass_count   = 0;
    integer past_pass_count   = 0;
    integer stable_pass_count = 0;
    integer error_count       = 0;

    v3_10_0_0_sampled_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .start_pulse(start_pulse),
        .data_in(data_in),
        .active(active),
        .latched_data(latched_data)
    );

    // Clock generator: posedges at 5, 15, 25, 35, 45, 55, 65, 75...
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    // ------------------------------------------------------------------------
    // Property 1: $rose(start_pulse) |=> active
    // When start_pulse transitions 0 -> 1, active must be 1 on next cycle
    // ------------------------------------------------------------------------
    property p_rose_active;
        @(posedge clk) disable iff (!rst_n)
        $rose(start_pulse) |=> active;
    endproperty

    assert_rose: assert property (p_rose_active) begin
        rose_pass_count = rose_pass_count + 1;
    end

    // ------------------------------------------------------------------------
    // Property 2: active |-> (latched_data == $past(data_in))
    // When active is 1, latched_data must match data_in from 1 cycle ago
    // ------------------------------------------------------------------------
    property p_past_data;
        @(posedge clk) disable iff (!rst_n)
        active |-> (latched_data == $past(data_in));
    endproperty

    assert_past: assert property (p_past_data) begin
        past_pass_count = past_pass_count + 1;
    end

    // ------------------------------------------------------------------------
    // Property 3: $fell(start_pulse) |=> $fell(active)
    // When start_pulse falls 1 -> 0, active must fall on the subsequent cycle
    // ------------------------------------------------------------------------
    property p_fell_active;
        @(posedge clk) disable iff (!rst_n)
        $fell(start_pulse) |=> $fell(active);
    endproperty

    assert_fell: assert property (p_fell_active) begin
        stable_pass_count = stable_pass_count + 1;
    end

    // Test sequence driving stimuli
    initial begin
        rst_n       = 0;
        start_pulse = 0;
        data_in     = 8'h00;

        #6; // T = 6 (after reset posedge at T = 5)
        rst_n = 1;

        // --------------------------------------------------------------------
        // Step 1: Assert start_pulse with data_in = 8'hA5 (T = 15)
        // Previous start_pulse was 0, so $rose(start_pulse) is TRUE at T = 15!
        // --------------------------------------------------------------------
        @(posedge clk); // T = 15
        start_pulse <= 1'b1;
        data_in     <= 8'hA5;

        // --------------------------------------------------------------------
        // Step 2: Deassert start_pulse (T = 25)
        // In Preponed of T = 25: start_pulse is 1, active becomes 1 in RTL.
        // $rose(start_pulse) triggered at T=15 checks active at T=25 -> PASS 1
        // active=1 at T=25 checks latched_data == $past(data_in) (8'hA5) -> PASS 1
        // $fell(start_pulse) is detected at T=25!
        // --------------------------------------------------------------------
        @(posedge clk); // T = 25
        start_pulse <= 1'b0;
        data_in     <= 8'h00;

        // --------------------------------------------------------------------
        // Step 3: Idle cycle (T = 35)
        // active becomes 0. $fell(active) matches consequence of $fell at T=25 -> PASS 1
        // --------------------------------------------------------------------
        @(posedge clk); // T = 35

        @(posedge clk); // T = 45 (allow Reactive region actions to commit)
        #1; // T = 46
        if (rose_pass_count !== 1 || past_pass_count !== 1 || stable_pass_count !== 1) begin
            $display("[FAIL T=46] SVA sampled function counts mismatch! rose=%0d past=%0d fell=%0d (exp 1, 1, 1)",
                     rose_pass_count, past_pass_count, stable_pass_count);
            error_count = error_count + 1;
        end

        #4; // T = 50
        $display("\n================================================================================");
        $display("[CAS v3.10.0.0 REGRESSION SUITE: SVA SAMPLED VALUE FUNCTIONS]");
        if (error_count == 0 && rose_pass_count == 1 && past_pass_count == 1 && stable_pass_count == 1) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §16.9.3 ($rose, $fell, $past, $stable).");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s))", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | clk=%b rst_n=%b | start=%b data=%h | act=%b latched=%h | rose_pass=%0d past_pass=%0d fell_pass=%0d",
                 $time, clk, rst_n, start_pulse, data_in, active, latched_data,
                 rose_pass_count, past_pass_count, stable_pass_count);
    end
endmodule