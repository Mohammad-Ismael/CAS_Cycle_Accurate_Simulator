#!/bin/bash
sed -i '/FN_DCACHE_TYPE_ACCESS/d; /BA_DCACHE_TYPE_ASSIGN/d; /DCACHE_TYPE_ACCESS/d; /PKG_PARAM_EVAL/d; /FN_CLOG2/d' /app/CAS/src/kernel/elaborator.cpp
cmake --build /app/CAS/build -j4
echo "=== ELAB CHECK ==="
timeout 30s /app/CAS/build/bin/cas -f /app/scripts/zetraa_cas_files.f --top ariane_cas_tb --elab-only 2>&1 | grep -E "gen_cache_hpd|gen_cache_wb|CAS-MVPD|ERROR|Error|FATAL" | head -n 20
echo "=== SIMULATION ==="
rm -f /app/trace_rvfi_hart_00.dasm /tmp/sim750_events.log
timeout 450s /app/CAS/build/bin/cas -f /app/scripts/zetraa_cas_files.f --top ariane_cas_tb --max-cycles 750 --no-vcd --no-debug -sv_lib /app/projects/ZETRAA/work-dpi/ariane_dpi.so +PRELOAD=/app/projects/ZETRAA/verif/sim/out_2026-11-23/directed_tests/hello_world.o +elf_file=/app/projects/ZETRAA/verif/sim/out_2026-11-23/directed_tests/hello_world.o > /tmp/sim750_events.log 2>&1
echo "EXIT_CODE=$?"
grep -E "MAP8_CHECK|CFG_DIAG|MMU_DIAG|AXI_AR|AXI_R|IC_REQ|COMMIT_EVT|ERROR|Error|FATAL" /tmp/sim750_events.log | head -n 60
wc -l /app/trace_rvfi_hart_00.dasm 2>/dev/null || echo "NO_TRACE_FILE"
head -n 30 /app/trace_rvfi_hart_00.dasm 2>/dev/null || true
