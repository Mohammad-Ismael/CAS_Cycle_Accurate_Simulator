import os
import re
import shlex

repo_dir = "/app/projects/ZETRAA"
dat_path = f"{repo_dir}/work-ver/Variane_testharness__verFiles.dat"

defines = []
incdirs = []
sv_files = []

with open(dat_path, "r", errors="ignore") as f:
    for line in f:
        line = line.strip()
        if line.startswith("C "):
            cmd_str = line[2:].strip()
            if cmd_str.startswith('"') and cmd_str.endswith('"'):
                cmd_str = cmd_str[1:-1]
            # unescape \"
            cmd_str = cmd_str.replace('\\"', '"')
            tokens = shlex.split(cmd_str)
            for tok in tokens:
                if tok.startswith("+incdir+"):
                    p = tok[len("+incdir+"):]
                    if not os.path.isabs(p):
                        p = os.path.normpath(os.path.join(repo_dir, p))
                    if f"+incdir+{p}" not in incdirs:
                        incdirs.append(f"+incdir+{p}")
                elif tok.startswith("-I"):
                    p = tok[2:]
                    if not os.path.isabs(p):
                        p = os.path.normpath(os.path.join(repo_dir, p))
                    if f"+incdir+{p}" not in incdirs:
                        incdirs.append(f"+incdir+{p}")
                elif tok.startswith("+define+"):
                    if tok not in defines:
                        defines.append(tok)
                elif tok.startswith("-D"):
                    d = f"+define+{tok[2:]}"
                    if d not in defines:
                        defines.append(d)
        elif line.startswith("S "):
            # S <size> <inode> ... "filepath"
            q_pos = line.find('"')
            if q_pos != -1 and line.endswith('"'):
                p = line[q_pos+1:-1]
                if not os.path.isabs(p):
                    p = os.path.normpath(os.path.join(repo_dir, p))
                if p.endswith((".sv", ".v")) and not p.endswith(".vlt"):
                    if p not in sv_files and os.path.exists(p):
                        sv_files.append(p)

print(f"Extracted {len(defines)} defines, {len(incdirs)} incdirs, {len(sv_files)} SV files.")

# Also ensure standard simulation defines and required include directories for ZETRAA
for d in ["+define+PRELOAD=1", "+define+VERILATOR=1", "+define+SYNTHESIS"]:
    if d not in defines:
        defines.append(d)

extra_incdirs = [
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
for inc in extra_incdirs:
    if inc not in incdirs:
        incdirs.append(inc)

out_path = "/app/scripts/zetraa_cas_files.f"
with open(out_path, "w") as f:
    f.write("# ZETRAA clean CAS filelist\n")
    for d in defines:
        f.write(f"{d}\n")
    for inc in incdirs:
        f.write(f"{inc}\n")
    for sv in sv_files:
        f.write(f"{sv}\n")

print(f"Wrote {out_path} successfully.")
