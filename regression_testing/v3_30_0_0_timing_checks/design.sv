// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.30.0.0
// Standard Target: IEEE Std 1800-2023 §31.8 & §32.7 (System Timing Checks)
// Design Module: D-Type Flip-Flop with $setup, $hold, and$width Timing Checks
// ============================================================================

module v3_30_0_0_dff_timed (
    input  wire clk,
    input  wire rst_n,
    input  wire d,
    output reg  q
);
    // Notifier initialized to 0 per standard-cell library convention
    reg notifier = 1'b0;

    // Normal register behavioral update
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            q <= 1'b0;
        end else begin
            q <= d;
        end
    end

    // Notifier transition corrupts output upon $setup / $hold violation
    always @(notifier) begin
        q <= 1'bx;
    end

    // IEEE Std 1800-2023 §31.8 & §32.7: Timing Checks inside Specify Block
    specify
        // Minimum clock high/low pulse width: 10 time units
        $width(posedge clk, 10, 0, notifier);

        // Setup check: 'd' must be stable 5 time units before posedge clk
        $setup(d, posedge clk, 5, notifier);

        // Hold check: 'd' must remain stable 3 time units after posedge clk
        $hold(posedge clk, d, 3, notifier);
    endspecify

endmodule