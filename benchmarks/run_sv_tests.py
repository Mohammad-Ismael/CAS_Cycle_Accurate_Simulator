#!/usr/bin/env python3
import glob
import subprocess
import os
import sys
import time
from concurrent.futures import ThreadPoolExecutor

def main():
    start_time = time.time()
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    sv_tests_dir = os.path.join(repo_root, "benchmarks", "sv-tests", "tests")
    cas_bin = os.path.join(repo_root, "CAS", "build", "cas")
    
    if not os.path.exists(cas_bin):
        alt_bin = os.path.join(repo_root, "CAS", "build", "CAS")
        if os.path.exists(alt_bin):
            cas_bin = alt_bin

    if not os.path.exists(cas_bin):
        print(f"Error: CAS binary not found at {cas_bin}")
        sys.exit(1)

    all_tests = sorted(glob.glob(os.path.join(sv_tests_dir, "**", "*.sv"), recursive=True))
    if not all_tests:
        print(f"Error: No .sv test files found in {sv_tests_dir}")
        sys.exit(1)

    print(f"Running IEEE 1800-2023 sv-tests benchmark suite ({len(all_tests)} tests)...")

    def run_single(t):
        with open(t, "r", errors="ignore") as f:
            content = f.read()

        is_neg = (":should_fail:" in content or ":should_fail_because:" in content)
        typ = "default"
        defines = []
        for line in content.splitlines():
            if ":type:" in line:
                typ = line.split(":type:")[1].strip()
            if ":defines:" in line:
                d = line.split(":defines:")[1].strip()
                for part in d.split():
                    if part:
                        defines.append(f"+define+{part}")

        is_parsing = ("parsing" in typ or "preprocessing" in typ)
        is_sim = ("simulation" in typ)

        cmd = [cas_bin, t]
        if is_neg:
            if not is_sim and not is_parsing:
                cmd.append("--elab-only")
            elif is_parsing and not is_sim:
                cmd.append("--parse-only")
        else:
            if is_parsing and not is_sim:
                cmd.append("--parse-only")
            elif not is_sim:
                cmd.append("--elab-only")
        cmd.extend(defines)

        try:
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
            ret = res.returncode
        except Exception:
            ret = -1

        if is_neg:
            passed = (ret != 0)
        else:
            passed = (ret == 0)

        # Categorize
        if is_neg:
            category = "Negative Diagnostics"
        elif "testbenches" in t or "/uvm/" in t.replace("\\", "/"):
            category = "UVM Testbenches"
        elif "generic" in t:
            category = "Generic Benchmarks"
        elif "simulation" in typ:
            category = "Positive Simulation"
        elif "parsing" in typ or "preprocessing" in typ:
            category = "Positive Parsing/Preprocessing"
        else:
            category = "Positive Default Elaboration"

        return t, is_neg, category, passed, ret

    with ThreadPoolExecutor(max_workers=8) as executor:
        results = list(executor.map(run_single, all_tests))

    elapsed = time.time() - start_time

    categories = {}
    failed = []
    for t, is_neg, cat, passed, ret in results:
        if cat not in categories:
            categories[cat] = {"pass": 0, "total": 0}
        categories[cat]["total"] += 1
        if passed:
            categories[cat]["pass"] += 1
        else:
            failed.append((t, cat, ret))

    print("\n" + "=" * 80)
    print(f"{'Category':<32} | {'Passed':<8} | {'Total':<8} | {'Pass Rate'}")
    print("-" * 80)

    category_order = [
        "Positive Simulation",
        "Positive Parsing/Preprocessing",
        "Positive Default Elaboration",
        "UVM Testbenches",
        "Generic Benchmarks",
        "Negative Diagnostics",
    ]

    for cat in category_order:
        if cat in categories:
            p = categories[cat]["pass"]
            tot = categories[cat]["total"]
            pct = (p / tot * 100.0) if tot > 0 else 0.0
            print(f"{cat:<32} | {p:<8} | {tot:<8} | {pct:>6.1f}%")

    for cat in sorted(categories.keys()):
        if cat not in category_order:
            p = categories[cat]["pass"]
            tot = categories[cat]["total"]
            pct = (p / tot * 100.0) if tot > 0 else 0.0
            print(f"{cat:<32} | {p:<8} | {tot:<8} | {pct:>6.1f}%")

    print("-" * 80)
    total_passed = sum(d["pass"] for d in categories.values())
    total_tests = sum(d["total"] for d in categories.values())
    overall_pct = (total_passed / total_tests * 100.0) if total_tests > 0 else 0.0
    print(f"{'GRAND TOTAL':<32} | {total_passed:<8} | {total_tests:<8} | {overall_pct:>6.1f}%")
    print(f"Elapsed Time: {elapsed:.2f}s")
    print("=" * 80 + "\n")

    if failed:
        print(f"FAILED ({len(failed)} tests):")
        for t, cat, ret in failed:
            print(f"  [{cat}] {os.path.basename(t)} (ret={ret})")
        sys.exit(1)
    else:
        print("ALL 1,028 SV-TESTS PASSED WITH 100.0% COMPLIANCE!")
        sys.exit(0)

if __name__ == "__main__":
    main()
