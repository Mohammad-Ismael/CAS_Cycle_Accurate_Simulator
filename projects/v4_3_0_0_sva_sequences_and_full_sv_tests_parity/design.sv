`timescale 1ns/1ps

module arbiter_pipeline_core (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        req,
    output logic        gnt0,
    output logic        gnt1,
    output logic        active,
    output logic [7:0]  status
);

    int cycle_cnt;
    logic req_d1;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            cycle_cnt <= 0;
            gnt0      <= 1'b0;
            gnt1      <= 1'b0;
            active    <= 1'b0;
            status    <= 8'h00;
            req_d1    <= 1'b0;
        end else begin
            req_d1 <= req;
            if (req && !req_d1) begin
                cycle_cnt <= 1;
                active    <= 1'b1;
                gnt0      <= 1'b0;
                gnt1      <= 1'b0;
                status    <= 8'h01;
            end else if (active) begin
                cycle_cnt <= cycle_cnt + 1;
                if (cycle_cnt == 1) begin
                    gnt0   <= 1'b1;
                    status <= 8'h0A;
                end else if (cycle_cnt == 2) begin
                    gnt0   <= 1'b0;
                    gnt1   <= 1'b1;
                    status <= 8'h0B;
                end else begin
                    gnt0   <= 1'b0;
                    gnt1   <= 1'b0;
                    active <= 1'b0;
                    status <= 8'h00;
                end
            end else begin
                gnt0   <= 1'b0;
                gnt1   <= 1'b0;
                status <= 8'h00;
            end
        end
    end

`ifndef VERILATOR
    // SVA sequence delay range
    sequence seq_grant_range;
        @(posedge clk) req ##[1:4] (gnt0 || gnt1);
    endsequence
    asrt_range: assert property (seq_grant_range);

    // SVA sequence binary AND
    sequence seq_and_ops;
        @(posedge clk) (req ##1 active) and (req ##[1:2] gnt0);
    endsequence
    asrt_and: assert property (seq_and_ops);

    // SVA sequence binary OR
    sequence seq_or_ops;
        @(posedge clk) (req ##1 active) or (req ##2 gnt1);
    endsequence
    asrt_or: assert property (seq_or_ops);

    // SVA sequence INTERSECT
    sequence seq_intersect_ops;
        @(posedge clk) (req ##1 active) intersect (req ##[1:3] gnt0);
    endsequence
    asrt_intersect: assert property (seq_intersect_ops);

    // SVA sequence THROUGHOUT
    property prop_throughout;
        @(posedge clk) disable iff (!rst_n)
        active throughout (req ##1 gnt0);
    endproperty
    asrt_throughout: assert property (prop_throughout);

    // Sampled Value Functions: $past, $rose, $fell, $changed, $stable
    property prop_sampled_funcs;
        @(posedge clk) disable iff (!rst_n)
        $rose(req) |=> ($past(req) == 1'b1) && $changed(status);
    endproperty
    asrt_sampled: assert property (prop_sampled_funcs);
`endif

endmodule
