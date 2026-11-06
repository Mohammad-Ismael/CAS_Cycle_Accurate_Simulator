`timescale 1ns/1ps

module ibex_simple_system_tb;
  logic IO_CLK;
  logic IO_RST_N;

  initial begin
    IO_CLK = 0;
    #100;
    $display("[MEM-CHECK] word 297 = %h", dut.u_ram.u_ram.mem[297]);
  end

  // Free-running 50 MHz clock (20ns period)
  always begin
    #10 IO_CLK = ~IO_CLK;
  end

  // Reset generation: Assert reset (0) for 50ns, then deassert (1)
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
    if (IO_RST_N && dut.u_ram.a_req_i) begin
      $display("[RAM_PORT_A] T=%0t req=%b we=%b addr_raw=%h addr_idx=%0d rdata=%h mem_at_idx=%h",
               $time, dut.u_ram.a_req_i, dut.u_ram.a_we_i, dut.u_ram.a_addr_i, dut.u_ram.a_addr_idx,
               dut.u_ram.a_rdata_o, dut.u_ram.u_ram.mem[dut.u_ram.a_addr_idx]);
    end
  end
endmodule
