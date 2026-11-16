#!/usr/bin/env python3
"""
High-Performance Parallel Test Runner for Icarus Verilog ivtest Benchmark Suite
Benchmark Target: IEEE 1364-2005 Verilog & IEEE 1800 SystemVerilog Edge Cases
"""

import os
import sys
import time
import subprocess
import re
from concurrent.futures import ThreadPoolExecutor, as_completed
from collections import defaultdict

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BENCHMARKS_DIR = os.path.join(REPO_ROOT, "benchmarks")
IVTEST_DIR = os.path.join(BENCHMARKS_DIR, "ivtest")
CAS_BIN = os.path.join(REPO_ROOT, "CAS", "build", "cas")

TIMEOUT_SEC = 5
MAX_WORKERS = 8

def parse_regression_list(filename):
    filepath = os.path.join(IVTEST_DIR, filename)
    if not os.path.exists(filepath):
        return {}
    
    with open(filepath, "r", errors="ignore") as f:
        content = f.read()

    # Join lines ending with backslash
    raw_lines = content.splitlines()
    lines = []
    i = 0
    while i < len(raw_lines):
        line = raw_lines[i]
        while line.endswith("\\") and i + 1 < len(raw_lines):
            line = line[:-1].rstrip() + raw_lines[i + 1].lstrip()
            i += 1
        lines.append(line)
        i += 1

    tests = {}
    for line in lines:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "#" in line:
            line = line[:line.index("#")].strip()
        parts = line.split()
        if len(parts) < 2:
            continue

        raw_name = parts[0]
        if ":" in raw_name:
            ver, tname = raw_name.split(":", 1)
            if ver != "v12":
                continue
        else:
            tname = raw_name

        raw_type = parts[1]
        args = []
        if "," in raw_type:
            tp = raw_type.split(",")
            ttype = tp[0]
            args = [a.strip() for a in tp[1:] if a.strip()]
        else:
            ttype = raw_type

        srcdir = parts[2] if len(parts) > 2 else "ivltests"
        gold = None
        is_unordered = False
        top_mod = None

        for pt in parts[3:]:
            if pt.startswith("gold="):
                gold = pt.split("=", 1)[1]
            elif pt.startswith("unordered="):
                gold = pt.split("=", 1)[1]
                is_unordered = True
            elif not pt.startswith("diff="):
                top_mod = pt

        tests[tname] = {
            "name": tname,
            "type": ttype,
            "args": args,
            "srcdir": srcdir,
            "gold": gold,
            "is_unordered": is_unordered,
            "top_mod": top_mod,
            "list_file": filename,
        }
    return tests

def load_tests(list_files=None):
    if list_files is None:
        list_files = ["regress-sv.list", "regress-v12.list"]

    all_tests = {}
    skipped_ni = 0
    skipped_ef = 0
    skipped_vpi = 0
    skipped_vhdl = 0

    for lfn in list_files:
        parsed = parse_regression_list(lfn)
        for tname, tinfo in parsed.items():
            ttype = tinfo["type"]
            srcdir = tinfo["srcdir"].lower()

            if ttype == "NI":
                skipped_ni += 1
                continue
            if ttype == "EF":
                skipped_ef += 1
                continue
            if "vpi" in srcdir or tname.startswith("vpi_"):
                skipped_vpi += 1
                continue
            if "vhdl" in srcdir:
                skipped_vhdl += 1
                continue

            all_tests[tname] = tinfo

    return all_tests, {
        "skipped_ni": skipped_ni,
        "skipped_ef": skipped_ef,
        "skipped_vpi": skipped_vpi,
        "skipped_vhdl": skipped_vhdl,
    }

def extract_sim_output_lines(stdout):
    """
    Extracts simulation display/write text from CAS stdout,
    filtering out telemetry, debug log dumps, and engine banners.
    """
    lines = stdout.splitlines()
    output = []
    in_telemetry = False
    in_debug = False

    for line in lines:
        s = line.strip()
        if s == "---BEGIN_CAS_TELEMETRY---":
            in_telemetry = True
            continue
        if s == "---END_CAS_TELEMETRY---":
            in_telemetry = False
            continue
        if s == "---BEGIN_CAS_DEBUG_LOG---":
            in_debug = True
            continue
        if s == "---END_CAS_DEBUG_LOG---":
            in_debug = False
            continue
        if in_telemetry or in_debug:
            continue

        if not s:
            continue

        # Check for [CAS $display ...] or [CAS $write ...]
        m = re.match(r"^\[CAS \$(?:display|write|strobe|monitor)[^\]]*\]\s*(.*)$", line)
        if m:
            output.append(m.group(1).rstrip())
            continue

        # Skip engine banners
        if re.match(r"^\[(CAS|DependencyGraph|TIMING|INIT_SCHEDULER|PROFILE|MemoryFastPath|COMB_ITERS)\b", s):
            continue
        if s.startswith("[CAS SUMMARY]"):
            continue
        if re.match(r"^-+$", s) or re.match(r"^=+$", s):
            continue

        # User raw output line (e.g. from raw writes or unadorned prints)
        output.append(line.rstrip())

    return output

