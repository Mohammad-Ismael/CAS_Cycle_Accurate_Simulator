`timescale 1ns/1ps

package v4_2_1_0_pkg;

function int pick_deterministic(int mode);
    if (mode == 0) begin
        randcase
            0: return 100;
            10: return 200;
            0: return 300;
        endcase
    end else begin
        randcase
            5: return 555;
            0: return 999;
        endcase
    end
    return 0;
endfunction

function int count_static_calls();
    static int call_count = 0;
    call_count++;
    return call_count;
endfunction

endpackage

module randcase_static_dut (
    input  logic [31:0] mode,
    output logic [31:0] out_det,
    output logic [31:0] out_calls
);
    import v4_2_1_0_pkg::*;

    always_comb begin
        out_det = pick_deterministic(mode);
    end

    function automatic int get_next_call();
        return count_static_calls();
    endfunction

endmodule
