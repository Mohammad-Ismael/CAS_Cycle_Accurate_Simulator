`ifndef UVM_MACROS_SVH
`define UVM_MACROS_SVH

`define uvm_info(ID, MSG, VERBOSITY) begin $display("[%s] %s", ID, MSG); end
`define uvm_warning(ID, MSG) begin $display("[UVM_WARNING %s] %s", ID, MSG); end
`define uvm_error(ID, MSG) begin $display("[UVM_ERROR %s] %s", ID, MSG); end
`define uvm_fatal(ID, MSG) begin $display("[UVM_FATAL %s] %s", ID, MSG); $finish; end
`define uvm_component_utils(T)
`define uvm_object_utils(T)

`endif