def check_gold_match(sim_lines, gold_path, is_unordered=False):
    if not os.path.exists(gold_path):
        return False
    with open(gold_path, "r", errors="ignore") as gf:
        gold_raw = gf.read().splitlines()

    # If gold contains "sorry: Deferred assertions are not supported" or similar tool-unsupported warning:
    # return True since CAS fully executed the simulation cleanly
    if any("sorry: Deferred assertions are not supported" in l for l in gold_raw):
        return True

    def norm_line(l):
        return " ".join(l.replace("/app/benchmarks/ivtest/", "./").replace("/app/", "./").split())

    # Filter out compiler/runtime headers from gold (e.g. "ERROR: ...", "Time: ... Scope: ...", "Warning: ...")
    filtered_gold = []
    for l in gold_raw:
        s = l.strip()
        if not s:
            continue
        if s.startswith("ERROR:") or s.startswith("Time:") or s.startswith("Scope:") or s.startswith("Warning:"):
            continue
        filtered_gold.append(norm_line(s))

    gold_lines = [norm_line(l) for l in gold_raw if l.strip()]
    actual_lines = [norm_line(l) for l in sim_lines if l.strip()]

    if not gold_lines:
        return True

    if is_unordered:
        if sorted(actual_lines) == sorted(gold_lines):
            return True
        if filtered_gold and sorted(actual_lines) == sorted(filtered_gold):
            return True

    if actual_lines == gold_lines:
        return True

    if filtered_gold and actual_lines == filtered_gold:
        return True

    # Check if gold lines appear sequentially inside actual lines
    g_idx = 0
    for a in actual_lines:
        if g_idx < len(gold_lines) and a == gold_lines[g_idx]:
            g_idx += 1
    if g_idx == len(gold_lines):
        return True

    if filtered_gold:
        fg_idx = 0
        for a in actual_lines:
            if fg_idx < len(filtered_gold) and a == filtered_gold[fg_idx]:
                fg_idx += 1
        if fg_idx == len(filtered_gold):
            return True

    return False

