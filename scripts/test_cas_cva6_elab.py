import os
import subprocess
import sys

repo_dir = "/app/projects/ZETRAA"
cas_bin = "/app/CAS/build/bin/cas"

with open(f"{repo_dir}/work-ver/Variane_testharness__ver.d") as f:
    content = f.read()

colon_pos = content.find(" : ")
deps = content[colon_pos + 3:].split()

sv_files = []
for d in deps:
    d = d.strip()
    if d.endswith((".sv", ".v")) and not d.endswith(".vlt"):
        if not os.path.isabs(d):
            d = os.path.normpath(os.path.join(repo_dir, d))
        if d not in sv_files:
            sv_files.append(d)

incdirs = [
    f"+incdir+{repo_dir}/vendor/pulp-platform/common_cells/include/",
    f"+incdir+{repo_dir}/vendor/pulp-platform/axi/include/",
    f"+incdir+{repo_dir}/corev_apu/register_interface/include/",
    f"+incdir+{repo_dir}/corev_apu/tb/common/",
    f"+incdir+{repo_dir}/core/include/",
    f"+incdir+{repo_dir}/corev_apu/axi_node/",
    f"+incdir+{repo_dir}/common/local/util/",
    f"+incdir+{repo_dir}/verif/tb/core/",
    f"+incdir+{repo_dir}/corev_apu/instr_tracing/ITI/include/",
    f"+incdir+{repo_dir}/core/cache_subsystem/hpdcache/rtl/include/",
]

defines = [
    "+define+PRELOAD=1",
    "+define+VERILATOR=1",
    "+define+SYNTHESIS",
]

cmd = [
    cas_bin,
    "--elab-only",
    "--top", "ariane_testharness",
] + defines + incdirs + sv_files

print(f"Running CAS --elab-only with {len(sv_files)} files...")
res = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
print(f"Return code: {res.returncode}")
if res.stdout:
    print("STDOUT (head):")
    for l in res.stdout.splitlines()[:20]:
        print(" ", l)
if res.stderr:
    print("STDERR (head):")
    for l in res.stderr.splitlines()[:30]:
        print(" ", l)
