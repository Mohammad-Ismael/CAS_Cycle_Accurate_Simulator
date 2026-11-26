with open("/app/scripts/ariane_cas_tb.sv", "r") as f:
    content = f.read()

target = "    // Commit monitor"
replacement = """    always @(zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.pmp_if_allow or
             zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.i_pmp_if.allow_o) begin
        $display("[PMP_TRACK] T=%0t pmp_if_allow=%b i_pmp_if.allow_o=%b",
                 $time,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.pmp_if_allow,
                 zetraa_dut.i_zetraa_core.i_zetraa_core.ex_stage_i.lsu_i.i_pmp_data_if.i_pmp_if.allow_o);
    end

    // Commit monitor"""

if target in content:
    content = content.replace(target, replacement, 1)
    with open("/app/scripts/ariane_cas_tb.sv", "w") as f:
        f.write(content)
    print("SUCCESS: patched pmp_track")
else:
    print("FAILED: target not found")
