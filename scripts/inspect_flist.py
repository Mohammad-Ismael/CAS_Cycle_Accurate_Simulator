import os

repo_dir = "/app/projects/ZETRAA"

incdirs = set()
for root, dirs, files in os.walk(repo_dir):
    for f in files:
        if f.endswith((".Flist", ".cva6", ".flist")):
            p = os.path.join(root, f)
            with open(p, errors="ignore") as fp:
                for line in fp:
                    line = line.strip()
                    if line.startswith("+incdir+"):
                        idir = line[len("+incdir+"):].strip()
                        idir = idir.replace("${ZETRAA_REPO_DIR}", repo_dir)
                        idir = idir.replace("$ZETRAA_REPO_DIR", repo_dir)
                        idir = idir.replace("${HPDCACHE_DIR}", f"{repo_dir}/core/cache_subsystem/hpdcache")
                        idir = idir.replace("$HPDCACHE_DIR", f"{repo_dir}/core/cache_subsystem/hpdcache")
                        if os.path.isdir(idir):
                            incdirs.add(os.path.normpath(idir))

print(f"Discovered {len(incdirs)} include dirs from all flists:")
for d in sorted(incdirs):
    print("  +incdir+" + d)
