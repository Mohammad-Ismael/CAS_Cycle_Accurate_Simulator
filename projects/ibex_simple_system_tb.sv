`timescale 1ns/1ps

module ibex_simple_system_tb;
  logic IO_CLK;
  logic IO_RST_N;

  initial begin
    IO_CLK = 0;
  end

  always begin
    #10 IO_CLK = ~IO_CLK;
  end

  initial begin
    IO_RST_N = 0;
    #50;
    IO_RST_N = 1;
  end

  parameter SRAMInitFile = "";

  ibex_simple_system #(
    .SRAMInitFile(SRAMInitFile)
  ) dut (
    .IO_CLK(IO_CLK),
    .IO_RST_N(IO_RST_N)
  );

  always @(posedge IO_CLK) begin
    if (IO_RST_N && dut.u_top.data_req_o && dut.u_top.data_we_o && (dut.u_top.data_addr_o == 32'h00020000)) begin
      $write("%c", dut.u_top.data_wdata_o[7:0]);
    end
  end

endmodule