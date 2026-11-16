// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.31.0.0
// Standard Target: IEEE Std 1800-2023 §13.5 & §13.6 (Subroutine Defaults & Named Args)
// Design Module: Configurable Transaction & Address Generator
// ============================================================================

module v3_31_0_0_addr_engine (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [31:0] base_addr,
    input  logic        trigger,
    output logic [31:0] computed_addr,
    output logic [7:0]  tag_out
);
    // IEEE Std 1800-2023 §13.5.3: Task with default arguments & mixed directions
    task automatic compute_next_slot(
        input  logic [31:0] base,
        input  int          offset = 16,
        input  int          multiplier = 2,
        input  logic [7:0]  tag = 8'hA0,
        output logic [31:0] res_addr,
        output logic [7:0]  res_tag
    );
        res_addr = base + (offset * multiplier);
        res_tag  = tag;
    endtask

    // Function with defaults and return value
    function automatic int calc_checksum(int a, int b = 5, int c = 10);
        return a + (b * 2) + (c * 4);
    endfunction

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            computed_addr <= 32'h0;
            tag_out       <= 8'h0;
        end else if (trigger) begin
            logic [31:0] tmp_addr;
            logic [7:0]  tmp_tag;

            // Named argument call with defaults applied out-of-order (§13.5.1)
            compute_next_slot(
                .base     (base_addr),
                .multiplier(4),
                .res_addr (tmp_addr),
                .res_tag  (tmp_tag)
                // offset takes default 16, tag takes default 8'hA0
            );

            computed_addr <= tmp_addr;
            tag_out       <= tmp_tag;
        end
    end

endmodule