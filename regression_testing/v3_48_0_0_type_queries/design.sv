`timescale 1ns/1ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.48.0.0
// Standard Target: IEEE Std 1800-2023 §20.6 / §6.23 ($typename Type Queries)
// Design Module: Type Introspection and Schema Validator
// ============================================================================

package v3_48_0_0_pkg;

    typedef enum logic [1:0] {
        ST_IDLE = 2'b00,
        ST_BUSY = 2'b01,
        ST_DONE = 2'b10
    } state_e;

    typedef struct packed {
        logic [7:0]  src;
        logic [7:0]  dst;
        logic [15:0] len;
    } frame_hdr_s;

    class packet_c;
        int id;
        function new(int id);
            this.id = id;
        endfunction
    endclass

endpackage

module v3_48_0_0_type_introspector (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  byte_in,
    output logic        valid_schema
);
    import v3_48_0_0_pkg::*;

    state_e     curr_state;
    frame_hdr_s hdr_reg;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            valid_schema <= 1'b0;
        end else begin
            // Format conforms to Synopsys VCS formatting:
            // - Scoped UDTs include category prefix ('enum ', 'struct ', 'class ')
            // - Vector dimensions are compact ('logic[7:0]')
            if ($typename(curr_state) == "enum v3_48_0_0_pkg::state_e" &&
                $typename(byte_in)    == "logic[7:0]") begin
                valid_schema <= 1'b1;
            end else begin
                valid_schema <= 1'b0;
            end
        end
    end

endmodule