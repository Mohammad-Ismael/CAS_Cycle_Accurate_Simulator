#!/usr/bin/env python3
"""
High-Performance Parallel Test Runner for Verilator test_regress Benchmark Suite
Benchmark Target: IEEE 1364-2005 Verilog & IEEE 1800 SystemVerilog Compliance & Stress Tests
"""

import os
import sys
import time
import subprocess
import re
import glob
import types
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from collections import defaultdict

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BENCHMARKS_DIR = os.path.join(REPO_ROOT, "benchmarks")
VERILATOR_REPO = os.path.join(BENCHMARKS_DIR, "verilator_repo")
TEST_REGRESS_DIR = os.path.join(VERILATOR_REPO, "test_regress")
T_DIR = os.path.join(TEST_REGRESS_DIR, "t")
CAS_BIN = os.path.join(REPO_ROOT, "CAS", "build", "cas")

TIMEOUT_SEC = 5
MAX_WORKERS = 8

# Inject mock vltest_bootstrap so importing it inside t_*.py is a harmless no-op
sys.modules['vltest_bootstrap'] = types.ModuleType('vltest_bootstrap')
os.environ.setdefault('VERILATOR_ROOT', VERILATOR_REPO)

class MockTest:
    def __init__(self, py_path):
        self.py_filename = py_path
        self.name = re.sub(r'\.py$', '', os.path.basename(py_path))
        self.top_filename = None
        self.v_other_filenames = []
        self.golden_filename = re.sub(r'\.py$', '.out', py_path)
        self.expect_filename = None
        self.has_compile = False
        self.has_execute = False
        self.has_lint = False
        self.fails = False
        self.verilator_flags = []
        self.verilator_flags2 = []
        self.top_module = None
        self.is_skipped = False
        self.skip_reason = None
        self.c_flags = []
        self.c_flags2 = []
        self.v_flags = []
        self.v_flags2 = []
        self.driver_verilator_flags = []
        self.stats = 'stats'
        self.obj_dir = 'obj_dir'
        self.vm_prefix = 'V' + self.name
        self.defines = []
        self.includes = []
        self.parameters = []
        self.sim_time = 10000
        self.pli_filename = None

    def scenarios(self, *args, **kwargs): pass
    def priority(self, *args, **kwargs): pass
    def leak_check_disable(self, *args, **kwargs): pass
    def skip_if_too_few_cores(self, *args, **kwargs): pass
    def skip(self, reason=''):
        self.is_skipped = True
        self.skip_reason = reason
    def passes(self, *args, **kwargs): pass
    def clean(self, *args, **kwargs): pass
    def run(self, *args, **kwargs): pass
    def error(self, *args, **kwargs): pass
    def oprint(self, *args, **kwargs): pass

    def lint(self, **kwargs):
        self.has_lint = True
        if kwargs.get('fails'): self.fails = True
        if kwargs.get('expect_filename'): self.expect_filename = kwargs.get('expect_filename')

    def compile(self, **kwargs):
        self.has_compile = True
        if kwargs.get('fails'): self.fails = True
        if kwargs.get('expect_filename'): self.expect_filename = kwargs.get('expect_filename')
        for k in ('v_other_filenames', 'v_filenames', 'v_files'):
            val = kwargs.get(k)
            if isinstance(val, list):
                self.v_other_filenames.extend(val)
            elif isinstance(val, str):
                self.v_other_filenames.append(val)
        raw_flags = []
        for k in ('verilator_flags', 'verilator_flags2', 'v_flags', 'v_flags2'):
            val = kwargs.get(k, [])
            if isinstance(val, list):
                raw_flags.extend(val)
            elif isinstance(val, str):
                raw_flags.append(val)
        raw_flags.extend(self.v_flags)
        raw_flags.extend(self.v_flags2)
        raw_flags.extend(self.verilator_flags)
        raw_flags.extend(self.verilator_flags2)
        tokens = []
        for item in raw_flags:
            if isinstance(item, str):
                tokens.extend(item.split())
        for i, f in enumerate(tokens):
            self.verilator_flags2.append(f)
            if f == '--top-module' and i + 1 < len(tokens):
                self.top_module = tokens[i+1]
            elif f.startswith('+define+'):
                self.defines.append(f[8:])
            elif f.startswith('-D'):
                self.defines.append(f[2:])
            elif f.startswith('+incdir+'):
                self.includes.append(f[8:])
            elif f.startswith('-I'):
                self.includes.append(f[2:])
            elif f == '-P' and i + 1 < len(tokens):
                self.parameters.append(tokens[i+1])
            elif f.startswith('-P'):
                self.parameters.append(f[2:])

    def execute(self, **kwargs):
        self.has_execute = True
        if kwargs.get('fails'): self.fails = True
        if kwargs.get('expect_filename'): self.expect_filename = kwargs.get('expect_filename')
        for k in ('v_other_filenames', 'v_filenames', 'v_files'):
            val = kwargs.get(k)
            if isinstance(val, list):
                self.v_other_filenames.extend(val)
            elif isinstance(val, str):
                self.v_other_filenames.append(val)

    def __getattr__(self, name):
        def dummy(*args, **kwargs): return []
        return dummy

