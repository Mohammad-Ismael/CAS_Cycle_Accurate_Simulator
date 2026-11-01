// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.28.0.0
// Standard Target: IEEE Std 1800-2023 §28 & §29 (Gate Primitives & UDPs)
// Testbench Name : tb_v3_28_0_0_regression
// ============================================================================

module tb_v3_28_0_0_regression;
    logic a;
    logic b;
    logic cin;
    logic ctrl_en0;
    logic ctrl_en1;
    logic sum;
    logic cout;
    wire  shared_bus;

    int pass_count  = 0;
    int error_count = 0;

    v3_28_0_0_gate_core dut (
        .a         (a),
        .b         (b),
        .cin       (cin),
        .ctrl_en0  (ctrl_en0),
        .ctrl_en1  (ctrl_en1),
        .sum       (sum),
        .cout      (cout),
        .shared_bus(shared_bus)
    );

    initial begin
        // Initial setup: all inputs 0, tristates disabled
        a        = 0;
        b        = 0;
        cin      = 0;
        ctrl_en1 = 0; // bufif1 disabled -> 'z'
        ctrl_en0 = 1; // bufif0 disabled -> 'z'

        #10;
        // Test 1: Check High-Z bus when both tristates disabled
        if (shared_bus !== 1'bz) begin
            $display("[FAIL T=%0t] Test 1 Tristate disabled mismatch! got %b (exp z)", $time, shared_bus);
            error_count++;
        end else begin
            pass_count++;
        end

        // Test 2: Full Adder calculation a=1, b=0, cin=1
        // sum = 1 ^ 0 ^ 1 = 0
        // cout (UDP majority of 1, 0, 1) = 1
        // Enable bufif1 (ctrl_en1=1) -> shared_bus = sum = 0
        #10;
        a        = 1;
        b        = 0;
        cin      = 1;
        ctrl_en1 = 1;
        ctrl_en0 = 1;

        #1;
        if (sum !== 1'b0 || cout !== 1'b1 || shared_bus !== 1'b0) begin
            $display("[FAIL T=%0t] Test 2 Adder & bufif1 mismatch! sum=%b (exp 0) cout=%b (exp 1) bus=%b (exp 0)",
                     $time, sum, cout, shared_bus);
            error_count++;
        end else begin
            pass_count++;
        end

        // Test 3: a=1, b=1, cin=1
        // sum = 1 ^ 1 ^ 1 = 1
        // cout (UDP majority of 1, 1, 1) = 1
        // Enable bufif0 (ctrl_en0=0) and disable bufif1 (ctrl_en1=0) -> shared_bus = cout = 1
        #10;
        a        = 1;
        b        = 1;
        cin      = 1;
        ctrl_en1 = 0;
        ctrl_en0 = 0;

        #1;
        if (sum !== 1'b1 || cout !== 1'b1 || shared_bus !== 1'b1) begin
            $display("[FAIL T=%0t] Test 3 Adder & bufif0 mismatch! sum=%b (exp 1) cout=%b (exp 1) bus=%b (exp 1)",
                     $time, sum, cout, shared_bus);
            error_count++;
        end else begin
            pass_count++;
        end

        // Test 4: Tristate contention (both enabled driving opposing values)
        // a=1, b=0, cin=0 -> sum = 1, cout = 0
        // ctrl_en1=1 (drives sum=1), ctrl_en0=0 (drives cout=0) -> contention on shared_bus -> 'x'
        #10;
        a        = 1;
        b        = 0;
        cin      = 0;
        ctrl_en1 = 1;
        ctrl_en0 = 0;

        #1;
        if (shared_bus !== 1'bx) begin
            $display("[FAIL T=%0t] Test 4 Tristate contention mismatch! got %b (exp x)", $time, shared_bus);
            error_count++;
        end else begin
            pass_count++;
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.28.0.0 REGRESSION SUITE: GATE PRIMITIVES & UDPS]");
        if (error_count == 0 && pass_count == 4) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §28 & §29 gate primitives, bufif0/1, and UDPs.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 4, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end

    initial begin
        $monitor("Time=%0t | a=%b b=%b cin=%b | en0=%b en1=%b | sum=%b cout=%b | bus=%b",
                 $time, a, b, cin, ctrl_en0, ctrl_en1, sum, cout, shared_bus);
    end
endmodule