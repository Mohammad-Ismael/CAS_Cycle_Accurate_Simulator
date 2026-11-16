`resetall
`timescale 1ns/1ps

package v4_4_pkg;
    // Strict typed enum (§6.19)
    typedef enum logic [1:0] {
        STATE_IDLE  = 2'b00,
        STATE_CALC  = 2'b01,
        STATE_CHECK = 2'b10,
        STATE_DONE  = 2'b11
    } state_e;

    // Struct typedef (§7.2)
    typedef struct {
        int a;
        int b;
    } pair_t;

    // Interface classes with parameter type conflict resolution per IEEE 1800-2023 §8.26.6.2
    interface class ic1#(type T = int);
        pure virtual function int fn1(int a);
    endclass

    interface class ic2#(type T = int);
        pure virtual function int fn2(int a);
    endclass

    interface class ic3#(type TYPE = int) extends ic1#(TYPE), ic2#(TYPE);
        typedef TYPE T;
    endclass

    class ConcreteCalc implements ic3#(int);
        virtual function int fn1(int a);
            return a + 5;
        endfunction

        virtual function int fn2(int a);
            return a * 2;
        endfunction
    endclass

endpackage

`resetall
`timescale 1ns/1ps

module dut_v4_4 (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        start,
    input  int          in_fork_val,
    output logic [31:0] out_val,
    output logic        done
);
    import v4_4_pkg::*;

    state_e current_state;
    pair_t p_data;
    real r_factor;
    int acc_val;

    // Void subroutine with explicit empty return (§13.4.1)
    function automatic void init_pair(input int x, input int y);
        p_data.a = x;
        p_data.b = y;
        return;
    endfunction

    // Non-void subroutine with expression return (§13.4.1)
    function automatic int sum_pair(input int x, input int y);
        return x + y;
    endfunction

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            current_state <= STATE_IDLE;
            out_val <= 32'd0;
            done <= 1'b0;
            r_factor <= 0.0;
            acc_val <= 0;
            init_pair(15, 25);
        end else begin
            case (current_state)
                STATE_IDLE: begin
                    if (start) begin
                        // Strict enum assignment with explicit cast (§6.19.4)
                        current_state <= state_e'(2'b01); // STATE_CALC
                        r_factor <= 10.0; // Valid real arithmetic without illegal bit-selects (§6.12)
                        acc_val <= sum_pair(p_data.a, p_data.b); // 15 + 25 = 40
                    end
                end

                STATE_CALC: begin
                    ConcreteCalc calc = new();
                    int c_res = calc.fn1(acc_val) + calc.fn2(10); // (40 + 5) + (10 * 2) = 45 + 20 = 65
                    out_val <= c_res + in_fork_val + int'(r_factor); // 65 + 50 + 10 = 125
                    current_state <= state_e'(2'b10); // STATE_CHECK
                end

                STATE_CHECK: begin
                    if (out_val == 32'd125) begin
                        current_state <= state_e'(2'b11); // STATE_DONE
                        done <= 1'b1;
                    end else begin
                        current_state <= STATE_IDLE;
                    end
                end

                STATE_DONE: begin
                    done <= 1'b1;
                end

                default: begin
                    current_state <= STATE_IDLE;
                end
            endcase
        end
    end

endmodule
