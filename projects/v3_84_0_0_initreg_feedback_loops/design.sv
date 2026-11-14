`timescale 1ns/1ps

//==============================================================================
// Module: v3_84_0_0_initreg_feedback_loops
// Standard: IEEE 1800-2023 SystemVerilog
// Description:
//   Self-gated feedback register loop (rvdffie pattern).
//   en = |(din ^ dout)
//   Without register initialization (X-state), en resolves to X/lockup.
//   With +initreg+0, dout initializes to 0 and updates cleanly on din change.
//==============================================================================

module rvdffie_param #(parameter WIDTH = 1) (
    input  logic             clk,
    input  logic [WIDTH-1:0] din,
    output logic [WIDTH-1:0] dout
);
    logic en;
    assign en = |(din ^ dout);

    always_ff @(posedge clk) begin
        if (en) begin
            dout <= din;
        end
    end
endmodule

module v3_84_0_0_initreg_feedback_loops (
    input  logic         clk,
    input  logic         din_1,
    output logic         dout_1,
    input  logic [31:0]  din_32,
    output logic [31:0]  dout_32,
    input  logic [141:0] din_142,
    output logic [141:0] dout_142
);

    rvdffie_param #(1)   u_ff_1   (.clk(clk), .din(din_1),   .dout(dout_1));
    rvdffie_param #(32)  u_ff_32  (.clk(clk), .din(din_32),  .dout(dout_32));
    rvdffie_param #(142) u_ff_142 (.clk(clk), .din(din_142), .dout(dout_142));

endmodule
