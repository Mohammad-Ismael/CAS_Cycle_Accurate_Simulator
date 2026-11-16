// ============================================================================
// CAS (Cycle Accurate Simulator) // Milestone Regression Benchmark
// Target Release : v3.93.0.0
// Standard Target: IEEE Std 1800-2023 Chapter 8 (Classes, OOP, Interface Classes)
//                  §8.26 Interface classes, typedefs, and parameters
//                  §8.5 Parameterized classes
//                  §8.7 Class constructor and property access
// Module Name    : v3_93_0_0_classes_core
// ============================================================================

// 1. IEEE 1800 §8.26: Interface class with typedef, localparam, and pure virtual method
interface class iface_calc;
    typedef int val_t;
    localparam int SCALE = 10;
    pure virtual function int compute(val_t x);
endclass

// 2. Implementing class using scoped type iface_calc::val_t and constructor
class adder_impl implements iface_calc;
    int offset;

    function new(int init_offset = 5);
        offset = init_offset;
    endfunction

    virtual function int compute(iface_calc::val_t x);
        return (x * 10) + offset;
    endfunction
endclass

// 3. IEEE 1800 §8.5: Parameterized class
class param_box #(parameter int WIDTH = 12);
endclass

module v3_93_0_0_classes_core (
    input  logic clk,
    input  logic rst_n,
    output int   out_box_override,
    output int   out_box_default,
    output int   out_calc_default,
    output int   out_calc_custom
);
    // Module-scope parameterized class handles
    param_box #(34) box_inst;
    param_box #()   def_box;

    initial begin
        out_box_override = 0;
        out_box_default  = 0;
        out_calc_default = 0;
        out_calc_custom  = 0;

        box_inst = new;
        def_box  = new;

        out_box_override = box_inst.WIDTH;
        out_box_default  = def_box.WIDTH;

        // Procedural class handles and constructor argument variants
        begin
            adder_impl h1 = new;     // default init_offset = 5
            adder_impl h2 = new(20); // explicit init_offset = 20
            out_calc_default = h1.compute(7); // (7 * 10) + 5 = 75
            out_calc_custom  = h2.compute(7); // (7 * 10) + 20 = 90
        end
    end
endmodule
