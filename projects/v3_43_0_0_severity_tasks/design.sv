`timescale 1ns/1ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.43.0.0
// Standard Target: IEEE Std 1800-2023 §20.10 (Severity System Tasks)
// Design Module: Protocol Watchdog and Boundary Guard
// ============================================================================

module v3_43_0_0_protocol_guard (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  pkt_len,
    input  logic        pkt_valid,
    output logic        guard_ok,
    output logic [1:0]  guard_status // 0: IDLE, 1: OK, 2: WARN, 3: ERR
);

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            guard_ok     <= 1'b0;
            guard_status <= 2'd0;
        end else if (pkt_valid) begin
            if (pkt_len == 8'd0) begin
                guard_ok     <= 1'b0;
                guard_status <= 2'd3;
                $error("Protocol Error: Received zero-length packet payload at time %0t", $time);
            end else if (pkt_len > 8'd64) begin
                guard_ok     <= 1'b1;
                guard_status <= 2'd2;
                $warning("Protocol Warning: Packet length %0d exceeds standard MTU (64 bytes) at time %0t",
                         pkt_len, $time);
            end else begin
                guard_ok     <= 1'b1;
                guard_status <= 2'd1;
                $info("Protocol Info: Valid frame received (len=%0d) at time %0t", pkt_len, $time);
            end
        end else begin
            guard_ok     <= 1'b0;
            guard_status <= 2'd0;
        end
    end

endmodule