// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.35.0.0
// Standard Target: IEEE Std 1800-2023 §11.4.14 (Streaming Operators)
// Design Module: Multi-Format Frame Serializer & Endian Converter
// ============================================================================

module v3_35_0_0_stream_engine (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        trigger,
    input  logic [31:0] word_in,
    output logic [31:0] byte_reversed_word,
    output logic [31:0] bit_reversed_word,
    output logic [7:0]  unpacked_bytes [0:3],
    output logic        valid_out
);
    // IEEE Std 1800-2023 §11.4.14: Streaming unpack and slice reordering
    always_comb begin
        // 1. Byte-level endian reversal: stream right-to-left in 8-bit slices
        byte_reversed_word = { << 8 { word_in } };

        // 2. Full bit-level reversal: stream right-to-left in 1-bit slices
        bit_reversed_word  = { << { word_in } };

        // 3. Unpack 32-bit scalar into unpacked byte array: left-to-right stream
        { >> { unpacked_bytes } } = word_in;
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            valid_out <= 1'b0;
        end else if (trigger) begin
            logic [31:0] reassembled_word;
            // Pack unpacked byte array back into a 32-bit scalar
            reassembled_word = { >> { unpacked_bytes } };

            // Verify loopback and slice reversal integrity
            valid_out <= (reassembled_word == word_in) &&
                         (byte_reversed_word == { word_in[7:0], word_in[15:8], word_in[23:16], word_in[31:24] });
        end else begin
            valid_out <= 1'b0;
        end
    end

endmodule