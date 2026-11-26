set pagination off
b StratifiedScheduler::propagate_comb
run -f /app/scripts/zetraa_cas_files.f --top ariane_cas_tb --max-cycles 1 --no-vcd --no-debug -sv_lib /app/projects/ZETRAA/work-dpi/ariane_dpi.so +PRELOAD=/app/projects/ZETRAA/verif/sim/out_2026-11-23/directed_tests/hello_world.o +elf_file=/app/projects/ZETRAA/verif/sim/out_2026-11-23/directed_tests/hello_world.o
python
netlist = gdb.parse_and_eval('this->engine.netlist')
sz = int(netlist['_M_impl']['_M_finish'] - netlist['_M_impl']['_M_start'])
print(f'NETLIST SIZE: {sz}')
for i in range(sz):
    net = netlist['_M_impl']['_M_start'][i]
    name = net['name']['_M_dataplus']['_M_p'].string()
    if 'icache_miss_req_wdata' in name or 'i_axi_arbiter.icache_miss_i' in name:
        tname = net['type_name']['_M_dataplus']['_M_p'].string()
        print(f'NET {i}: {name} w={int(net["width"])} type={tname}')

rcas = gdb.parse_and_eval('this->engine.resolved_continuous_assigns')
rca_sz = int(rcas['_M_impl']['_M_finish'] - rcas['_M_impl']['_M_start'])
print(f'RCAS SIZE: {rca_sz}')
for i in range(rca_sz):
    rca = rcas['_M_impl']['_M_start'][i]
    nid = int(rca['target']['net_id'])
    if nid < sz:
        name = netlist['_M_impl']['_M_start'][nid]['name']['_M_dataplus']['_M_p'].string()
        if 'icache_miss_req_wdata' in name:
            print(f'RCA {i}: target={name} off={int(rca["target"]["offset"])} w={int(rca["target"]["width"])}')
end
quit
