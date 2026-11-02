`timescale 1ns/1ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.52.0.0
// Standard Target: IEEE Std 1800-2023 §20.9 (Bit Count Function $countbits)
// Design Module: Bus Integrity & 4-State Health Monitor
// ============================================================================

package v3_52_0_0_pkg;
    typedef struct packed {
        logic [7:0] payload;
        logic [3:0] ecc;
        logic       valid;
    } bus_word_s; // 13 bits total
endpackage

module v3_52_0_0_bus_monitor (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [12:0] raw_bus,
    output logic [4:0]  known_bits_count,
    output logic [3:0]  unknown_bits_count,
    output logic [4:0]  ones_count,
    output logic        bus_fully_known
);
    import v3_52_0_0_pkg::*;

    bus_word_s sampled_bus;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            sampled_bus        <= '0;
            known_bits_count   <= 5'd0;
            unknown_bits_count <= 4'd0;
            ones_count         <= 5'd0;
            bus_fully_known    <= 1'b0;
        end else begin
            sampled_bus <= bus_word_s'(raw_bus);

            // 1. $countbits: count bits matching '0 or '1 (known bits)
            known_bits_count <= 5'($countbits(raw_bus, '0, '1));

            // 2. $countbits: count bits matching 'x or 'z (unknown bits)
            unknown_bits_count <= 4'($countbits(raw_bus, 'x, 'z));

            // 3. $countbits: count bits matching '1 (equivalent to $countones)
            ones_count <= 5'($countbits(raw_bus, '1));

            // 4. Assert integrity if no bits are 'x or 'z
            bus_fully_known <= ($countbits(raw_bus, 'x, 'z) == 0);
        end
    end

endmodule