`timescale 1ns/1ps

package tagged_types_pkg;
    typedef union tagged {
        void Invalid;
        int Valid;
    } opt_int_t;

    typedef union tagged packed {
        void Empty;
        logic [6:0] Data;
    } pkt_u_t;
endpackage

module tagged_union_tester (
    input  logic clk,
    output int   out_valid_val,
    output logic [7:0] out_pkt_packed,
    output logic [6:0] out_pkt_data,
    output int   out_anon_mod_val,
    output int   out_anon_proc_val,
    output int   status_errors
);
    import tagged_types_pkg::*;

    opt_int_t opt_val;
    pkt_u_t   pkt_val;

    // Anonymous module-level tagged union variable
    union tagged {
        void None;
        int Number;
    } mod_anon;

    int errors;

    initial begin
        errors = 0;

        // 1. opt_int_t test
        opt_val = tagged Invalid;
        opt_val = tagged Valid(42);
        if (opt_val.Valid !== 42) begin
            $display("[FAIL] opt_val.Valid expected 42, got %0d", opt_val.Valid);
            errors++;
        end

        opt_val = tagged Valid(-99);
        if (opt_val.Valid !== -99) begin
            $display("[FAIL] opt_val.Valid expected -99, got %0d", opt_val.Valid);
            errors++;
        end

        // 2. pkt_u_t packed tagged union test
        pkt_val = tagged Empty;
        if (pkt_val !== 8'h00) begin
            $display("[FAIL] pkt_val Empty expected 8'h00, got 8'h%02x", pkt_val);
            errors++;
        end

        pkt_val = tagged Data(7'b1010101);
        if (pkt_val !== 8'hD5) begin
            $display("[FAIL] pkt_val Data raw packed expected 8'hD5, got 8'h%02x", pkt_val);
            errors++;
        end
        if (pkt_val.Data !== 7'b1010101) begin
            $display("[FAIL] pkt_val.Data expected 7'b1010101, got 7'b%07b", pkt_val.Data);
            errors++;
        end

        // 3. Anonymous module-level tagged union
        mod_anon = tagged None;
        mod_anon = tagged Number(12345);
        if (mod_anon.Number !== 12345) begin
            $display("[FAIL] mod_anon.Number expected 12345, got %0d", mod_anon.Number);
            errors++;
        end

        // 4. Anonymous procedural-level tagged union
        begin
            union tagged {
                void Nil;
                int Value;
            } proc_anon;
            proc_anon = tagged Nil;
            proc_anon = tagged Value(67890);
            if (proc_anon.Value !== 67890) begin
                $display("[FAIL] proc_anon.Value expected 67890, got %0d", proc_anon.Value);
                errors++;
            end
            out_anon_proc_val = proc_anon.Value;
        end

        out_valid_val    = opt_val.Valid;
        out_pkt_packed   = pkt_val;
        out_pkt_data     = pkt_val.Data;
        out_anon_mod_val = mod_anon.Number;
        status_errors    = errors;
    end
endmodule
