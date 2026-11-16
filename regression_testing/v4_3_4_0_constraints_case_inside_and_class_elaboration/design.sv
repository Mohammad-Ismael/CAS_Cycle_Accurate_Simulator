// CAS Milestone Regression Suite: v4.3.4.0
// Feature Focus: Constraint Blocks (§18.5), Case Inside (§12.5.4), Bare Unsigned Formals, 
// Parameterized Base Class Elaboration (§8.25), Typed Constructors, Disable Block (§9.6.2), & Automatic Procedural Variables (§6.21)

`timescale 1ns/1ps

class base_item #(type T = int, int WIDTH = 32);
    T base_id;
    function new(T id = 0);
        this.base_id = id;
    endfunction
    virtual function T get_id();
        return base_id;
    endfunction
endclass

class derived_item extends base_item #(int, 32);
    int unsigned extra_data;
    function new(int id = 10, int unsigned extra = 32'hAAAA);
        super.new(id);
        this.extra_data = extra;
    endfunction
    virtual function int get_id();
        return base_id + int'(extra_data & 32'hFF);
    endfunction
endclass

class random_packet;
    rand int unsigned length;
    rand bit [3:0] priority_lvl;

`ifndef VERILATOR
    constraint c_dist {
        length dist { [1:10] := 5, [11:20] :/ 10 };
    }
    constraint c_ordering {
        solve priority_lvl before length;
    }
    extern constraint c_limits;
`endif

    function new();
        length = 15;
        priority_lvl = 4'h3;
    endfunction
endclass

`ifndef VERILATOR
constraint random_packet::c_limits {
    soft length > 0;
    soft length <= 100;
}
`endif

/* verilator lint_off WIDTHEXPAND */
/* verilator lint_off WIDTHTRUNC */

module dut_v4_3_4 (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [31:0] val_in,
    input  logic [31:0] seed_in,
    input  logic [31:0] data_in,
    input  logic [3:0]  limit_in,
    output logic [31:0] case_inside_out,
    output logic [31:0] crc_out,
    output logic [31:0] disable_acc_out,
    output logic [31:0] class_result_out
);

    // Function with bare unsigned formals (IEEE 1800-2023 §6.7)
    function automatic int unsigned calc_hash(input unsigned s, input unsigned d);
        int unsigned acc;
        acc = int'(s) ^ (int'(d) << 3) ^ 32'h5A5A5A5A;
        return acc;
    endfunction

    // Function with case ... inside pattern matching (IEEE 1800-2023 §12.5.4)
    function automatic int eval_case_inside(input int v);
        int r;
        case (v) inside
            1, 3, 5:   r = 100;
            [10:20]:   r = 200;
            [30:40]:   r = 300;
            default:   r = 999;
        endcase
        return r;
    endfunction

    // Function with disable block and procedural automatic variable (IEEE 1800-2023 §9.6.2, §6.21)
    function automatic int run_disable_loop(input int max_limit);
        int acc;
        acc = 0;
        begin : step_loop
            for (int i = 0; i < 10; i++) begin
                automatic int step_val = (i + 1) * 3;
                acc += step_val;
                if (i == max_limit) begin
                    disable step_loop;
                end
            end
        end
        return acc;
    endfunction

    derived_item item_inst;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            case_inside_out  <= 32'd0;
            crc_out          <= 32'd0;
            disable_acc_out  <= 32'd0;
            class_result_out <= 32'd0;
            item_inst        = null;
        end else begin
            case_inside_out  <= eval_case_inside(val_in);
            crc_out          <= calc_hash(seed_in, data_in);
            disable_acc_out  <= run_disable_loop(int'(limit_in));

            if (item_inst == null) begin
                item_inst = new(100, 32'h55);
            end
            class_result_out <= item_inst.get_id();
        end
    end

endmodule