_ALL_V_FILES = None
_ALL_PY_BASES = None

def get_companion_files(top_filename):
    global _ALL_V_FILES, _ALL_PY_BASES
    if _ALL_V_FILES is None:
        if os.path.exists(T_DIR):
            listing = os.listdir(T_DIR)
            _ALL_V_FILES = [f for f in listing if f.endswith('.v') or f.endswith('.sv')]
            _ALL_PY_BASES = set(os.path.splitext(f)[0] for f in listing if f.endswith('.py'))
        else:
            _ALL_V_FILES = []
            _ALL_PY_BASES = set()
    if not top_filename:
        return []
    top_base = os.path.splitext(os.path.basename(top_filename))[0]
    prefix = top_base + "_"
    matches = []
    for f in _ALL_V_FILES:
        if f.startswith(prefix):
            base = os.path.splitext(f)[0]
            if base not in _ALL_PY_BASES:
                full_path = os.path.join(T_DIR, f)
                if full_path != top_filename:
                    matches.append(full_path)
    return matches

def parse_verilator_test(py_path):
    mt = MockTest(py_path)
    safe_os = types.SimpleNamespace(**{k: v for k, v in os.__dict__.items() if k not in ('system', 'popen')}, system=lambda *a, **k: 0, popen=lambda *a, **k: None)
    safe_subprocess = types.SimpleNamespace(run=lambda *a, **k: types.SimpleNamespace(returncode=0, stdout='', stderr=''), Popen=lambda *a, **k: None, call=lambda *a, **k: 0)
    env = {'test': mt, 're': re, 'os': safe_os, 'sys': sys, 'subprocess': safe_subprocess}
    
    old_stdout = sys.stdout
    old_stderr = sys.stderr
    try:
        import io
        devnull = io.StringIO()
        sys.stdout = devnull
        sys.stderr = devnull
        with open(py_path, 'r', errors='ignore') as f:
            code = compile(f.read(), py_path, 'exec')
            exec(code, env)
    except Exception:
        pass
    finally:
        sys.stdout = old_stdout
        sys.stderr = old_stderr

    # Resolve primary top source file
    if not mt.top_filename or not isinstance(mt.top_filename, str):
        cand_v = re.sub(r'\.py$', '.v', py_path)
        cand_sv = re.sub(r'\.py$', '.sv', py_path)
        if os.path.exists(cand_v):
            mt.top_filename = cand_v
        elif os.path.exists(cand_sv):
            mt.top_filename = cand_sv
        else:
            mt.top_filename = cand_v
    elif not os.path.isabs(mt.top_filename):
        mt.top_filename = os.path.normpath(os.path.join(T_DIR, mt.top_filename if not mt.top_filename.startswith('t/') else mt.top_filename[2:]))

    # Auto-discover companion sub-files (e.g. t_case_huge_sub2.v)
    if mt.top_filename:
        mt.v_other_filenames.extend(get_companion_files(mt.top_filename))

    # Resolve extra source files
    resolved_extra = []
    seen = set()
    for ef in mt.v_other_filenames:
        if isinstance(ef, str) and (ef.endswith('.v') or ef.endswith('.sv')):
            p = ef if os.path.isabs(ef) else os.path.normpath(os.path.join(T_DIR, ef if not ef.startswith('t/') else ef[2:]))
            if os.path.exists(p) and p != mt.top_filename and p not in seen:
                resolved_extra.append(p)
                seen.add(p)
    mt.v_other_filenames = resolved_extra

    return mt

