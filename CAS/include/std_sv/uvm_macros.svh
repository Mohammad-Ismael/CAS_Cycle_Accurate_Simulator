`ifndef UVM_MACROS_SVH
`define UVM_MACROS_SVH

`ifdef VERILATOR
class uvm_object_registry #(type T = int);
    static function T create(string name = "");
        T obj = new(name);
        return obj;
    endfunction
endclass

class uvm_component_registry #(type T = int);
    static function T create(string name = "", uvm_component parent = null);
        T obj = new(name, parent);
        return obj;
    endfunction
endclass

`define uvm_component_utils(T) typedef uvm_component_registry#(T) type_id;
`define uvm_component_param_utils(T) typedef uvm_component_registry#(T) type_id;
`define uvm_object_utils(T) typedef uvm_object_registry#(T) type_id;
`define uvm_object_utils_begin(T) typedef uvm_object_registry#(T) type_id;
`else
`define uvm_component_utils(T)
`define uvm_component_param_utils(T)
`define uvm_object_utils(T)
`define uvm_object_utils_begin(T)
`endif

`define uvm_info(ID, MSG, VERBOSITY) begin $display("[%s] %s", ID, MSG); end
`define uvm_warning(ID, MSG) begin $display("[UVM_WARNING %s] %s", ID, MSG); end
`define uvm_error(ID, MSG) begin $display("[UVM_ERROR %s] %s", ID, MSG); end
`define uvm_fatal(ID, MSG) begin $display("[UVM_FATAL %s] %s", ID, MSG); $finish; end
`define uvm_object_utils_end
`define uvm_field_int(ARG, FLAG)

`endif
