// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.94.0.0
// Standard Target: IEEE Std 1800-2023 §7.5 Dynamic Arrays & §21.2.1.2 Hex Formatting
// Module Name    : v3_94_0_0_dynamic_arrays_and_hex_pad_core
// ============================================================================

`timescale 1ns/1ps

module v3_94_0_0_dynamic_arrays_and_hex_pad_core (
    input  logic        clk,
    input  logic        rst_n,
    output int          out_size_init,
    output int          out_size_resized,
    output int          out_size_deleted,
    output logic [7:0]  out_elem_read,
    output string       out_hex_pad_zero,
    output string       out_hex_pad_byte,
    output string       out_hex_pad_upper,
    output string       out_hex_unpadded
);
    // 1. IEEE 1800 §7.5: Module-scope dynamic array
    bit [7:0] dyn_mod[];

    initial begin
        out_size_init     = 0;
        out_size_resized  = 0;
        out_size_deleted  = -1;
        out_elem_read     = 8'h00;
        out_hex_pad_zero  = "";
        out_hex_pad_byte  = "";
        out_hex_pad_upper = "";
        out_hex_unpadded  = "";

        // Allocate 16 elements
        dyn_mod = new [16];
        out_size_init = dyn_mod.size();
        dyn_mod[5] = 8'hA5;
        out_elem_read = dyn_mod[5];

        // Resize to 8 elements
        dyn_mod = new [8];
        out_size_resized = dyn_mod.size();

        // Delete array
        dyn_mod.delete();
        out_size_deleted = dyn_mod.size();

        // 2. IEEE 1800 §21.2.1.2: Width-based hex zero padding
        out_hex_pad_zero  = $sformatf("%h", 8'h00);       // "00"
        out_hex_pad_byte  = $sformatf("%h", 8'h0f);       // "0f"
        out_hex_pad_upper = $sformatf("%H", 12'h0a5);     // "0A5"
        out_hex_unpadded  = $sformatf("%0h", 8'h0f);      // "f"
    end
endmodule
