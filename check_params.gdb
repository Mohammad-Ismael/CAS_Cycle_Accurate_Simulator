set pagination off
b StratifiedScheduler::propagate_comb
run -f /app/scripts/zetraa_cas_files.f --top ariane_cas_tb --max-cycles 1 --no-vcd --no-debug -sv_lib /app/projects/ZETRAA/work-dpi/ariane_dpi.so +PRELOAD=/app/projects/ZETRAA/verif/sim/out_2026-11-23/directed_tests/hello_world.o +elf_file=/app/projects/ZETRAA/verif/sim/out_2026-11-23/directed_tests/hello_world.o
python
pvm = gdb.parse_and_eval('this->engine.param_val_map')
print("Searching param_val_map...")
bkts = int(pvm['_M_h']['_M_bucket_count'])
for b in range(bkts):
    node = pvm['_M_h']['_M_buckets'][b]
    while node != 0:
        p = node.cast(gdb.lookup_type('std::__detail::_Hash_node<std::pair<const std::string, LogicVal>, true>').pointer())
        k = p['_M_storage']['_M_storage']['first']['_M_dataplus']['_M_p'].string()
        if 'HPDcache' in k or 'mem_addr_t' in k:
            v = p['_M_storage']['_M_storage']['second']
            w = int(v['width'])
            val = int(v['aval'])
            print(f"PARAM {k}: w={w} val=0x{val:x}")
        node = node['_M_nxt']
end
quit