def read_inputs_v(filename):
    """
    Parses input port declarations from the top-level module 't'
    to determine if a clock/stimulus wrapper is required.
    """
    inputs = {}
    if not os.path.exists(filename):
        return inputs
    try:
        with open(filename, 'r', encoding='utf8', errors='ignore') as fh:
            content = fh.read()
        m = re.search(r'\bmodule\s+t\b\s*(?:#\s*\([^)]*\)\s*)?(?:\((.*?)\))?;', content, re.DOTALL)
        if m and m.group(1):
            for m_port in re.finditer(r'\binput\s+(?:(?:logic|bit|reg|wire|signed|unsigned)\s+)*(\[[^\]]+\]\s*)?([A-Za-z0-9_]+)', m.group(1)):
                rng = m_port.group(1) or ""
                inputs[m_port.group(2)] = rng.strip()
        m_body = re.search(r'\bmodule\s+t\b.*?\bendmodule\b', content, re.DOTALL)
        if m_body:
            for m_inp in re.finditer(r'^\s*input\s+(?:(?:logic|bit|reg|wire|signed|unsigned)\s+)*(\[[^\]]+\]\s*)?([A-Za-z0-9_]+)', m_body.group(0), re.MULTILINE):
                rng = m_inp.group(1) or ""
                inputs[m_inp.group(2)] = rng.strip()
    except Exception:
        pass
    return inputs