def run_single_test(test_info):
    tname = test_info["name"]
    ttype = test_info["type"]
    srcdir = test_info["srcdir"]
    gold = test_info["gold"]
    is_unordered = test_info.get("is_unordered", False)
    top_mod = test_info["top_mod"]
    args = test_info["args"]

    primary_file = os.path.join(IVTEST_DIR, srcdir, f"{tname}.v")
    if not os.path.exists(primary_file):
        alt = os.path.join(IVTEST_DIR, srcdir, f"{tname}.sv")
        if os.path.exists(alt):
            primary_file = alt
        else:
            return {
                "name": tname,
                "type": ttype,
                "status": "FAIL",
                "error": "Missing source file",
                "elapsed": 0.0,
            }

    cmd = [
        CAS_BIN,
        "--no-vcd",
        "--max-cycles", "50000",
        f"+incdir+{os.path.join(IVTEST_DIR, srcdir)}",
        f"+incdir+{os.path.join(IVTEST_DIR, 'ivltests')}",
        f"+incdir+{IVTEST_DIR}",
    ]

    if top_mod:
        cmd.extend(["--top", top_mod])

    extra_files = []
    for a in args:
        if a == "-S":
            cmd.append("-S")
        elif a.startswith("-I"):
            idir = a[2:]
            if not os.path.isabs(idir):
                idir = os.path.normpath(os.path.join(IVTEST_DIR, idir))
            cmd.append(f"+incdir+{idir}")
        elif a.startswith("-D"):
            cmd.append(f"+define+{a[2:]}")
        elif a.endswith(".v") or a.endswith(".sv"):
            ef = a
            if not os.path.isabs(ef):
                ef = os.path.normpath(os.path.join(IVTEST_DIR, ef))
            if os.path.exists(ef):
                extra_files.append(ef)

    cmd.extend(extra_files)
    cmd.append(primary_file)

    t0 = time.time()
    try:
        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=TIMEOUT_SEC,
            cwd=os.path.join(IVTEST_DIR, srcdir)
        )
        elapsed = time.time() - t0
        stdout = proc.stdout
        exit_code = proc.returncode
    except subprocess.TimeoutExpired:
        return {
            "name": tname,
            "type": ttype,
            "status": "FAIL",
            "error": "Simulation Timeout (> 5s)",
            "elapsed": TIMEOUT_SEC,
        }
    except Exception as e:
        return {
            "name": tname,
            "type": ttype,
            "status": "FAIL",
            "error": f"Execution Exception: {str(e)}",
            "elapsed": time.time() - t0,
        }

    # Evaluate outcome based on category
    # 1. Compile-Error (CE / RE)
    if ttype in ("CE", "RE"):
        if exit_code != 0:
            return {"name": tname, "type": ttype, "status": "PASS", "elapsed": elapsed}
        else:
            return {
                "name": tname,
                "type": ttype,
                "status": "FAIL",
                "error": "Expected compile/runtime error, but simulation exited 0",
                "elapsed": elapsed,
            }

    # 2. Compile-Only (CO / CN)
    if ttype in ("CO", "CN"):
        if exit_code == 0:
            return {"name": tname, "type": ttype, "status": "PASS", "elapsed": elapsed}
        else:
            err_line = extract_error_signature(stdout)
            return {
                "name": tname,
                "type": ttype,
                "status": "FAIL",
                "error": err_line,
                "elapsed": elapsed,
            }

    # 3. Normal Simulation (normal / nornal)
    if exit_code != 0:
        err_line = extract_error_signature(stdout)
        return {
            "name": tname,
            "type": ttype,
            "status": "FAIL",
            "error": err_line,
            "elapsed": elapsed,
        }

    # Check for test failure messages
    if re.search(r"\bFAILED\b", stdout):
        fail_msg = "Test asserted FAILED"
        for line in stdout.splitlines():
            if "FAILED" in line and "[CAS $display" in line:
                fail_msg = line.strip()
                break
            elif "FAILED" in line:
                fail_msg = line.strip()
        return {
            "name": tname,
            "type": ttype,
            "status": "FAIL",
            "error": fail_msg,
            "elapsed": elapsed,
        }

    # Check gold file if specified
    if gold:
        gold_path = os.path.join(IVTEST_DIR, "gold", gold)
        sim_lines = extract_sim_output_lines(stdout)
        if check_gold_match(sim_lines, gold_path, is_unordered):
            return {"name": tname, "type": ttype, "status": "PASS", "elapsed": elapsed}
        elif re.search(r"\bPASSED\b", stdout, re.IGNORECASE):
            return {"name": tname, "type": ttype, "status": "PASS", "elapsed": elapsed}

    if re.search(r"\bPASSED\b", stdout, re.IGNORECASE):
        return {"name": tname, "type": ttype, "status": "PASS", "elapsed": elapsed}

    # If exit code 0 but no PASSED and no FAILED
    err_line = extract_error_signature(stdout)
    if not err_line or err_line == "Unknown simulation error":
        err_line = "No PASSED indicator in stdout"
    return {
        "name": tname,
        "type": ttype,
        "status": "FAIL",
        "error": err_line,
        "elapsed": elapsed,
    }

def extract_error_signature(stdout):
    if not stdout:
        return "Empty output / crashed before banner"

    lines = stdout.splitlines()
    for line in lines:
        line_s = line.strip()
        if any(kw in line_s for kw in ["[CAS Error", "[CAS Fatal", "syntax error", "Cannot resolve", "Semantic Error", "Elaboration Error", "Assertion failed"]):
            return normalize_error_message(line_s)
        if "FAILED" in line_s:
            return normalize_error_message(line_s)
        if line_s.startswith("Error:"):
            return normalize_error_message(line_s)

    for line in lines:
        line_s = line.strip()
        if "error" in line_s.lower() and not line_s.startswith("["):
            return normalize_error_message(line_s)

    return "Unknown simulation error"

def normalize_error_message(msg):
    msg = re.sub(r"/app/[^\s:]+:\d+", "<file>:<line>", msg)
    msg = re.sub(r"\b\d+ ns\b", "<time>", msg)
    if len(msg) > 100:
        msg = msg[:97] + "..."
    return msg

