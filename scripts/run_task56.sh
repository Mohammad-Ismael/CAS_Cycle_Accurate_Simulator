#!/bin/bash
cmake --build /app/CAS/build -j4 || exit 1
echo "=== SIMULATION ==="
rm -f /app/trace_rvfi_hart_00.dasm /tmp/sim750_events.log
timeout 450s /app/CAS/build/bin/cas -f /app/scripts/zetraa_cas_files.f --top ariane_cas_tb --max-cycles 750 --no-vcd --no-debug -sv_lib /app/projects/ZETRAA/work-dpi/ariane_dpi.so +PRELOAD=/app/projects/ZETRAA/verif/sim/out_2026-11-23/directed_tests/hello_world.o +elf_file=/app/projects/ZETRAA/verif/sim/out_2026-11-23/directed_tests/hello_world.o > /tmp/sim750_events.log 2>&1
echo "EXIT_CODE=$?"
python3 -c "
import re
with open('/tmp/sim750_events.log') as f:
    for line in f:
        if re.search(r'MAP8_CHECK|CFG_DIAG|MMU_DIAG|AXI_AR|AXI_R|IC_REQ|COMMIT_EVT|fatal|error|assert', line, re.I):
            print(line, end='')
" | head -n 60
tail -n 20 /tmp/sim750_events.log
wc -l /app/trace_rvfi_hart_00.dasm 2>/dev/null || echo "NO_TRACE_FILE"
head -n 30 /app/trace_rvfi_hart_00.dasm 2>/dev/null || true
