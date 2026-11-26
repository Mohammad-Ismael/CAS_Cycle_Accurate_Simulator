import subprocess
import time

p = subprocess.Popen([
    '/app/CAS/build/bin/cas',
    '-f', '/app/scripts/zetraa_cas_files.f',
    '/app/scripts/ariane_cas_tb.sv',
    '--top', 'ariane_cas_tb',
    '-sv_lib', '/app/projects/ZETRAA/work-dpi/ariane_dpi.so',
    '--max-cycles', '1'
], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

time.sleep(12) # wait for elaboration to finish and enter simulation

# Attach gdb and print backtrace
gdb_out = subprocess.check_output([
    'gdb', '-batch', '-ex', 'bt', '-p', str(p.pid)
], text=True)

print("=== GDB BACKTRACE ===")
print(gdb_out)

p.kill()