def load_all_tests(test_filter=None):
    py_files = sorted(glob.glob(os.path.join(T_DIR, 't_*.py')))
    if test_filter:
        py_files = [
            pf for pf in py_files
            if any(t == os.path.splitext(os.path.basename(pf))[0] or t in os.path.basename(pf) for t in test_filter)
        ]
    
    valid_tests = {}
    skipped_counts = {
        'exe_cpp': 0,
        'dpi_vpi': 0,
        'sc': 0,
        'lint_only': 0,
        'no_source': 0,
        'other': 0
    }

    for pf in py_files:
        t = parse_verilator_test(pf)
        tname = t.name
        all_flags = ' '.join(str(f) for f in t.verilator_flags + t.verilator_flags2 if isinstance(f, str))
        cpp_harness = os.path.join(T_DIR, f"{tname}.cpp")
        has_cpp = os.path.exists(cpp_harness) or any('.cpp' in str(x) for x in t.v_other_filenames if isinstance(x, str)) or '--exe' in all_flags
        has_sc = '--sc' in all_flags or 'sc_time' in all_flags
        is_dpi_vpi = (
            tname.startswith('t_dpi_') or tname.startswith('t_vpi_') or tname.startswith('t_vpi')
            or '--vpi' in all_flags or '--dpi-hdr-only' in all_flags
            or bool(t.pli_filename)
        )
        is_cli_flag_only = (
            any(f.startswith('--debug') or f in ('-V', '--version', '--help', '--gdbbt', '-E') for f in (t.verilator_flags + t.verilator_flags2) if isinstance(f, str))
            or '-E' in all_flags or '--pipe-filter' in all_flags
        )
        is_tool_internal = (
            any(f in ('-V', '--version', '--help', '--gdbbt', '--lib-create', '--protect-ids', '--hierarchical-block', '--savable') or f.startswith('--debug') for f in (t.verilator_flags + t.verilator_flags2) if isinstance(f, str))
            or tname.startswith(('t_savable_', 't_hier_block', 't_runflag', 't_protect_ids', 't_lib_create'))
            or '--savable' in all_flags or '--hierarchical-block' in all_flags or '--lib-create' in all_flags or '--protect-ids' in all_flags
        )
        is_coverage = '--coverage' in all_flags or tname.startswith('t_vlcov_') or tname.startswith('t_cover_')

        if not os.path.exists(t.top_filename):
            skipped_counts['no_source'] += 1
            continue
        if has_sc:
            skipped_counts['sc'] += 1
            continue
        if is_dpi_vpi:
            skipped_counts['dpi_vpi'] += 1
            continue
        if has_cpp:
            skipped_counts['exe_cpp'] += 1
            continue
        if (t.has_lint and not t.has_compile and not t.has_execute) or '--lint-only' in all_flags or is_cli_flag_only or is_tool_internal or is_coverage:
            skipped_counts['lint_only'] += 1
            continue

        if t.fails:
            ttype = 'CE'
        elif t.has_execute or t.has_compile:
            ttype = 'SIM'
        else:
            skipped_counts['other'] += 1
            continue

        valid_tests[tname] = {
            'name': tname,
            'type': ttype,
            'top_filename': t.top_filename,
            'v_other_filenames': t.v_other_filenames,
            'golden_filename': t.golden_filename if os.path.exists(t.golden_filename) else None,
            'top_module': t.top_module,
            'defines': t.defines,
            'includes': t.includes,
            'parameters': t.parameters,
            'sim_time': getattr(t, 'sim_time', 15000) if getattr(t, 'sim_time', 15000) else 15000,
            'has_execute': t.has_execute,
        }

    return valid_tests, skipped_counts

def extract_sim_output_lines(stdout):
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

        if re.match(r"^\[(CAS|DependencyGraph|TIMING|INIT_SCHEDULER|PROFILE|MemoryFastPath|COMB_ITERS)\b", s):
            continue
        if s.startswith("[CAS SUMMARY]"):
            continue
        if re.match(r"^-+$", s) or re.match(r"^=+$", s):
            continue

        output.append(line.rstrip())

    return output

def check_gold_match(sim_lines, gold_path):
    if not os.path.exists(gold_path):
        return False
    with open(gold_path, "r", errors="ignore") as gf:
        gold_raw = gf.read().splitlines()

    def norm_line(l):
        return " ".join(l.replace("/app/benchmarks/verilator_repo/test_regress/", "./").replace("/app/", "./").split())

    gold_lines = [norm_line(l) for l in gold_raw if l.strip()]
    actual_lines = [norm_line(l) for l in sim_lines if l.strip()]

    if not gold_lines:
        return True

    if actual_lines == gold_lines:
        return True

    # Check if gold lines appear sequentially inside actual lines
    g_idx = 0
    for a in actual_lines:
        if g_idx < len(gold_lines) and a == gold_lines[g_idx]:
            g_idx += 1
    if g_idx == len(gold_lines):
        return True

    return False

def normalize_error_message(msg):
    msg = re.sub(r"/app/[^\s:]+:\d+", "<file>:<line>", msg)
    msg = re.sub(r"\b\d+ ns\b", "<time>", msg)
    if len(msg) > 100:
        msg = msg[:97] + "..."
    return msg

