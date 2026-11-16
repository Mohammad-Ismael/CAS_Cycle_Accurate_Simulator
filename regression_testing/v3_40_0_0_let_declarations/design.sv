`timescale 1ns/1ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.40.0.0
// Standard Target: IEEE Std 1800-2023 §11.12 ('let' Declarations)
// Design Module: Packet Filter and Window Range Validator using 'let' Templates
// ============================================================================

package v3_40_0_0_pkg;
    // Package-level let declaration (§11.12)
    let is_even(val) = ((val & 1) == 0);
endpackage

module v3_40_0_0_packet_filter (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  data_in,
    input  logic        data_valid,
    output logic [7:0]  data_out,
    output logic        filter_pass,
    output logic        even_tag
);
    import v3_40_0_0_pkg::*;

    // Module-scoped let declaration with default arguments (§11.12.1)
    let in_window(x, min_bound = 8'd10, max_bound = 8'd100) = (x >= min_bound && x <= max_bound);

    // Typed formal argument with expression customization
    let qualify_data(logic [7:0] d, bit v) = v ? d : 8'h00;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            data_out    <= 8'h00;
            filter_pass <= 1'b0;
            even_tag    <= 1'b0;
        end else begin
            data_out    <= qualify_data(data_in, data_valid);
            filter_pass <= data_valid && in_window(data_in);
            even_tag    <= is_even(data_in);
        end
    end

endmodule