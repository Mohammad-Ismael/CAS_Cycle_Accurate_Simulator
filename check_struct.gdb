set pagination off
b StratifiedScheduler::propagate_comb
run -f /app/scripts/zetraa_cas_files.f --top ariane_cas_tb --max-cycles 1 --no-vcd --no-debug -sv_lib /app/projects/ZETRAA/work-dpi/ariane_dpi.so +PRELOAD=/app/projects/ZETRAA/verif/sim/out_2026-11-23/directed_tests/hello_world.o +elf_file=/app/projects/ZETRAA/verif/sim/out_2026-11-23/directed_tests/hello_world.o
python
struct_types = gdb.parse_and_eval('this->engine.struct_types')
print("Iterating struct_types...")
# iterate std::unordered_map
for b in range(int(struct_types['_M_h']['_M_bucket_count'])):
    node = struct_types['_M_h']['_M_buckets'][b]
    while node != 0:
        val = node.dereference()
        # val._M_storage._M_storage is std::pair<const std::string, PackedStructType>
        p = val.cast(gdb.lookup_type('std::__detail::_Hash_node<std::pair<const std::string, PackedStructType>, true>').pointer())
        first = p['_M_storage']['_M_storage']['first']['_M_dataplus']['_M_p'].string()
        st = p['_M_storage']['_M_storage']['second']
        if 'hpdcache_mem_req_t' in first:
            w = int(st['total_width'])
            print(f"STRUCT {first} total_width={w}")
            m_sz = int(st['members']['_M_impl']['_M_finish'] - st['members']['_M_impl']['_M_start'])
            for m in range(m_sz):
                mem = st['members']['_M_impl']['_M_start'][m]
                mname = mem['name']['_M_dataplus']['_M_p'].string()
                mw = int(mem['width'])
                moff = int(mem['offset'])
                print(f"  member {mname} off={moff} w={mw}")
        node = node['_M_nxt']
end
quit