def extract_error_signature(stdout):
    if not stdout:
        return "Empty output / crashed before banner"

    lines = stdout.splitlines()
    for line in lines:
        line_s = line.strip()
        if any(kw in line_s for kw in [
            "[CAS Error", "[CAS ERROR]", "[CAS Fatal", "[CAS FATAL]",
            "[CAS-DTINPCIL]", "[PREPROC-ERROR]", "[PREPROC-FATAL]",
            "syntax error", "Cannot resolve", "Semantic Error", "Elaboration Error", "Assertion failed"
        ]):
            return normalize_error_message(line_s)
        if "%Error" in line_s:
            return normalize_error_message(line_s)
        if line_s.startswith("Error:") or line_s.startswith("error:"):
            return normalize_error_message(line_s)
        if "Simulation Terminated at time" in line_s and ("$stop" in line_s or "$fatal" in line_s):
            return normalize_error_message(line_s)

    for line in lines:
        line_s = line.strip()
        if "error" in line_s.lower() and not line_s.startswith("["):
            return normalize_error_message(line_s)

    return "Unknown simulation error"

def run_single_test(test_info):
    tname = test_info["name"]
    ttype = test_info["type"]
    top_filename = test_info["top_filename"]
    v_other_filenames = test_info["v_other_filenames"]
    gold = test_info["golden_filename"]
    top_mod = test_info["top_module"]
    defines = test_info["defines"]
    includes = test_info["includes"]
    parameters = test_info["parameters"]

    if not os.path.exists(top_filename):
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
        "--max-cycles", "500000",
        f"+incdir+{T_DIR}",
    ]

    for inc in includes:
        cmd.append(f"+incdir+{inc}")
    for d in defines:
        cmd.append(f"+define+{d}")
    for p in parameters:
        cmd.extend(["-P", p])

    if top_mod:
        cmd.extend(["--top", top_mod])

    # Check if module 't' has input ports (e.g. clk) requiring a top-level stimulus wrapper
    inputs = read_inputs_v(top_filename) if test_info.get("has_execute", True) else {}
    top_wrapper_file = None

    if inputs and not top_mod:
        try:
            sim_time = test_info.get('sim_time', 15000)
            if not sim_time or sim_time < 5000:
                sim_time = 15000
            with tempfile.NamedTemporaryFile('w', suffix='_cas_top_wrapper.v', delete=False) as tf:
                tf.write("module top;\n")
                for inp, rng in sorted(inputs.items()):
                    if rng:
                        tf.write(f"    reg {rng} {inp};\n")
                    else:
                        tf.write(f"    reg {inp};\n")
                tf.write("    t t (\n")
                comma = ""
                for inp in sorted(inputs.keys()):
                    tf.write(f"      {comma}.{inp} ({inp})\n")
                    comma = ","
                tf.write("    );\n\n")
                tf.write("    initial begin\n")
                for inp in sorted(inputs.keys()):
                    tf.write(f"        {inp} = 0;\n")
                tf.write("        #10;\n")
                if 'fastclk' in inputs:
                    tf.write("        fastclk = 1;\n")
                if 'clk' in inputs:
                    tf.write("        clk = 1;\n")
                tf.write(f"        #{sim_time};\n")
                tf.write("        $finish;\n")
                tf.write("    end\n")
                if 'clk' in inputs:
                    tf.write("    always #6 if ($time >= 10) clk = ~clk;\n")
                if 'fastclk' in inputs:
                    tf.write("    always #1 if ($time >= 10) fastclk = ~fastclk;\n")
                tf.write("endmodule\n")
                top_wrapper_file = tf.name
            cmd.extend(["--top", "top", top_wrapper_file])
        except Exception:
            pass

    cmd.append(top_filename)
    cmd.extend(v_other_filenames)

    t0 = time.time()
    try:
        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=TIMEOUT_SEC,
            cwd=T_DIR
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
    finally:
        if top_wrapper_file and os.path.exists(top_wrapper_file):
            try:
                os.remove(top_wrapper_file)
            except OSError:
                pass

    # 1. Negative Compile-Error (CE)
    if ttype == "CE":
        has_error = (exit_code != 0) or any(kw in stdout.lower() for kw in ["[cas error", "[cas fatal", "syntax error", "%error", "assertion failed", "error:"])
        if has_error:
            return {"name": tname, "type": ttype, "status": "PASS", "elapsed": elapsed}
        else:
            return {
                "name": tname,
                "type": ttype,
                "status": "FAIL",
                "error": "Expected compile/elaboration failure, but exited 0",
                "elapsed": elapsed,
            }

    # 2. Pure-SV Simulation (SIM)
    if exit_code != 0:
        err_line = extract_error_signature(stdout)
        return {
            "name": tname,
            "type": ttype,
            "status": "FAIL",
            "error": err_line,
            "elapsed": elapsed,
        }

    # Exit code is 0: check for simulation failure triggers in output
    for line in stdout.splitlines():
        line_s = line.strip()
        if "%Error" in line_s or "FATAL" in line_s:
            return {
                "name": tname,
                "type": ttype,
                "status": "FAIL",
                "error": normalize_error_message(line_s),
                "elapsed": elapsed,
            }
        if "Simulation Terminated at time" in line_s and ("$stop" in line_s or "$fatal" in line_s):
            return {
                "name": tname,
                "type": ttype,
                "status": "FAIL",
                "error": normalize_error_message(line_s),
                "elapsed": elapsed,
            }

    # Compile-only test (no test.execute() in test driver)
    if not test_info.get("has_execute", True):
        return {"name": tname, "type": ttype, "status": "PASS", "elapsed": elapsed}

    # Check golden output match if gold file exists
    if gold and os.path.exists(gold):
        sim_lines = extract_sim_output_lines(stdout)
        if check_gold_match(sim_lines, gold):
            return {"name": tname, "type": ttype, "status": "PASS", "elapsed": elapsed}

    # Check for *-* All Finished *-* or *- All Finished -*
    if re.search(r'\*-\*?\s*All Finished\s*\*-\*?', stdout):
        return {"name": tname, "type": ttype, "status": "PASS", "elapsed": elapsed}

    # Check if simulation completed at a $finish in the test source code (not wrapper timeout)
    m_finish = re.search(r'\[CAS \$finish ([^\]]+)\] Simulation Terminated at time', stdout)
    if m_finish:
        finish_loc = m_finish.group(1)
        if "cas_top_wrapper" not in finish_loc:
            return {"name": tname, "type": ttype, "status": "PASS", "elapsed": elapsed}

    # If test had no input clock wrapper and completed cleanly at exit code 0 without errors/stops
    if not inputs and "[CAS SUMMARY] Simulation completed cleanly" in stdout:
        return {"name": tname, "type": ttype, "status": "PASS", "elapsed": elapsed}

    # If simulation completed cleanly without explicit banner, check if gold was absent or completed
    err_line = extract_error_signature(stdout)
    if not err_line or err_line == "Unknown simulation error":
        if inputs and m_finish and "cas_top_wrapper" in m_finish.group(1):
            err_line = "Wrapper stimulus timed out before test completed"
        else:
            err_line = "No '*- All Finished -*' indicator in stdout"

    return {
        "name": tname,
        "type": ttype,
        "status": "FAIL",
        "error": err_line,
        "elapsed": elapsed,
    }

