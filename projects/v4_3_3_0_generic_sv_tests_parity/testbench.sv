// Milestone Testbench v4.3.3.0: Generic SystemVerilog Constructs Parity
/* verilator lint_off WIDTHEXPAND */
/* verilator lint_off WIDTHTRUNC */
/* verilator lint_off UNUSEDSIGNAL */

package test_pkg;
    interface class BaseIface;
        pure virtual function int get_id();
    endclass

    class PkgItem;
        int id = 42;
        function new();
        endfunction
    endclass
endpackage

// Forward typedefs
typedef struct fwd_struct_t;
typedef union fwd_union_t;
typedef enum fwd_enum_t;
typedef fwd_plain_t;

typedef int fwd_plain_t;

typedef struct {
    rand bit [15:0] x;
    randc bit [15:0] y;
} fwd_struct_t;

typedef union {
    logic [15:0] raw;
    logic [15:0] alt;
} fwd_union_t;

typedef enum bit [3:0] {
    OP_ADD = 4'h0,
    OP_SUB = 4'h1,
    OP_AND = 4'h2,
    OP_OR  = 4'h3
} fwd_enum_t;

// Package-qualified inheritance & interface class
class ConcreteItem extends test_pkg::PkgItem implements test_pkg::BaseIface;
    function new();
        super.new();
    endfunction
    virtual function int get_id();
        return id + 10;
    endfunction
endclass

// Nested class
`ifndef VERILATOR
class Container;
    class Inner;
        int multiplier = 3;
        function int compute(int v);
            return v * multiplier;
        endfunction
    endclass
endclass
`else
class Container_Inner;
    int multiplier = 3;
    function int compute(int v);
        return v * multiplier;
    endfunction
endclass
`endif

module tb_v4_3_3_regression;
    logic        clk;
    logic        rst_n;
    logic [15:0] a;
    logic [15:0] b;
    logic [2:0]  op;
    logic [15:0] result;
    logic        zero;

    // Number literals with whitespace
    parameter int C_SIZE = 16 'd 16;
    parameter int C_BASE = 16 'h 0005;

    v4_3_3_alu #(.WIDTH(C_SIZE)) dut (
        .clk(clk),
        .rst_n(rst_n),
        .a(a),
        .b(b),
        .op(op),
        .result(result),
        .zero(zero)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        ConcreteItem c_item;
`ifndef VERILATOR
        Container::Inner inner_obj;
`else
        Container_Inner inner_obj;
`endif
        fwd_struct_t st;
        fwd_enum_t e_op;
        int check_sum;

        rst_n = 0;
        a = 16 'h 0000;
        b = 16 'h 0000;
        op = 3 'b 000;
        check_sum = 0;

        c_item = new();
        inner_obj = new();

        st.x = 16 'd 100;
        st.y = 16 'd 200;
        e_op = OP_ADD;

        #15;
        rst_n = 1;

        // Loop using var int and bit declarations in header
        for (var int i = 0, bit active = 1 'b 1; i < 4; i++) begin
            @(posedge clk);
            a = st.x + i;
            b = st.y - i;
            op = i[2:0];
            @(negedge clk);
            check_sum = check_sum + result;
        end

        // Verify package and nested class calls
        if (c_item.get_id() != 52) begin
            $display("[FAIL] Expected c_item.get_id() == 52, got %0d", c_item.get_id());
            $fatal(1);
        end

        if (inner_obj.compute(10) != 30) begin
            $display("[FAIL] Expected inner_obj.compute(10) == 30, got %0d", inner_obj.compute(10));
            $fatal(1);
        end

        $display("[PASS] All generic SystemVerilog construct tests completed cleanly! check_sum=%0d", check_sum);
        $finish;
    end

endmodule
