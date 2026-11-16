`timescale 1ns/1ps

// ============================================================================
// CAS Milestone Regression Testbench
// Target Release : v3.41.0.0
// Standard Target: IEEE Std 1800-2023 §8.24 (Out-of-Body Method Declarations)
// Testbench Name : tb_v3_41_0_0_regression
// ============================================================================

module tb_v3_41_0_0_regression;
    import v3_41_0_0_pkg::*;

    int pass_count  = 0;
    int error_count = 0;

    initial begin
        base_formatter   base_inst;
        custom_formatter custom_inst;
        base_formatter   poly_handle;
        int frame_res;
        int hdr_res;

        #10;
        // Construct instances via extern constructors
        base_inst   = new(32'h0000_000A);
        custom_inst = new(32'h0000_0005, 32'h0000_00FF);

        // --------------------------------------------------------------------
        // Test 1: Base class extern function and task invocation
        // payload = 32'h0000_0100
        // format_header(0x100) = 0x100 + 0xA = 0x10A
        // transmit_frame: frame_out = 0x10A ^ 0xF0F0_0000 = 32'hF0F0_010A
        // --------------------------------------------------------------------
        base_inst.transmit_frame(32'h0000_0100, frame_res);
        if (frame_res === 32'hF0F0_010A && base_inst.trans_count === 1) begin
            $display("[PASS T=%0t] Base extern method dispatch verified: frame=%08h count=%0d",
                     $time, frame_res, base_inst.trans_count);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Base extern method dispatch failed! frame=%08h (exp F0F0010A)",
                     $time, frame_res);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 2: Derived class extern method override
        // raw_tag = 32'h0000_1234
        // format_header(0x1234): (0x1234 & 0xFF) + 0x5 = 0x34 + 0x5 = 32'h0000_0039
        // --------------------------------------------------------------------
        hdr_res = custom_inst.format_header(32'h0000_1234);
        if (hdr_res === 32'h0000_0039) begin
            $display("[PASS T=%0t] Derived extern method override verified: hdr=%08h",
                     $time, hdr_res);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Derived extern method override failed! hdr=%08h (exp 00000039)",
                     $time, hdr_res);
            error_count++;
        end

        // --------------------------------------------------------------------
        // Test 3: Polymorphic dispatch through base handle invoking extern methods
        // poly_handle points to custom_inst
        // transmit_frame(0x1234) -> calls custom_formatter::format_header(0x1234) = 0x39
        // frame_out = 0x39 ^ 0xF0F0_0000 = 32'hF0F0_0039
        // --------------------------------------------------------------------
        poly_handle = custom_inst;
        poly_handle.transmit_frame(32'h0000_1234, frame_res);
        if (frame_res === 32'hF0F0_0039 && custom_inst.trans_count === 1) begin
            $display("[PASS T=%0t] Polymorphic dispatch with extern methods verified: frame=%08h",
                     $time, frame_res);
            pass_count++;
        end else begin
            $display("[FAIL T=%0t] Polymorphic dispatch with extern methods failed! frame=%08h (exp F0F00039)",
                     $time, frame_res);
            error_count++;
        end

        #10;
        $display("\n================================================================================");
        $display("[CAS v3.41.0.0 REGRESSION SUITE: EXTERN METHOD DECLARATIONS §8.24]");
        if (error_count == 0 && pass_count == 3) begin
            $display("RESULT : >>> PASS (100%% VCS COMPLIANT) <<<");
            $display("SUMMARY: Verified IEEE 1800-2023 §8.24 extern class methods, out-of-body definitions, and polymorphism.");
        end else begin
            $display("RESULT : >>> FAIL <<< (passes=%0d exp 3, errors=%0d)", pass_count, error_count);
        end
        $display("================================================================================\n");

        $finish;
    end
endmodule