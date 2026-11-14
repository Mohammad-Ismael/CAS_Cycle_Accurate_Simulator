#!/usr/bin/env python3
import os
import re
import sys
import json
import argparse
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
from collections import defaultdict, Counter

def parse_metadata(file_path):
    meta = {
        "name": os.path.basename(file_path),
        "type": "parsing elaboration",
        "should_fail_because": "",
        "files": [file_path],
        "incdirs": [os.path.dirname(file_path)],
        "defines": [],
        "top_module": "",
        "timeout": 4
    }
    try:
        with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
    except Exception:
        return meta

    m = re.search(r"/\*(.*?)\*/", content, re.DOTALL)
    header = m.group(1) if m else content

    for line in header.splitlines():
        line = line.strip()
        pm = re.match(r"^:([a-zA-Z_-]+):\s*(.*)", line)
        if pm:
            k, v = pm.group(1).lower(), pm.group(2).strip()
            if k == "type":
                meta["type"] = v
            elif k == "should_fail_because":
                meta["should_fail_because"] = v
            elif k == "top_module":
                meta["top_module"] = v
            elif k == "files":
                flist = []
                for fn in v.split():
                    cand = os.path.join(os.path.dirname(file_path), fn)
                    if os.path.exists(cand):
                        flist.append(cand)
                    else:
                        flist.append(fn)
                if flist:
                    meta["files"] = flist
            elif k == "incdirs":
                meta["incdirs"] = [os.path.join(os.path.dirname(file_path), d) for d in v.split()]
            elif k == "defines":
                meta["defines"] = v.split()
    return meta

def evaluate_assertions(log_text):
    asserts_found = 0
    for line in log_text.splitlines():
        idx = line.find(":assert:")
        if idx != -1:
            expr = line[idx + len(":assert:"):].strip()
            asserts_found += 1
            try:
                # sanitize any 0b / standard SV constants if needed
                if not eval(expr, {"__builtins__": {}}, {}):
                    return False, f"Assertion evaluated to False: {expr}"
            except Exception as e:
                return False, f"Assertion evaluation error '{expr}': {e}"
    return True, asserts_found

def categorize_error(retcode, out_text, err_text, is_timeout):
    if is_timeout:
        return "Timeout (>4s)", "Execution exceeded 4s timeout"
    
    combined = (out_text + "\n" + err_text).strip()
    
    # Check for crash/segfault
    if retcode < 0 or retcode in (134, 139):
        return "Crash / Signal (Segfault)", f"Process terminated by signal {-retcode if retcode < 0 else retcode}"

    # Search for specific error lines
    err_line = ""
    for line in combined.splitlines():
        line_s = line.strip()
        if any(token in line_s for token in ["[CAS ERROR]", "[FATAL]", "ERROR:", "Error:", "syntax error", "Syntax Error", "Cannot open", "Undefined"]):
            err_line = line_s
            break
            
    if not err_line:
        # Fall back to first non-empty line or returncode
        lines = [l.strip() for l in combined.splitlines() if l.strip()]
        err_line = lines[-1] if lines else f"Process exited with code {retcode}"

    # Classify category
    err_lower = err_line.lower()
    if "syntax error" in err_lower or "unexpected" in err_lower or "parser" in err_lower:
        cat = "Parser / Syntax Error"
    elif "cannot find module" in err_lower or "elaboration" in err_lower or "unknown net" in err_lower or "scoping" in err_lower:
        cat = "Elaboration / Hierarchy Error"
    elif "assertion" in err_lower or ":assert:" in err_lower:
        cat = "Simulation Assertion Failure"
    elif "unsupported" in err_lower or "not implemented" in err_lower:
        cat = "Unsupported SV Construct"
    elif "cannot open source file" in err_lower or "file not found" in err_lower:
        cat = "File / Include Resolution Error"
    else:
        cat = "Runtime / Execution Error"
        
    return cat, err_line

def run_test(test_info, cas_bin):
    ch, path, meta = test_info
    cmd = [cas_bin, "--no-vcd"]
    if meta["top_module"]:
        cmd += ["-top", meta["top_module"]]
    for inc in meta["incdirs"]:
        cmd += ["+incdir+" + inc]
    for df in meta["defines"]:
        cmd += ["+define+" + df]
    cmd += meta["files"]

    is_timeout = False
    proc_ret = -1
    stdout, stderr = "", ""
    test_dir = os.path.dirname(path)
    
    try:
        proc = subprocess.run(cmd, cwd=test_dir, capture_output=True, text=True, timeout=meta["timeout"])
        proc_ret = proc.returncode
        stdout = proc.stdout
        stderr = proc.stderr
    except subprocess.TimeoutExpired:
        is_timeout = True
    except Exception as ex:
        stderr = str(ex)

    combined = stdout + "\n" + stderr
    passed = False
    fail_cat, fail_msg = "", ""

    if is_timeout:
        passed = False
        fail_cat, fail_msg = categorize_error(proc_ret, stdout, stderr, True)
    elif proc_ret == 0:
        # Check assertions
        assert_ok, assert_detail = evaluate_assertions(combined)
        if not assert_ok:
            passed = False
            fail_cat = "Simulation Assertion Failure"
            fail_msg = assert_detail
        elif "[CAS ERROR]" in combined or "[FATAL]" in combined:
            passed = False
            fail_cat, fail_msg = categorize_error(proc_ret, stdout, stderr, False)
        else:
            passed = True
    else:
        passed = False
        fail_cat, fail_msg = categorize_error(proc_ret, stdout, stderr, False)

    return {
        "chapter": ch,
        "test_path": path,
        "name": meta["name"],
        "passed": passed,
        "category": fail_cat,
        "error_line": fail_msg,
        "stdout": stdout,
        "stderr": stderr
    }

