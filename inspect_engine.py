import gdb

gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("break scheduler.cpp:2478")
gdb.execute("condition 1 s_idx == 10067")
gdb.execute("run -f /app/scripts/zetraa_cas_files.f --top ariane_cas_tb --max-cycles 1 --no-vcd --no-debug -sv_lib /app/projects/ZETRAA/work-dpi/ariane_dpi.so +PRELOAD=/app/projects/ZETRAA/verif/sim/out_2026-11-23/directed_tests/hello_world.o +elf_file=/app/projects/ZETRAA/verif/sim/out_2026-11-23/directed_tests/hello_world.o")

count = 0
while True:
    try:
        frame = gdb.selected_frame()
        count += 1
        s_idx = int(frame.read_var("s_idx"))
        bind = frame.read_var("bind")
        src_id = int(bind['src']['net_id'])
        dst_id = int(bind['dst']['net_id'])
        sched = frame.read_var("this")
        iters = int(frame.read_var("iters"))
        sim_time = int(sched['current_sim_time'])
        engine = sched['engine']
        src_val = engine['netlist']['_M_impl']['_M_start'][src_id]['value']
        dst_val = engine['netlist']['_M_impl']['_M_start'][dst_id]['value']
        print(f"[{count}] T={sim_time} iters={iters} s_idx={s_idx}: src({src_id})={int(src_val['aval'])} dst({dst_id})={int(dst_val['aval'])}")
        gdb.execute("next")
        gdb.execute("next")
        gdb.execute("next")
        gdb.execute("continue")
    except Exception as e:
        print("Done or error:", e)
        break

gdb.execute("quit")
