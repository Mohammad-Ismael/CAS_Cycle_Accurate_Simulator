`timescale 1ns/1ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.57.0.0
// Standard Target: IEEE Std 1800-2023 §20.14 ($sformatf and String Formatting)
// Design Module: Transaction Log Serializer & Formatted Report Generator
// ============================================================================

package v3_57_0_0_pkg;
    typedef struct packed {
        logic [7:0]  tag;
        logic [15:0] addr;
        logic [31:0] data;
    } txn_s; // 56 bits
endpackage

module v3_57_0_0_report_generator (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  tag_in,
    input  logic [15:0] addr_in,
    input  logic [31:0] data_in,
    output logic [31:0] report_length_out,
    output logic        report_match_valid
);
    import v3_57_0_0_pkg::*;

    string last_report;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            last_report        = "";
            report_length_out  <= 32'd0;
            report_match_valid <= 1'b0;
        end else begin
            // Format transaction log string using $sformatf per §20.14
            last_report = $sformatf("TXN[tag=%02h,addr=0x%04h,data=%08h]", tag_in, addr_in, data_in);

            report_length_out <= 32'(last_report.len());

            // Validate formatted string matches expected reference
            if (last_report == "TXN[tag=fe,addr=0x1000,data=deadbeef]") begin
                report_match_valid <= 1'b1;
            end else begin
                report_match_valid <= 1'b0;
            end
        end
    end

endmodule