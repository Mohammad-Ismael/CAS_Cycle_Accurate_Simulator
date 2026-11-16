// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v2.5.0.0
// Standard Target: IEEE Std 1800-2023 §6.19 (Enumerated Types & Methods)
// Testbench Name : tb_v2_5_0_0_regression
// ============================================================================

module tb_v2_5_0_0_regression;
    logic       clk;
    logic       rst_n;
    logic [1:0] cmd_in;
    logic [1:0] state_out;

    typedef enum logic [1:0] {
        ST_IDLE  = 2'b00,
        ST_READ  = 2'b01,
        ST_WRITE = 2'b10,
        ST_ERROR = 2'b11
    } state_t;

    state_t tb_state;
    integer error_count = 0;

    v2_5_0_0_fsm_core dut (
        .clk(clk),
        .rst_n(rst_n),
        .cmd_in(cmd_in),
        .state_out(state_out)
    );

    // Clock generator: posedges at 5, 15, 25, 35, 45, 55...
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n    = 0;
        cmd_in   = 2'b00;
        tb_state = ST_IDLE;

        // Reset check
        #6; // T = 6 (after posedge at T = 5)
        rst_n = 1;
        if (state_out !== ST_IDLE) begin
            $display("[FAIL T=6] FSM reset failed! state=%0d (exp ST_IDLE)", state_out);
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 1: Transition to ST_READ
        // --------------------------------------------------------------------
        #4; // T = 10
        cmd_in = 2'b01;

        #5; // T = 15 (posedge clk) -> FSM updates to ST_READ
        #1; // T = 16
        tb_state = state_t'(state_out);
        if (state_out !== ST_READ || tb_state.name() != "ST_READ") begin
            $display("[FAIL T=16] Transition to ST_READ failed! state=%0d name=%s", state_out, tb_state.name());
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 2: Auto-return to ST_IDLE
        // --------------------------------------------------------------------
        cmd_in = 2'b00;
        #9; // T = 25 (posedge clk) -> FSM returns to ST_IDLE
        #1; // T = 26
        tb_state = state_t'(state_out);
        if (state_out !== ST_IDLE || tb_state.name() != "ST_IDLE") begin
            $display("[FAIL T=26] Return to ST_IDLE failed! state=%0d name=%s", state_out, tb_state.name());
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 3: Transition to ST_WRITE
        // --------------------------------------------------------------------
        cmd_in = 2'b10;
        #9; // T = 35 (posedge clk) -> FSM updates to ST_WRITE
        #1; // T = 36
        tb_state = state_t'(state_out);
        if (state_out !== ST_WRITE || tb_state.name() != "ST_WRITE") begin
            $display("[FAIL T=36] Transition to ST_WRITE failed! state=%0d name=%s", state_out, tb_state.name());
            error_count = error_count + 1;
        end

        // --------------------------------------------------------------------
        // TEST 4: Built-in enum traversal methods (§6.19.5: first, last, next, prev)
        // --------------------------------------------------------------------
        tb_state = tb_state.first(); // ST_IDLE (0)
        if (tb_state !== ST_IDLE) begin
            $display("[FAIL] first() failed! tb_state=%0d", tb_state);
            error_count = error_count + 1;
        end

        tb_state = tb_state.next(); // ST_READ (1)
        if (tb_state !== ST_READ) begin
            $display("[FAIL] next() failed! tb_state=%0d", tb_state);
            error_count = error_count + 1;
        end

        tb_state = tb_state.last(); // ST_ERROR (3)
        if (tb_state !== ST_ERROR) begin
            $display("[FAIL] last() failed! tb_state=%0d", tb_state);
            error_count = error_count + 1;
        end

        tb_state = tb_state.prev(); // ST_WRITE (2)
        if (tb_state !== ST_WRITE) begin
            $display("[FAIL] prev() failed! tb_state=%0d", tb_state);
            error_count = error_count + 1;
        end

        #4; // T = 40
        $display("\n================================================================================");
        $display("[CAS v2.5.0.0 REGRESSION SUITE: ENUMERATED TYPES AND METHODS]");
        if (error_count == 0) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §6.19 enum types and methods (first, last, next, prev, name).");
        end else begin
            $display("RESULT : >>> FAIL <<< (%0d error(s) detected)", error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | clk=%b rst_n=%b cmd_in=%b | state_out=%b (%s)",
                 $time, clk, rst_n, cmd_in, state_out, tb_state.name());
    end
endmodule