import re, glob
for path in glob.glob("/app/projects/ZETRAA/core/**/*.sv", recursive=True):
    with open(path, "r", errors="ignore") as f:
        content = f.read()
        if "icache_dreq_t" in content:
            m = re.findall(r"struct\s+packed\s*\{[^}]*\}\s*icache_dreq_t|typedef\s+struct\s+packed\s*\{[^}]*\}\s*icache_dreq_t|icache_dreq_t\s*=\s*struct\s+packed\s*\{[^}]*\}", content)
            if m:
                print(path, m)
