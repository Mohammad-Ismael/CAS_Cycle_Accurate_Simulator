// ============================================================================
// CAS Milestone Regression Design
// Target Release : v3.80.0.0
// Standard Target: IEEE Std 1800-2023 §7.2 (Packed Struct Member Slicing)
// Design Name    : v3_80_0_0_struct_member_slice
// ============================================================================

package v3_80_0_0_pkg;
    typedef struct packed {
        logic [15:0] header;
        logic [31:0] payload;
        logic [7:0]  crc;
    } packet_t;
endpackage

module v3_80_0_0_struct_member_slice (
    input  logic clk,
    input  logic rst_n,
    input  v3_80_0_0_pkg::packet_t pkt_in,
    output logic [7:0]  payload_byte0_comb,
    output logic [7:0]  payload_byte1_comb,
    output logic [15:0] payload_word_seq
);
    import v3_80_0_0_pkg::*;

    // Continuous assignment slicing packed struct member
    assign payload_byte0_comb = pkt_in.payload[7:0];
    assign payload_byte1_comb = pkt_in.payload[15:8];

    // Procedural assignment slicing packed struct member
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            payload_word_seq <= 16'h0;
        end else begin
            payload_word_seq <= pkt_in.payload[23:8];
        end
    end
endmodule