def main():
    parser = argparse.ArgumentParser(description="Run sv-tests simulation suite against CAS")
    parser.add_argument("--chapters", nargs="+", type=int, default=[5, 6, 7, 8, 9, 10, 11, 12],
                        help="Chapter numbers to run")
    parser.add_argument("--threads", type=int, default=8, help="Number of concurrent worker threads")
    parser.add_argument("--repo-root", default="/app", help="Path to repo root")
    args = parser.parse_args()

    cas_bin = os.path.join(args.repo_root, "CAS", "build", "cas")
    sv_tests_dir = os.path.join(args.repo_root, "external_suites", "sv-tests", "tests")

    test_queue = []
    
    for ch_num in args.chapters:
        ch_name = f"chapter-{ch_num}"
        ch_dir = os.path.join(sv_tests_dir, ch_name)
        if not os.path.exists(ch_dir):
            continue
        for root, dirs, files in os.walk(ch_dir):
            for fname in sorted(files):
                if fname.endswith(".sv"):
                    fpath = os.path.join(root, fname)
                    meta = parse_metadata(fpath)
                    # Filter: :type: contains simulation and NO :should_fail_because:
                    if "simulation" in meta["type"] and not meta["should_fail_because"]:
                        test_queue.append((ch_name, fpath, meta))

    print(f"Discovered {len(test_queue)} positive simulation tests across chapters {args.chapters}")
    print(f"Executing tests using {args.threads} parallel threads with 4s timeout...\n")

    results = []
    with ThreadPoolExecutor(max_workers=args.threads) as executor:
        futures = {executor.submit(run_test, t, cas_bin): t for t in test_queue}
        for future in as_completed(futures):
            res = future.result()
            results.append(res)

    # Sort results
    results.sort(key=lambda r: (r["chapter"], r["test_path"]))

    # Aggregate by chapter
    ch_stats = defaultdict(lambda: {"passed": 0, "failed": 0, "total": 0})
    failures = []
    
    # Prepopulate selected chapters so empty chapters still appear in scorecard
    for ch_num in args.chapters:
        ch_stats[f"chapter-{ch_num}"] = {"passed": 0, "failed": 0, "total": 0}

    for r in results:
        ch = r["chapter"]
        ch_stats[ch]["total"] += 1
        if r["passed"]:
            ch_stats[ch]["passed"] += 1
        else:
            ch_stats[ch]["failed"] += 1
            failures.append(r)

    # Print Scorecard
    print("==========================================================================")
    print("                 IEEE 1800 CHAPTER 5-12 BASELINE SCORECARD                ")
    print("==========================================================================")
    print(f"{'Chapter':<15} | {'Passed':<8} | {'Failed':<8} | {'Total':<8} | {'Pass %':<8}")
    print("-" * 55)

    tot_p = 0
    tot_f = 0
    tot_t = 0

    for ch_num in sorted(args.chapters):
        ch = f"chapter-{ch_num}"
        st = ch_stats[ch]
        p, f, t = st["passed"], st["failed"], st["total"]
        pct = (p / t * 100.0) if t > 0 else 0.0
        tot_p += p
        tot_f += f
        tot_t += t
        print(f"{ch:<15} | {p:<8} | {f:<8} | {t:<8} | {pct:>6.1f}%")

    tot_pct = (tot_p / tot_t * 100.0) if tot_t > 0 else 0.0
    print("-" * 55)
    print(f"{'TOTAL':<15} | {tot_p:<8} | {tot_f:<8} | {tot_t:<8} | {tot_pct:>6.1f}%")
    print("==========================================================================\n")

    # Save failures to /tmp/sv_tests_failures.json
    out_json = "/tmp/sv_tests_failures.json"
    with open(out_json, "w", encoding="utf-8") as jf:
        json.dump(failures, jf, indent=2)
    print(f"Saved {len(failures)} failures to {out_json}\n")

    # Group top failure categories
    cat_counts = Counter(f["category"] for f in failures)
    print("==========================================================================")
    print("                       TOP FAILURE CATEGORIES                             ")
    print("==========================================================================")
    top_cats = cat_counts.most_common(5)
    for idx, (cat, count) in enumerate(top_cats, 1):
        sample = next(f for f in failures if f["category"] == cat)
        rel_path = os.path.relpath(sample["test_path"], args.repo_root)
        print(f"{idx}. {cat} ({count} tests)")
        print(f"   Sample Test : {rel_path}")
        print(f"   Error Line  : {sample['error_line']}")
        print()

if __name__ == "__main__":
    main()