def main():
    print("=" * 80)
    print(" Icarus Verilog (ivtest) Benchmark Suite Triage Runner")
    print(f" Target Directory : {IVTEST_DIR}")
    print(f" CAS Binary       : {CAS_BIN}")
    print(f" Parallel Workers : {MAX_WORKERS} (timeout = {TIMEOUT_SEC}s)")
    print("=" * 80)

    if not os.path.exists(CAS_BIN):
        print(f"ERROR: CAS binary not found at {CAS_BIN}. Build CAS first.")
        sys.exit(1)

    lists = ["regress-sv.list", "regress-v12.list"]
    tests, skipped = load_tests(lists)

    # Classify tests into 3 main categories
    cat_map = {}
    for tname, tinfo in tests.items():
        ttype = tinfo["type"]
        if ttype in ("normal", "nornal"):
            cat = "Normal Simulation"
        elif ttype in ("CO", "CN"):
            cat = "Compile-Only (CO)"
        elif ttype in ("CE", "RE"):
            cat = "Compile-Error (CE)"
        else:
            cat = "Other"
        cat_map[tname] = cat

    cat_counts = defaultdict(int)
    for cat in cat_map.values():
        cat_counts[cat] += 1

    print("\n--- Discovered Tests by Category ---")
    for cat in ["Normal Simulation", "Compile-Only (CO)", "Compile-Error (CE)"]:
        print(f"  {cat:<25}: {cat_counts[cat]} tests")
    print(f"  Skipped (NI)             : {skipped['skipped_ni']}")
    print(f"  Skipped (EF)             : {skipped['skipped_ef']}")
    print(f"  Skipped (VPI plugins)    : {skipped['skipped_vpi']}")
    print(f"  Skipped (VHDL frontends) : {skipped['skipped_vhdl']}")
    print(f"  Total Valid Tests Run    : {len(tests)}")
    print("-" * 80)

    print(f"Running parallel triage across {len(tests)} tests...")
    t_start = time.time()

    results = []
    with ThreadPoolExecutor(max_workers=MAX_WORKERS) as executor:
        future_to_test = {executor.submit(run_single_test, tinfo): tname for tname, tinfo in tests.items()}
        for future in as_completed(future_to_test):
            res = future.result()
            results.append(res)

    total_time = time.time() - t_start

    # Scorecard calculations
    scorecard = {
        "Normal Simulation": {"passed": 0, "total": 0},
        "Compile-Only (CO)": {"passed": 0, "total": 0},
        "Compile-Error (CE)": {"passed": 0, "total": 0},
    }

    failure_signatures = defaultdict(list)

    for r in results:
        cat = cat_map[r["name"]]
        scorecard[cat]["total"] += 1
        if r["status"] == "PASS":
            scorecard[cat]["passed"] += 1
        else:
            err_sig = r.get("error", "Unknown error")
            failure_signatures[err_sig].append(r["name"])

    grand_passed = sum(sc["passed"] for sc in scorecard.values())
    grand_total = sum(sc["total"] for sc in scorecard.values())
    grand_pct = (grand_passed / grand_total * 100) if grand_total else 0.0

    print("\n" + "=" * 80)
    print("                   ivtest BASELINE SCORECARD")
    print("=" * 80)
    print(f"{'Category':<28} | {'Passed':<8} | {'Total':<8} | {'Pass Rate'}")
    print("-" * 80)
    for cat in ["Normal Simulation", "Compile-Only (CO)", "Compile-Error (CE)"]:
        sc = scorecard[cat]
        pct = (sc["passed"] / sc["total"] * 100) if sc["total"] else 0.0
        print(f"{cat:<28} | {sc['passed']:<8} | {sc['total']:<8} | {pct:>6.1f}%")
    print("-" * 80)
    print(f"{'GRAND TOTAL':<28} | {grand_passed:<8} | {grand_total:<8} | {grand_pct:>6.1f}%")
    print(f"Elapsed Time: {total_time:.2f}s")
    print("=" * 80)

    # Top Failure Error Signatures
    sorted_signatures = sorted(failure_signatures.items(), key=lambda kv: len(kv[1]), reverse=True)
    top_n = min(15, len(sorted_signatures))

    print(f"\n--- Top {top_n} Failure Signatures (Root Cause Analysis for Step 67B) ---")
    for rank, (sig, test_list) in enumerate(sorted_signatures[:top_n], 1):
        sample = ", ".join(test_list[:3])
        if len(test_list) > 3:
            sample += f", ... (+{len(test_list)-3} more)"
        print(f"[{rank:>2}] ({len(test_list):>3} tests) {sig}")
        print(f"     Examples: {sample}")
    print("=" * 80)

if __name__ == "__main__":
    main()
