`timescale 1ns/1ps

// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.41.0.0
// Standard Target: IEEE Std 1800-2023 §8.24 (Out-of-Body Method Declarations)
// Design Module: Protocol Formatter with Extern Class Methods
// ============================================================================

package v3_41_0_0_pkg;

    class base_formatter;
        int stream_id;
        int trans_count;

        extern function new(int id);
        extern virtual function int format_header(int raw_tag);
        extern virtual task transmit_frame(input int payload, output int frame_out);
    endclass

    // Out-of-body constructor
    function base_formatter::new(int id);
        this.stream_id   = id;
        this.trans_count = 0;
    endfunction

    // Out-of-body virtual function
    function int base_formatter::format_header(int raw_tag);
        return (raw_tag + this.stream_id);
    endfunction

    // Out-of-body virtual task
    task base_formatter::transmit_frame(input int payload, output int frame_out);
        frame_out = format_header(payload) ^ 32'hF0F0_0000;
        this.trans_count++;
    endtask

    // Derived class extending base_formatter with overridden extern method
    class custom_formatter extends base_formatter;
        int mask_val;

        extern function new(int id, int mask);
        extern virtual function int format_header(int raw_tag);
    endclass

    function custom_formatter::new(int id, int mask);
        super.new(id);
        this.mask_val = mask;
    endfunction

    function int custom_formatter::format_header(int raw_tag);
        return (raw_tag & this.mask_val) + this.stream_id;
    endfunction

endpackage