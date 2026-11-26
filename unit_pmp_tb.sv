module unit_pmp_tb;
  import zetraa_pkg::*;
  import cva6_config_pkg::*;

  localparam config_pkg::cva6_cfg_t CVA6Cfg = build_config_pkg::build_config(cva6_config_pkg::cva6_cfg);

  typedef struct packed {
    logic [CVA6Cfg.XLEN-1:0] cause;
    logic [CVA6Cfg.XLEN-1:0] tval;
    logic [CVA6Cfg.GPLEN-1:0] tval2;
    logic [31:0] tinst;
    logic gva;
    logic valid;
  } exception_t;

  typedef struct packed {
    logic fetch_valid;
    logic [CVA6Cfg.PLEN-1:0] fetch_paddr;
    exception_t fetch_exception;
  } icache_areq_t;

  logic clk;
  logic rst_n;
  icache_areq_t icache_areq_in;
  icache_areq_t icache_areq_out;
  logic [CVA6Cfg.VLEN-1:0] icache_fetch_vaddr_in;

  riscv::priv_lvl_t priv_lvl_in;
  logic [avoid_neg(8-1):0][CVA6Cfg.PLEN-3:0] pmpaddr_in;
  riscv::pmpcfg_t [avoid_neg(8-1):0] pmpcfg_in;

  pmp_data_if #(
    .CVA6Cfg(CVA6Cfg),
    .icache_areq_t(icache_areq_t),
    .exception_t(exception_t)
  ) dut (
    .clk_i(clk),
    .rst_ni(rst_n),
    .icache_areq_i(icache_areq_in),
    .icache_areq_o(icache_areq_out),
    .icache_fetch_vaddr_i(icache_fetch_vaddr_in),
    .lsu_valid_i(1'b0),
    .lsu_paddr_i('0),
    .lsu_vaddr_i('0),
    .lsu_exception_i('0),
    .lsu_is_store_i(1'b0),
    .lsu_valid_o(),
    .lsu_paddr_o(),
    .lsu_exception_o(),
    .priv_lvl_i(priv_lvl_in),
    .v_i(1'b0),
    .ld_st_priv_lvl_i(priv_lvl_in),
    .ld_st_v_i(1'b0),
    .pmpcfg_i(pmpcfg_in),
    .pmpaddr_i(pmpaddr_in)
  );

  always @(dut.pmp_if_allow or dut.i_pmp_if.allow_o) begin
    $display("[UNIT_TRACK] T=%0t pmp_if_allow=%b allow_o=%b", $time, dut.pmp_if_allow, dut.i_pmp_if.allow_o);
  end

  initial begin
    clk = 0;
    rst_n = 1;
    priv_lvl_in = riscv::PRIV_LVL_M;
    pmpcfg_in = '0;
    pmpaddr_in = '0;
    icache_fetch_vaddr_in = 64'h10000;
    icache_areq_in = '0;
    icache_areq_in.fetch_valid = 1'b1;
    icache_areq_in.fetch_paddr = 64'h10000;
    #10;
    $display("[UNIT_PMP_IF] pmp_if_allow=%0d match_exec=%0d f_ex_val=%0d f_ex_cause=%0d dut_paddr=0x%h dut_valid=%b priv=%0d pmpcfg0=0x%h",
             dut.pmp_if_allow, dut.match_any_execute_region,
             icache_areq_out.fetch_exception.valid, icache_areq_out.fetch_exception.cause,
             dut.icache_areq_i.fetch_paddr, dut.icache_areq_i.fetch_valid,
             dut.priv_lvl_i, dut.pmpcfg_i[0]);
    $finish;
  end
endmodule
