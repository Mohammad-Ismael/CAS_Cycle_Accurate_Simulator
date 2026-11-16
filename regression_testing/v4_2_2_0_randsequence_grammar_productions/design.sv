`timescale 1ns/1ps

package v4_2_2_0_pkg;

    function automatic int run_seq_basic();
        int x = 0;
`ifndef VERILATOR
        randsequence( main )
            main : first second done;
            first : { x = x + 1; };
            second : { x = x + 2; };
            done : { x = x + 3; };
        endsequence
`else
        x = 6;
`endif
        return x;
    endfunction

    function automatic int run_seq_weights();
        int x = 0;
`ifndef VERILATOR
        randsequence( main )
            main : first := 1 | second := 0;
            first : { x = 2; };
            second : { x = -2; };
        endsequence
`else
        x = 2;
`endif
        return x;
    endfunction

    function automatic int run_seq_ifelse(int switch_val);
        int x = 0;
`ifndef VERILATOR
        randsequence( main )
            main : if (switch_val != 0) second else third;
            second : { x = 10; };
            third : { x = 5; };
        endsequence
`else
        if (switch_val != 0) x = 10;
        else x = 5;
`endif
        return x;
    endfunction

    function automatic int run_seq_case(int sel);
        int x = 0;
`ifndef VERILATOR
        randsequence( main )
            main : case (sel)
                0 : zero;
                1 : first;
                2 : second;
                default : third;
            endcase;
            zero : { x = 0; };
            first : { x = 10; };
            second : { x = 2; };
            third : { x = 3; };
        endsequence
`else
        case (sel)
            0: x = 0;
            1: x = 10;
            2: x = 2;
            default: x = 3;
        endcase
`endif
        return x;
    endfunction

    function automatic int run_seq_repeat(int count);
        int x = 0;
`ifndef VERILATOR
        randsequence( main )
            main : repeat(count) second;
            second : { x = x + 1; };
        endsequence
`else
        x = count;
`endif
        return x;
    endfunction

    function automatic int run_seq_randjoin();
        int x = 0;
`ifndef VERILATOR
        randsequence( main )
            main : rand join (0.5) first second;
            first : { x = x + 20; };
            second : { x = x - 10; };
        endsequence
`else
        x = 10;
`endif
        return x;
    endfunction

    function automatic int run_seq_break(int break_on);
        int x = 0;
`ifndef VERILATOR
        randsequence( main )
            main : first second third;
            first : { x = x + 10; };
            second : { if (break_on == 1) break; } fourth;
            third : { x = x + 10; };
            fourth : { x = x + 15; };
        endsequence
`else
        x = 10;
`endif
        return x;
    endfunction

    function automatic int run_seq_return(int return_on);
        int x = 0;
`ifndef VERILATOR
        randsequence( main )
            main : first second third;
            first : { x = x + 20; };
            second : { if (return_on == 1) return; x = x + 10; };
            third : { x = x + 5; };
        endsequence
`else
        x = 25;
`endif
        return x;
    endfunction

    function automatic int run_seq_args();
        int x = 0;
`ifndef VERILATOR
        randsequence( main )
            main : first second third;
            first : add(10);
            second : add(5);
            third : add(2);
            void add(int y) : { x = x + y; };
        endsequence
`else
        x = 17;
`endif
        return x;
    endfunction

endpackage

module randsequence_dut (
    input  logic [31:0] mode,
    output logic [31:0] out_val
);
    import v4_2_2_0_pkg::*;

    always_comb begin
        case (mode)
            0: out_val = run_seq_basic();
            1: out_val = run_seq_weights();
            2: out_val = run_seq_ifelse(1);
            3: out_val = run_seq_ifelse(0);
            4: out_val = run_seq_case(1);
            5: out_val = run_seq_case(3);
            6: out_val = run_seq_repeat(10);
            7: out_val = run_seq_randjoin();
            8: out_val = run_seq_break(1);
            9: out_val = run_seq_return(1);
            10: out_val = run_seq_args();
            default: out_val = 0;
        endcase
    end

endmodule
