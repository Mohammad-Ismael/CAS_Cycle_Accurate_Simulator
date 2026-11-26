import gdb

gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("break main.cpp:504")
gdb.execute("run")

try:
    frame = gdb.selected_frame()
    engine = frame.read_var('engine')
    netlist = engine['netlist']
    net_map = engine['net_map']
    slice_bindings = engine['slice_bindings']

    num_bindings = int(slice_bindings['_M_impl']['_M_finish'] - slice_bindings['_M_impl']['_M_start'])
    print(f"Total slice_bindings: {num_bindings}")

    start_ptr = slice_bindings['_M_impl']['_M_start']
    for i in range(num_bindings):
        bind = start_ptr[i]
        src_id = int(bind['src']['net_id'])
        dst_id = int(bind['dst']['net_id'])
        src_net = netlist['_M_impl']['_M_start'][src_id]
        dst_net = netlist['_M_impl']['_M_start'][dst_id]
        src_name = src_net['name'].string()
        dst_name = dst_net['name'].string()
        if 'pmp_if_allow' in src_name or 'pmp_if_allow' in dst_name or 'i_pmp_if.allow_o' in src_name or 'i_pmp_if.allow_o' in dst_name:
            print(f"Binding {i}: src={src_name} (id={src_id}, off={bind['src']['offset']}, w={bind['src']['width']}) -> dst={dst_name} (id={dst_id}, off={bind['dst']['offset']}, w={bind['dst']['width']})")
except Exception as e:
    import traceback
    traceback.print_exc()

gdb.execute("quit")
