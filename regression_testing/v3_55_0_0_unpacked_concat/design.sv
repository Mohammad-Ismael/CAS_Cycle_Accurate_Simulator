`timescale 1ns/1ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.55.0.0
// Standard Target: IEEE Std 1800-2023 §10.10 (Unpacked Array Concatenation)
// Design Module: Dynamic Packet Assembler & Frame Builder
// ============================================================================

package v3_55_0_0_pkg;
    typedef logic [7:0] byte_t;
    typedef byte_t      byte_dyn_t[];
endpackage

module v3_55_0_0_frame_builder (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  hdr_byte,
    input  logic [7:0]  payload_byte,
    output logic [15:0] frame_size_out,
    output logic [7:0]  first_byte_out,
    output logic [7:0]  last_byte_out,
    output logic        assembly_valid
);
    import v3_55_0_0_pkg::*;

    byte_dyn_t working_frame;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            working_frame   = new[0];
            frame_size_out  <= 16'd0;
            first_byte_out  <= 8'h00;
            last_byte_out   <= 8'h00;
            assembly_valid  <= 1'b0;
        end else begin
            // Assemble frame using unpacked array concatenation (§10.10):
            // 1 header byte + 2 preamble bytes + 1 payload byte = 4 bytes total
            working_frame = {hdr_byte, 8'h55, 8'h55, payload_byte};

            frame_size_out <= 16'(working_frame.size());
            if (working_frame.size() >= 4) begin
                first_byte_out <= working_frame[0];
                last_byte_out  <= working_frame[working_frame.size() - 1];
                assembly_valid <= (working_frame[1] == 8'h55 && working_frame[2] == 8'h55);
            end else begin
                assembly_valid <= 1'b0;
            end
        end
    end

endmodule