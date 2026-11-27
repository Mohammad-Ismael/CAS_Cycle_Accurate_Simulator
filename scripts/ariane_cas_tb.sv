`timescale 1ns/1ps

import zetraa_pkg::*;

module ariane_cas_tb;
    localparam config_pkg::cva6_cfg_t CVA6Cfg = build_config_pkg::build_config(cva6_config_pkg::cva6_cfg);
    localparam int unsigned CLOCK_PERIOD = 20;
    localparam int unsigned RTC_CLOCK_PERIOD = 30517;
    localparam NUM_WORDS = 2**18;

    logic clk_i = 1'b0;
    logic rst_ni = 1'b1;
    logic rtc_i = 1'b0;
    logic [31:0] exit_o;

    ariane_testharness #(
        .CVA6Cfg ( CVA6Cfg ),
        .NUM_WORDS ( NUM_WORDS ),
        .InclSimDTM ( 1'b1 ),
        .StallRandomOutput ( 1'b0 ),
        .StallRandomInput ( 1'b0 )
    ) zetraa_dut (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .rtc_i(rtc_i),
        .exit_o(exit_o)
    );

    // Clock process
    initial begin
        clk_i = 1'b0;
        forever #(CLOCK_PERIOD/2) clk_i = ~clk_i;
    end

    // Reset process: 1 -> 0 -> 1
    initial begin
        rst_ni = 1'b1;
        #(5);
        rst_ni = 1'b0;
        #(100);
        rst_ni = 1'b1;
    end

    // RTC clock process
    initial begin
        forever begin
            rtc_i = 1'b0;
            #(RTC_CLOCK_PERIOD/2) rtc_i = 1'b1;
            #(RTC_CLOCK_PERIOD/2) rtc_i = 1'b0;
        end
    end

    always @(zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.pmp_if_allow or
             zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.i_pmp_if.allow_o) begin
        $display("[PMP_TRACK] T=%0t pmp_if_allow=%b i_pmp_if.allow_o=%b",
                 $time,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.pmp_if_allow,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.i_pmp_if.allow_o);
    end

    initial begin
        $display("[SOC_ID_DIAG] cva6=%0d axi=%0d soc=%0d slv=%0d nrslv=%0d",
                 cva6_config_pkg::CVA6ConfigAxiIdWidth,
                 ariane_axi::IdWidth,
                 ariane_axi_soc::IdWidth,
                 ariane_axi_soc::IdWidthSlave,
                 ariane_soc::NrSlaves);
    end

    // Commit monitor
    integer commit_cnt = 0;
    integer axi_ar_cnt = 0;
    integer axi_r_cnt = 0;
    integer ic_req_cnt = 0;
    integer commit_evt_cnt = 0;
    always @(posedge clk_i) begin
        if (($time >= 290 && $time <= 310) || ($time >= 5320 && $time <= 5370)) begin
            $display("[CHAIN_PROBE] T=%0t fe_rdy=%b fe_hlt=%b fe_dreq_raw=0x%h top_dreq_raw=0x%h ic_dreq_raw=0x%h ic_areq_raw=0x%h lsu_areq_raw=0x%h",
                     $time,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.i_frontend.instr_queue_ready,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.i_frontend.halt_frontend_i,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.i_frontend.icache_dreq_o,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.icache_dreq_if_cache,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_cva6_icache.dreq_i,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_cva6_icache.areq_o,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.icache_areq_i);
            $display("[CFG_DIAG] T=%0t plen=%0d vlen=%0d n_exec=%0d core_rst_ni=%b boot_addr=0x%h csr_priv=%0d ic_flush_cnt=%0d fe_vaddr=0x%h fe_req=%b fe_paddr=0x%h fe_val=%b ic_st=%0d ic_dreq=%b ic_vaddr=0x%h",
                     $time,
                     CVA6Cfg.PLEN,
                     CVA6Cfg.VLEN,
                     CVA6Cfg.NrExecuteRegionRules,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.rst_ni,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.boot_addr_i,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.csr_regfile_i.priv_lvl_q,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_cva6_icache.flush_cnt_q,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.icache_areq_i.fetch_vaddr,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.icache_areq_i.fetch_req,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.icache_areq_o.fetch_paddr,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.icache_areq_o.fetch_valid,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_cva6_icache.state_q,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_cva6_icache.dreq_i.req,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_cva6_icache.dreq_i.vaddr);
            $display("[MMU_DIAG] T=%0t f_ex_val=%b f_ex_cause=%0d f_ex_tval=0x%h mmu_ex_val=%b en_trans=%b priv_lvl=%0d pmp_allow=%b match_exec=%b pmpcfg0=0x%h",
                     $time,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.icache_areq_o.fetch_exception.valid,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.icache_areq_o.fetch_exception.cause,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.icache_areq_o.fetch_exception.tval,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.pmp_icache_areq_i.fetch_exception.valid,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.gen_mmu.i_cva6_mmu.enable_translation_i,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.gen_mmu.i_cva6_mmu.priv_lvl_i,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.pmp_if_allow,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.match_any_execute_region,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.gen_mmu.i_cva6_mmu.pmpcfg_i[0]);
            $display("[FE_DIAG] T=%0t npc_q=0x%h npc_rst_load_q=%b ic_val_q=%b if_rdy=%b dreq_req=%b dreq_vaddr=0x%h bits_dreq=%0d",
                     $time,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.i_frontend.npc_q,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.i_frontend.npc_rst_load_q,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.i_frontend.icache_valid_q,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.i_frontend.if_ready,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.i_frontend.icache_dreq_o.req,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.i_frontend.icache_dreq_o.vaddr,
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.i_frontend.icache_dreq_o));
        end

        if ($time == 10) begin
            $display("[HPD_CFG_DIAG] user_addrW=%0d cfg_addrW=%0d sub_req_w=%0d arb_req_i_w=%0d",
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.HPDcacheUserCfg.memAddrWidth,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.HPDcacheCfg.u.memAddrWidth,
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.dcache_read),
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.dcache_read_i));
        end
        if ($time == 5330 || $time == 5350) begin
            $display("[MEM_REQ_DIAG] T=%0t miss_paddr=0x%h w_w=%0d w_addr=0x%h w_raw=0x%h r_w=%0d r_addr=0x%h r_raw=0x%h arb_w=%0d arb_addr=0x%h",
                     $time,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.icache_miss_i.paddr,
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.icache_miss_req_wdata),
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.icache_miss_req_wdata.mem_req_addr,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.icache_miss_req_wdata,
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.icache_miss_req_rdata),
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.icache_miss_req_rdata.mem_req_addr,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.icache_miss_req_rdata,
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.mem_req_read_arb),
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.mem_req_read_arb.mem_req_addr);
        end

        if ($time == 5350) begin
            $display("[XBAR_INT_DIAG] T=%0t dec_ar=%0d dec_err=%b ar_sel=%0d demux_in_val=%b demux_out8_val=%b mux8_in0_val=%b mux8_out_val=%b",
                     $time,
                     zetraa_dut.i_axi_xbar.i_xbar.gen_slv_port_demux[0].dec_ar,
                     zetraa_dut.i_axi_xbar.i_xbar.gen_slv_port_demux[0].dec_ar_error,
                     zetraa_dut.i_axi_xbar.i_xbar.gen_slv_port_demux[0].slv_ar_select,
                     zetraa_dut.i_axi_xbar.i_xbar.gen_slv_port_demux[0].i_axi_demux.slv_req_i.ar_valid,
                     zetraa_dut.i_axi_xbar.i_xbar.gen_slv_port_demux[0].i_axi_demux.mst_reqs_o[8].ar_valid,
                     zetraa_dut.i_axi_xbar.i_xbar.gen_mst_port_mux[8].i_axi_mux.slv_reqs_i[0].ar_valid,
                     zetraa_dut.i_axi_xbar.i_xbar.gen_mst_port_mux[8].i_axi_mux.mst_req_o.ar_valid);
            $display("[XBAR_HOP_DIAG] T=%0t slv0_val=%b slv0_addr=0x%h xbar_in_val=%b xbar_in_addr=0x%h xbar_in_rdy=%b xbar_out8_val=%b xbar_out8_addr=0x%h mst8_val=%b mst8_addr=0x%h",
                     $time,
                     zetraa_dut.slave[0].ar_valid,
                     zetraa_dut.slave[0].ar_addr,
                     zetraa_dut.i_axi_xbar.i_xbar.slv_ports_req_i[0].ar_valid,
                     zetraa_dut.i_axi_xbar.i_xbar.slv_ports_req_i[0].ar.addr,
                     zetraa_dut.i_axi_xbar.i_xbar.slv_ports_resp_o[0].ar_ready,
                     zetraa_dut.i_axi_xbar.i_xbar.mst_ports_req_o[8].ar_valid,
                     zetraa_dut.i_axi_xbar.i_xbar.mst_ports_req_o[8].ar.addr,
                     zetraa_dut.master[8].ar_valid,
                     zetraa_dut.master[8].ar_addr);
            $display("[AXI_WIDTH_DIAG] T=%0t arb_req_w=%0d read_ar_w=%0d read_ar_hex=0x%h arb_ar_w=%0d arb_ar_hex=0x%h core_w=%0d core_raw=0x%h top_w=%0d top_raw=0x%h req_addr=0x%h",
                     $time,
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.axi_req_o),
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.i_hpdcache_mem_to_axi_read.axi_ar_o),
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.i_hpdcache_mem_to_axi_read.axi_ar_o,
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.axi_req_o.ar),
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.axi_req_o.ar,
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.noc_req_o),
                     zetraa_dut.i_zetraa_core.i_zetraa_core.noc_req_o,
                     $bits(zetraa_dut.axi_ariane_req),
                     zetraa_dut.axi_ariane_req,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.i_hpdcache_mem_to_axi_read.req_i.mem_req_addr);
        end

        // [AXI_AR]
        if (rst_ni && zetraa_dut.axi_ariane_req.ar_valid === 1'b1 && axi_ar_cnt < 15) begin
            $display("[AXI_AR] T=%0t m_ar_addr=0x%h m_ar_rdy=%b rom_ar_val=%b rom_ar_rdy=%b b_req=%b b_addr=0x%h",
                     $time,
                     zetraa_dut.axi_ariane_req.ar.addr,
                     zetraa_dut.axi_ariane_resp.ar_ready,
                     zetraa_dut.master[8].ar_valid,
                     zetraa_dut.master[8].ar_ready,
                     zetraa_dut.i_bootrom.req_i,
                     zetraa_dut.i_bootrom.addr_i);
            axi_ar_cnt = axi_ar_cnt + 1;
        end

        if (rst_ni && $time >= 5350 && $time <= 5650) begin
            $display("[R_RET_DIAG] T=%0t m_r_val=%b m_r_rdy=%b arb_r_val=%b arb_r_rdy=%b ic_ack=%b ic_rtrn=%b fe_val=%b c_ack=%b",
                     $time,
                     zetraa_dut.axi_ariane_resp.r_valid,
                     zetraa_dut.axi_ariane_req.r_ready,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.axi_resp_i.r_valid,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.axi_req_o.r_ready,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_cva6_icache.mem_data_ack_i,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_cva6_icache.mem_rtrn_vld_i,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.fetch_valid_if_id,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.commit_ack);
        end

        if (rst_ni && $time == 5350) begin
            $display("[AR_ID_DIAG] T=%0t rdtxid=0x%h ic_tid=0x%h wdata_id=0x%h arb_id=0x%h m2a_id=0x%h m_ar_id=0x%h",
                     $time,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_cva6_icache.RdTxId,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_cva6_icache.mem_data_o.tid,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.icache_miss_req_wdata.mem_req_id,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.mem_req_read_arb.mem_req_id,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.i_hpdcache_mem_to_axi_read.axi_ar_o.id,
                     zetraa_dut.axi_ariane_req.ar.id);
            $display("[XBAR_ID_DIAG] T=%0t w=(%0d,%0d) s0_ar=0x%h m8_ar=0x%h s0_r=0x%h m8_r=0x%h",
                     $time,
                     $bits(zetraa_dut.slave[0].ar_id),
                     $bits(zetraa_dut.master[8].ar_id),
                     zetraa_dut.slave[0].ar_id,
                     zetraa_dut.master[8].ar_id,
                     zetraa_dut.slave[0].r_id,
                     zetraa_dut.master[8].r_id);
        end

        if (rst_ni && ($time == 5370 || $time == 5390)) begin
            $display("[XBAR_ID_DIAG] T=%0t w=(%0d,%0d) s0_ar=0x%h m8_ar=0x%h s0_r=0x%h m8_r=0x%h",
                     $time,
                     $bits(zetraa_dut.slave[0].ar_id),
                     $bits(zetraa_dut.master[8].ar_id),
                     zetraa_dut.slave[0].ar_id,
                     zetraa_dut.master[8].ar_id,
                     zetraa_dut.slave[0].r_id,
                     zetraa_dut.master[8].r_id);
            $display("[ARB_DIAG] T=%0t w=(%0d,%0d,id=%0d,usr=%0d) ic_id=0x%h m_ar_id=0x%h axi_r_id=0x%h mem_r_id=0x%h sel=%b dmux_rdy=%b rdy_arb0=%b m_rdy_i=%b m_rdy_o=%b wok=%b",
                     $time,
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.axi_resp_i.r),
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.i_hpdcache_mem_to_axi_read.axi_r_i),
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.i_hpdcache_mem_to_axi_read.axi_r_i.id),
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.i_hpdcache_mem_to_axi_read.axi_r_i.user),
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.icache_miss_id_i,
                     zetraa_dut.axi_ariane_req.ar.id,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.i_hpdcache_mem_to_axi_read.axi_r_i.id,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.mem_resp_read.mem_resp_r_id,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.i_mem_resp_read_demux.mem_resp_demux_sel,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.i_mem_resp_read_demux.mem_resp_demux_ready,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.mem_resp_read_ready_arb[0],
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.i_hpdcache_mem_to_axi_read.resp_ready_i,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.i_hpdcache_mem_to_axi_read.axi_r_ready_o,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.icache_miss_resp_wok);
            $display("[RT_DIAG] T=%0t w_rt=%0d dmux_w_rt=%0d depth=%0d dmux_depth=%0d dmux_id_w=%0d rt0=%0d rt32=%0d dmux_rt32=%0d",
                     $time,
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.mem_resp_read_rt),
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.i_mem_resp_read_demux.mem_resp_rt_i),
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.MEM_RESP_RT_DEPTH,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.i_mem_resp_read_demux.RT_DEPTH,
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.i_mem_resp_read_demux.mem_resp_id_i),
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.mem_resp_read_rt[0],
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.mem_resp_read_rt[32],
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.i_mem_resp_read_demux.mem_resp_rt_i[32]);
            $display("[NOC_RESP_DIAG] T=%0t w=(%0d,%0d,%0d,%0d,%0d) rval=(%b,%b,%b,%b,%b) rdata=(0x%h,0x%h,0x%h,0x%h,0x%h) raw=(0x%h,0x%h)",
                     $time,
                     $bits(zetraa_dut.axi_ariane_resp),
                     $bits(zetraa_dut.i_zetraa_core.noc_resp_i),
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.noc_resp_i),
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.noc_resp_i),
                     $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.axi_resp_i),
                     zetraa_dut.axi_ariane_resp.r_valid,
                     zetraa_dut.i_zetraa_core.noc_resp_i.r_valid,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.noc_resp_i.r_valid,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.noc_resp_i.r_valid,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.axi_resp_i.r_valid,
                     zetraa_dut.axi_ariane_resp.r.data,
                     zetraa_dut.i_zetraa_core.noc_resp_i.r.data,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.noc_resp_i.r.data,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.noc_resp_i.r.data,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_axi_arbiter.axi_resp_i.r.data,
                     zetraa_dut.axi_ariane_resp,
                     zetraa_dut.i_zetraa_core.noc_resp_i);
        end

        // [AXI_R]
        if (rst_ni && zetraa_dut.axi_ariane_resp.r_valid === 1'b1 && axi_r_cnt < 15) begin
            $display("[AXI_R] T=%0t m_r_data=0x%h m_r_resp=0x%h m_r_last=%b",
                     $time,
                     zetraa_dut.axi_ariane_resp.r.data,
                     zetraa_dut.axi_ariane_resp.r.resp,
                     zetraa_dut.axi_ariane_resp.r.last);
            axi_r_cnt = axi_r_cnt + 1;
        end

        // [IC_REQ]
        if (rst_ni && zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_cva6_icache.mem_data_req_o === 1'b1 && ic_req_cnt < 10) begin
            $display("[IC_REQ] T=%0t st_q=%0d f_val=%b f_ex=%b f_paddr=0x%h ack_i=%b",
                     $time,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_cva6_icache.state_q,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_cva6_icache.areq_i.fetch_valid,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_cva6_icache.areq_i.fetch_exception.valid,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_cva6_icache.areq_i.fetch_paddr,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.gen_cache_hpd.i_cache_subsystem.i_cva6_icache.mem_data_ack_i);
            ic_req_cnt = ic_req_cnt + 1;
        end

        // [COMMIT_EVT]
        if (rst_ni && (|zetraa_dut.i_zetraa_core.i_zetraa_core.commit_ack || |zetraa_dut.i_zetraa_core.i_zetraa_core.fetch_valid_if_id) && commit_evt_cnt < 15) begin
            $display("[COMMIT_EVT] T=%0t fe_val=%b fe_pc=0x%h fe_insn=0x%h ack=%b rvfi_val=%b",
                     $time,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.fetch_valid_if_id,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.fetch_entry_if_id[0].address,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.fetch_entry_if_id[0].instruction,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.commit_ack,
                     zetraa_dut.rvfi_instr[0].valid);
            commit_evt_cnt = commit_evt_cnt + 1;
        end

        if ($time > 5300 && (zetraa_dut.i_zetraa_core.i_zetraa_core.commit_ack[0] === 1'b1 || zetraa_dut.i_zetraa_core.i_zetraa_core.commit_stage_i.commit_ack_o[0] === 1'b1)) begin
            if (commit_cnt < 10) begin
                $display("[COMMIT] T=%0t ack=%b ack_o=%b pc=0x%h rvfi_valid=%b rvfi_pc=0x%h rvfi_insn=0x%h",
                         $time,
                         zetraa_dut.i_zetraa_core.i_zetraa_core.commit_ack[0],
                         zetraa_dut.i_zetraa_core.i_zetraa_core.commit_stage_i.commit_ack_o[0],
                         zetraa_dut.i_zetraa_core.i_zetraa_core.commit_stage_i.commit_instr_i[0].pc,
                         zetraa_dut.rvfi_instr[0].valid,
                         zetraa_dut.rvfi_instr[0].pc_rdata,
                         zetraa_dut.rvfi_instr[0].insn);
                commit_cnt = commit_cnt + 1;
            end
            $fflush(zetraa_dut.i_rvfi_tracer.f);
        end
    end

    // First issue monitor
    bit seen_iss = 0;
    always @(posedge clk_i) begin
        if (rst_ni && zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.mem_n[0].issued && !seen_iss) begin
            seen_iss = 1;
            $display("[ISSUE_FIRST] T=%0t dec_valid=%b dec_ack=%b flush=%b id_valid=%b id_pc=0x%h",
                     $time,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.decoded_instr_valid_i[0],
                     zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.decoded_instr_ack_o[0],
                     zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.flush_unissued_instr_i,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.issue_entry_id_issue[0].valid,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.issue_entry_id_issue[0].pc);
        end
    end

    // Probes at T = 20 ns, T = 120 ns, T = 5400 ns and T = 7000 ns
    initial begin
        #(20);
        $display("[MAP8_CHECK] w_am=%0d w_xbar=%0d w_ixbar=%0d raw_am=0x%h",
                 $bits(zetraa_dut.addr_map),
                 $bits(zetraa_dut.i_axi_xbar.addr_map_i),
                 $bits(zetraa_dut.i_axi_xbar.i_xbar.addr_map_i),
                 zetraa_dut.addr_map);
        $display("[MAP8_CHECK_ELEMS] am8_start=0x%h xbar8_start=0x%h ixbar8_start=0x%h",
                 zetraa_dut.addr_map[8].start_addr,
                 zetraa_dut.i_axi_xbar.addr_map_i[8].start_addr,
                 zetraa_dut.i_axi_xbar.i_xbar.addr_map_i[8].start_addr);
        $display("[MAP8_CHECK] map8_start=0x%h map8_end=0x%h", zetraa_dut.i_axi_xbar.i_xbar.addr_map_i[8].start_addr, zetraa_dut.i_axi_xbar.i_xbar.addr_map_i[8].end_addr);
        $display("[DIAG_T20] rst=%b mn0_iss=%b mq0_iss=%b",
                 rst_ni,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.mem_n[0].issued,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.mem_q[0].issued);
        #(100);
        $display("[DIAG_T120] rst=%b mn0_iss=%b mq0_iss=%b",
                 rst_ni,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.mem_n[0].issued,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.mem_q[0].issued);
        #(5280);
        $display("[DIAG_WIDTHS] bits_deco=%0d bits_iss_sbe=%0d bits_iss_n=%0d",
                 $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.id_stage_i.decoded_instruction[0]),
                 $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.id_stage_i.issue_n[0].sbe),
                 $bits(zetraa_dut.i_zetraa_core.i_zetraa_core.id_stage_i.issue_n[0]));
        $display("[DIAG_CHAIN_5400]");
        $display("  1. deco_pc=0x%h", zetraa_dut.i_zetraa_core.i_zetraa_core.id_stage_i.decoded_instruction[0].pc);
        $display("  2. iss_n_pc=0x%h", zetraa_dut.i_zetraa_core.i_zetraa_core.id_stage_i.issue_n[0].sbe.pc);
        $display("  3. iss_q_pc=0x%h", zetraa_dut.i_zetraa_core.i_zetraa_core.id_stage_i.issue_q[0].sbe.pc);
        $display("  4. iss_o_pc=0x%h", zetraa_dut.i_zetraa_core.i_zetraa_core.id_stage_i.issue_entry_o[0].pc);
        $display("  5. top_id_pc=0x%h", zetraa_dut.i_zetraa_core.i_zetraa_core.issue_entry_id_issue[0].pc);
        $display("[DIAG_T5400] id_out_valid=%b id_out_pc=0x%h id_out_fu=%0d mn0_pc=0x%h mn0_iss=%b mq0_pc=0x%h mq0_iss=%b",
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_entry_id_issue[0].valid,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_entry_id_issue[0].pc,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_entry_id_issue[0].fu,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.mem_n[0].sbe.pc,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.mem_n[0].issued,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.mem_q[0].sbe.pc,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.mem_q[0].issued);
        #(1600);
        $display("[DIAG_T7000] mq0_pc=0x%h mq1_pc=0x%h mq2_pc=0x%h mq3_pc=0x%h mq4_pc=0x%h",
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.mem_q[0].sbe.pc,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.mem_q[1].sbe.pc,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.mem_q[2].sbe.pc,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.mem_q[3].sbe.pc,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.mem_q[4].sbe.pc);
        $display("[DIAG_T7000_ISS] mq0_iss=%b mq1_iss=%b mq2_iss=%b mq3_iss=%b mq4_iss=%b",
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.mem_q[0].issued,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.mem_q[1].issued,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.mem_q[2].issued,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.mem_q[3].issued,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.issue_stage_i.i_scoreboard.mem_q[4].issued);
        #(400);
        $fflush(zetraa_dut.i_rvfi_tracer.f);
        $fclose(zetraa_dut.i_rvfi_tracer.f);
    end

    // Exit monitoring
    initial begin
        forever begin
            @(posedge clk_i);
            if (rst_ni && exit_o[0]) begin
                if ((exit_o >> 1) != 0) begin
                    $display("*** FAILED *** (tohost = %0d)", (exit_o >> 1));
                    $fatal(1);
                end else begin
                    $display("*** SUCCESS *** (tohost = 0)");
                    $finish();
                end
            end
        end
    end
endmodule
