`timescale 1ns/1ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.45.0.0
// Standard Target: IEEE Std 1800-2023 §6.24.3 (Bit-Stream Casting)
// Design Module: Stream Serializer and Header Unpacker
// ============================================================================

package v3_45_0_0_pkg;
    typedef struct packed {
        logic [7:0]  src_id;
        logic [7:0]  dst_id;
        logic [15:0] payload_len;
    } header_t; // 32 bits packed

    typedef logic [7:0] byte_array_t[4]; // 32 bits unpacked array of bytes

    typedef logic [31:0] stream32_t;
endpackage

module v3_45_0_0_stream_converter (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [31:0] raw_stream,
    input  logic        stream_valid,
    output logic [7:0]  hdr_src,
    output logic [7:0]  hdr_dst,
    output logic [15:0] hdr_len,
    output logic [31:0] reassembled_stream
);
    import v3_45_0_0_pkg::*;

    header_t     current_hdr;
    byte_array_t unpacked_bytes;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            hdr_src            <= 8'h00;
            hdr_dst            <= 8'h00;
            hdr_len            <= 16'h0000;
            reassembled_stream <= 32'h0000_0000;
        end else if (stream_valid) begin
            // 1. Bit-stream cast from 32-bit vector into packed struct
            current_hdr = header_t'(raw_stream);
            hdr_src <= current_hdr.src_id;
            hdr_dst <= current_hdr.dst_id;
            hdr_len <= current_hdr.payload_len;

            // 2. Bit-stream cast from packed struct into unpacked byte array [4]
            unpacked_bytes = byte_array_t'(current_hdr);

            // 3. Bit-stream cast from unpacked byte array [4] back into 32-bit vector
            reassembled_stream <= stream32_t'(unpacked_bytes);
        end
    end

endmodule