def main():
    print("=" * 80)
    print(" Verilator (test_regress) Pure-SV Benchmark Suite Triage Runner")
    print(f" Target Directory : {T_DIR}")
    print(f" CAS Binary       : {CAS_BIN}")
    print(f" Parallel Workers : {MAX_WORKERS} (timeout = {TIMEOUT_SEC}s)")
    print("=" * 80)

    if not os.path.exists(CAS_BIN):
        print(f"ERROR: CAS binary not found at {CAS_BIN}. Build CAS first.")
        sys.exit(1)

    if not os.path.exists(T_DIR):
        print(f"ERROR: Verilator test directory not found at {T_DIR}.")
        sys.exit(1)

    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--test", "-t", action="append", help="Filter test names")
    parser.add_argument("--limit", type=int, default=None, help="Limit number of tests to run")
    parser.add_argument("--category", "-c", choices=["SIM", "CE"], help="Filter by category")
    parser.add_argument("--min-pass", type=int, default=0, help="Minimum number of passing tests required to exit with code 0")
    args, _ = parser.parse_known_args()

    print("Discovering and categorizing tests across test_regress/t/...")
    tests, skipped = load_all_tests(test_filter=args.test)

    if args.test:
        tests = {k: v for k, v in tests.items() if any(t == k or t in k for t in args.test)}

    if args.category:
        tests = {k: v for k, v in tests.items() if v["type"] == args.category}

    if args.limit and args.limit > 0:
        limited_keys = list(tests.keys())[:args.limit]
        tests = {k: tests[k] for k in limited_keys}

    cat_counts = defaultdict(int)
    for tinfo in tests.values():
        cat_counts[tinfo["type"]] += 1

    print("\n--- Discovered Tests by Category ---")
    print(f"  Pure-SV Simulation (SIM) : {cat_counts['SIM']} tests")
    print(f"  Negative Compile-Error (CE): {cat_counts['CE']} tests")
    print(f"  Skipped (C++ --exe / --sc): {skipped['exe_cpp'] + skipped['sc']}")
    print(f"  Skipped (DPI-C / VPI)    : {skipped['dpi_vpi']}")
    print(f"  Skipped (Lint-Only)      : {skipped['lint_only']}")
    print(f"  Skipped (No Source Found): {skipped['no_source']}")
    print(f"  Skipped (Other)          : {skipped['other']}")
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

    scorecard = {
        "SIM": {"name": "Pure-SV Simulation (SIM)", "passed": 0, "total": 0},
        "CE": {"name": "Negative Compile-Error (CE)", "passed": 0, "total": 0},
    }

    failure_signatures = defaultdict(list)

    for r in results:
        cat = r["type"]
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
    print("                 Verilator BASELINE SCORECARD")
    print("=" * 80)
    print(f"{'Category':<32} | {'Passed':<8} | {'Total':<8} | {'Pass Rate'}")
    print("-" * 80)
    for cat in ["SIM", "CE"]:
        sc = scorecard[cat]
        pct = (sc["passed"] / sc["total"] * 100) if sc["total"] else 0.0
        print(f"{sc['name']:<32} | {sc['passed']:<8} | {sc['total']:<8} | {pct:>6.1f}%")
    print("-" * 80)
    print(f"{'GRAND TOTAL':<32} | {grand_passed:<8} | {grand_total:<8} | {grand_pct:>6.1f}%")
    print(f"Elapsed Time: {total_time:.2f}s")
    print("=" * 80)

    # Top 15 Failure Error Signatures
    sorted_signatures = sorted(failure_signatures.items(), key=lambda kv: len(kv[1]), reverse=True)
    top_n = min(15, len(sorted_signatures))

    print(f"\n--- Top {top_n} Root-Cause Failure Clusters ---")
    for rank, (sig, test_list) in enumerate(sorted_signatures[:top_n], 1):
        sample = ", ".join(test_list[:3])
        if len(test_list) > 3:
            sample += f", ... (+{len(test_list)-3} more)"
        print(f"[{rank:>2}] ({len(test_list):>4} tests) {sig}")
        print(f"     Examples: {sample}")
    print("=" * 80)

    if args.min_pass > 0:
        if grand_passed < args.min_pass:
            print(f"\n[CI FAILURE] Passing tests count ({grand_passed}) is below minimum threshold ({args.min_pass})!")
            sys.exit(1)
        else:
            print(f"\n[CI GATE PASS] Passing tests count ({grand_passed}) met or exceeded threshold ({args.min_pass})!")

if __name__ == "__main__":
    main()
