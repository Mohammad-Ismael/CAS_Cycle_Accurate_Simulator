// CAS v4.3.5.0 - Full Positive Elaboration Parity
interface class IDevice;
    pure virtual function int get_id();
    pure virtual function int get_val();
endclass

virtual class BaseDevice implements IDevice;
    int id = 42;
    virtual function int get_id();
        return id;
    endfunction
    pure virtual function int get_val();
endclass

class ConcreteDevice extends BaseDevice;
    int val = 99;
    virtual function int get_val();
        return val;
    endfunction
endclass

module dut (
    input  logic clk,
    input  logic rst_n,
    input  logic en,
    input  logic [7:0] in_a,
    input  logic [7:0] in_b,
    output logic [7:0] out_concat_slice,
    output int         out_loop_sum,
    output int         out_dev_val
);
    time t_cur;

    // Concatenation slice
    always_comb begin
        out_concat_slice = {in_a, in_b}[11:4];
    end

    // Do-while loop
    always_comb begin
        int i = 0;
        int sum = 0;
        do begin
            sum += i;
            i++;
        end while (i <= 4);
        out_loop_sum = sum;
    end

    // Event control with iff and declaration initializer
    logic trig_out = 0;
    always @(posedge clk iff en) begin
        if (!rst_n) begin
            trig_out <= 0;
        end else begin
            trig_out <= ~trig_out;
        end
    end

    // Interface class polymorphism
    always_comb begin
        ConcreteDevice dev = new();
        out_dev_val = dev.get_id() + dev.get_val();
    end

endmodule
