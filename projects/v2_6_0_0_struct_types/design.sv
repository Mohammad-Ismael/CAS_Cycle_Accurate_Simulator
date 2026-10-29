// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v2.6.0.0
// Standard Target: IEEE Std 1800-2023 §7.2 (Packed Structures)
// Module Name    : v2_6_0_0_struct_core
// ============================================================================

module v2_6_0_0_struct_core (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [15:0] raw_packet_in,
    output logic [7:0]  payload_out,
    output logic [3:0]  src_out,
    output logic [3:0]  dst_out,
    output logic [15:0] packet_loopback
);
    // 16-bit packed header/payload packet structure
    typedef struct packed {
        logic [3:0] src_id;
        logic [3:0] dst_id;
        logic [7:0] payload;
    } packet_t;

    packet_t rx_pkt;
    packet_t tx_pkt;

    // Direct aggregate assignment from raw 16-bit vector
    always_comb begin
        rx_pkt = raw_packet_in;
        src_out = rx_pkt.src_id;
        dst_out = rx_pkt.dst_id;
        payload_out = rx_pkt.payload;
    end

    // Sequential process modifying specific member fields
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tx_pkt <= 16'h0000;
        end else begin
            // Swap src and dst, increment payload by 1
            tx_pkt.src_id  <= rx_pkt.dst_id;
            tx_pkt.dst_id  <= rx_pkt.src_id;
            tx_pkt.payload <= rx_pkt.payload + 8'h01;
        end
    end

    // Expose aggregate packed struct as raw vector
    assign packet_loopback = tx_pkt;

endmodule