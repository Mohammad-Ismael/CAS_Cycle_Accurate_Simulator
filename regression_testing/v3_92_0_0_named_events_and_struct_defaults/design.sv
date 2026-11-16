// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.92.0.0
// Standard Target: IEEE Std 1800-2023 §7.2.2 (Struct Member Default Values)
//                  & §9.4.2 (Named Events and Event Control)
// Module Name    : v3_92_0_0_events_struct_defaults_core
// ============================================================================

module v3_92_0_0_events_struct_defaults_core (
    input  logic        clk,
    input  logic        rst_n,
    output logic [3:0]  out_s_lo,
    output logic [3:0]  out_s_hi,
    output int          out_step_count,
    output int          out_bare_count
);
    // 1. IEEE 1800 §7.2.2: Unpacked struct with member default initializer
    parameter logic [3:0] DEF_LO = 4'h5;

`ifdef VERILATOR
    // Workaround: Simulator lacks initial values in struct/union members (UNSUPPORTED)
    struct {
        bit [3:0] lo;
        bit [3:0] hi;
    } s_inst;

    initial begin
        s_inst.lo = DEF_LO;
    end
`else
    struct {
        bit [3:0] lo = DEF_LO;
        bit [3:0] hi;
    } s_inst;
`endif

    // 2. IEEE 1800 §9.4.2: Named event declarations
    event ev_step;
    event ev_bare;

    int step_count = 0;
    int bare_count = 0;

    initial begin
        out_step_count = 0;
        out_bare_count = 0;
    end

    // Process sensitive to parenthesized event @(ev_step)
    always @(ev_step) begin
        step_count = step_count + 1;
        out_step_count = step_count;
    end

    // Process sensitive to bare event @ev_bare
    always @ev_bare begin
        bare_count = bare_count + 1;
        out_bare_count = bare_count;
    end

    task trigger_step_event();
        ->ev_step;
    endtask

    task trigger_bare_event();
        ->ev_bare;
    endtask

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            s_inst.hi <= 4'h0;
        end else begin
            s_inst.hi <= s_inst.hi + 4'h1;
        end
    end

    assign out_s_lo = s_inst.lo;
    assign out_s_hi = s_inst.hi;

endmodule
