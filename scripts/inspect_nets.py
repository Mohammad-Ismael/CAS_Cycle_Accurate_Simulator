import subprocess
import json

# Let's run cas --elab-only with -d (debug dump) or inspect the nets
proc = subprocess.run([
    "/app/CAS/build/bin/cas",
    "-f", "/app/scripts/zetraa_cas_files.f",
    "--top", "ariane_cas_tb",
    "--elab-only",
], capture_output=True, text=True)

out = proc.stdout + proc.stderr
for line in out.splitlines():
    if any(k in line for k in ["icache_dreq", "icache_areq"]):
        print(line)
