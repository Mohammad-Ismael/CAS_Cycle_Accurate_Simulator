with open("/app/scripts/ariane_cas_tb.sv", "r") as f:
    content = f.read()

target = "zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.gen_mmu.i_cva6_mmu.pmpcfg_i[0]);"
replacement = target + """
            $display("[PMP_DETAIL_DIAG] T=%0t pmp_allow=%b priv_lvl_i=%0d addr_i=0x%h access_type=%0d match=%b conf_i0=0x%h conf_i0_lock=%b",
                     $time,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.i_pmp_if.allow_o,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.i_pmp_if.priv_lvl_i,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.i_pmp_if.addr_i,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.i_pmp_if.access_type_i,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.i_pmp_if.gen_pmp.match,
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.i_pmp_if.conf_i[0],
                     zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.i_pmp_if.conf_i[0].locked);"""

if target in content:
    content = content.replace(target, replacement, 1)
    with open("/app/scripts/ariane_cas_tb.sv", "w") as f:
        f.write(content)
    print("SUCCESS: patched")
else:
    print("FAILED: target not found")
