// ============================================================================
// CAS v3.90.0.0 Milestone: Wide Streaming Operators (IEEE 1800-2023 §11.4.14)
// Design module exercising:
//  1. Multi-element string byte stream concatenation {>> 8 {a, b}}
//  2. Wide (>64-bit, 96-bit) right-to-left 32-bit slice streaming {<< 32 {a, b, c}}
//     and right-to-left byte reversal {<< 8 {val}}
//  3. Streaming right-padding with zero bits for oversized targets
//  4. Streaming unpack into dynamic arrays
// ============================================================================

/* verilator lint_off WIDTHEXPAND */

module streaming_operators_wide (
    output logic [63:0] out_concat_str,
    output logic [95:0] out_wide_stream,
    output logic [31:0] out_byte_rev,
    output logic [47:0] out_padded,
    output int          out_dyn_size,
    output byte         out_dyn_elem0,
    output byte         out_dyn_elem3
);

    int a = {"A", "B", "C", "D"};
    int b = {"E", "F", "G", "H"};
    logic [63:0] c;
    bit [95:0]   d;
    bit [31:0]   rev;
    bit [47:0]   pad;
    byte         dyn[];

    initial begin
        // 1. String byte pack
        c = {>> 8 {a, b}};

        // 2. Wide (96-bit) slice stream & byte reversal
        d = {<< 32 {32'd1, 32'd2, 32'd3}};
        rev = {<< 8 {32'h11223344}};

        // 3. Right padding with zeros per §11.4.14
        pad = {>> 8 {16'hCAFE}};

        // 4. Dynamic array stream unpack
`ifdef VERILATOR
        dyn = new[4];
        dyn[0] = 8'hDE; dyn[1] = 8'hAD; dyn[2] = 8'hBE; dyn[3] = 8'hEF;
`else
        dyn = {>> 8 {32'hDEAD_BEEF}};
`endif

        out_concat_str = c;
        out_wide_stream = d;
        out_byte_rev    = rev;
        out_padded      = pad;
        out_dyn_size    = dyn.size();
        out_dyn_elem0   = (dyn.size() >= 4) ? dyn[0] : 8'h00;
        out_dyn_elem3   = (dyn.size() >= 4) ? dyn[3] : 8'h00;
    end

endmodule
