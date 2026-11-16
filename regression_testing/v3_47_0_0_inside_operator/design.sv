`timescale 1ns/1ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.47.0.0
// Standard Target: IEEE Std 1800-2023 §11.4.13 (Set Membership 'inside' Operator)
// Design Module: Packet Filter and Range Classifier using 'inside'
// ============================================================================

package v3_47_0_0_pkg;
    typedef enum logic [3:0] {
        PORT_CONTROL = 4'h0,
        PORT_DATA_LO = 4'h1,
        PORT_DATA_HI = 4'h2,
        PORT_MGMT    = 4'hF
    } port_id_e;
endpackage

module v3_47_0_0_packet_filter (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  pkt_addr,
    input  logic [3:0]  port_tag,
    input  logic        pkt_valid,
    output logic        in_reserved_range,
    output logic        valid_port,
    output logic [1:0]  filter_category // 0: DROP, 1: CONTROL, 2: STREAM
);
    import v3_47_0_0_pkg::*;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            in_reserved_range <= 1'b0;
            valid_port        <= 1'b0;
            filter_category   <= 2'd0;
        end else if (pkt_valid) begin
            // 1. Range test with open ranges [low:high]
            in_reserved_range <= (pkt_addr inside { [8'd100 : 8'd150], [8'd200 : 8'd220] });

            // 2. Set membership against individual discrete values
            valid_port <= (port_tag inside { PORT_CONTROL, PORT_DATA_LO, PORT_DATA_HI, PORT_MGMT });

            // 3. Mixed ranges and discrete values in decision predicate
            if (pkt_addr inside { 8'd0, [8'd10 : 8'd20] }) begin
                filter_category <= 2'd1; // CONTROL
            end else if (pkt_addr inside { [8'd30 : 8'd80], 8'd90 }) begin
                filter_category <= 2'd2; // STREAM
            end else begin
                filter_category <= 2'd0; // DROP
            end
        end else begin
            in_reserved_range <= 1'b0;
            valid_port        <= 1'b0;
            filter_category   <= 2'd0;
        end
    end

endmodule