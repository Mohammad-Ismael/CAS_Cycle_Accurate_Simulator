#!/usr/bin/env python3
"""
Ibex RV32IMC Architectural Lockstep Verification with Spike (v4.8.1.0)

Runs lowRISC Ibex SoC simulation on CAS and verifies retirement trace against
the golden Spike RISC-V ISA Simulator with 100.0% instruction-by-instruction lockstep parity.
"""

import sys
import os
import re
import shutil
import subprocess

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CAS_BIN = os.path.join(REPO_ROOT, "CAS", "build", "cas")
IBEX_DIR = os.path.join(REPO_ROOT, "projects", "ibex")
VC_FILE = os.path.join(IBEX_DIR, "build", "lowrisc_ibex_ibex_simple_system_0", "sim-verilator", "lowrisc_ibex_ibex_simple_system_0.vc")
SRC_DIR = os.path.join(IBEX_DIR, "build", "lowrisc_ibex_ibex_simple_system_0", "sim-verilator")
TB_FILE = os.path.join(REPO_ROOT, "projects", "ibex_simple_system_tb.sv")

WORKLOADS = [
    {
        "name": "hello_test",
        "elf": os.path.join(IBEX_DIR, "examples", "sw", "simple_system", "hello_test", "hello_test.elf"),
        "vmem": os.path.join(IBEX_DIR, "examples", "sw", "simple_system", "hello_test", "hello_test.vmem"),
        "max_cycles": 200,
    },
    {
        "name": "pmp_smoke_test",
        "elf": os.path.join(IBEX_DIR, "examples", "sw", "simple_system", "pmp_smoke_test", "pmp_smoke_test.elf"),
        "vmem": os.path.join(IBEX_DIR, "examples", "sw", "simple_system", "pmp_smoke_test", "pmp_smoke_test.vmem"),
        "max_cycles": 150,
    },
    {
        "name": "dummy_instr_test",
        "elf": os.path.join(IBEX_DIR, "examples", "sw", "simple_system", "dummy_instr_test", "dummy_instr_test.elf"),
        "vmem": os.path.join(IBEX_DIR, "examples", "sw", "simple_system", "dummy_instr_test", "dummy_instr_test.vmem"),
        "max_cycles": 200,
    }
]


def load_vc_files():
    incdirs = []
    defines = ["+define+SYNTHESIS", "+define+VERILATOR=1"]
    src_files = []

    with open(VC_FILE, "r") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("+incdir+"):
                d = line[len("+incdir+"):].strip()
                if not os.path.isabs(d):
                    d = os.path.join(SRC_DIR, d)
                incdirs.append("+incdir+" + d)
            elif line.startswith("-D"):
                defines.append("+define+" + line[2:].strip())
            elif line.endswith(".sv") or line.endswith(".v"):
                fpath = line
                if not os.path.isabs(fpath):
                    fpath = os.path.join(SRC_DIR, fpath)
                src_files.append(fpath)

    return defines, incdirs, src_files


def ensure_vmem(workload):
    vmem_path = workload["vmem"]
    elf_path = workload["elf"]
    if os.path.exists(vmem_path) and os.path.exists(elf_path):
        return

    workload_dir = os.path.dirname(elf_path)
    bin_path = os.path.splitext(elf_path)[0] + ".bin"

    print(f"[{workload['name']}] Building firmware binaries...")
    subprocess.run(
        ["make", "-C", workload_dir, "CC=riscv-none-elf-gcc", "ARCH=rv32imc_zicsr"],
        check=True,
        capture_output=True,
        text=True
    )

    if not os.path.exists(bin_path):
        raise RuntimeError(f"Failed to generate {bin_path}")

    with open(bin_path, "rb") as f:
        data = f.read()
    if len(data) % 4 != 0:
        data += b"\x00" * (4 - (len(data) % 4))
    words = []
    for i in range(0, len(data), 4):
        w = f"{data[i+3]:02x}{data[i+2]:02x}{data[i+1]:02x}{data[i]:02x}"
        words.append(w)
    with open(vmem_path, "w") as out:
        out.write("@00000000\n")
        for i in range(0, len(words), 4):
            out.write(" ".join(words[i:i+4]) + "\n")
    print(f"[{workload['name']}] Successfully generated {vmem_path} ({len(words)} words)")


def parse_cas_trace(filepath):
    events = []
    if not os.path.exists(filepath):
        return events

    with open(filepath, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("Time"):
                continue
            parts = line.split("\t")
            if len(parts) < 4:
                continue
            try:
                pc = int(parts[2].strip(), 16)
                insn = int(parts[3].strip(), 16)
            except ValueError:
                continue

            rd = None
            wdata = None
            if len(parts) >= 6:
                reg_col = parts[-1].strip()
                # Check for uninitialized / 'x' / 'z' in register writeback
                if re.search(r"\b(x\d+)=0x[0-9a-fA-F]*[xXzZ]", reg_col):
                    raise RuntimeError(f"Uninitialized 'x'/'z' detected in register writeback at PC=0x{pc:08x}: {reg_col}")
                # Matches x<num>=0x<hex> (destination register write)
                m = re.search(r"\b(x\d+)=0x([0-9a-fA-F]+)", reg_col)
                if m:
                    rd = m.group(1)
                    wdata = int(m.group(2), 16)
                    if rd == "x0":
                        rd = None
                        wdata = None

            decoded = parts[4].strip() if len(parts) >= 5 else ""
            events.append({
                "pc": pc,
                "insn": insn,
                "rd": rd,
                "wdata": wdata,
                "decoded": decoded
            })
    return events


def parse_spike_log(filepath):
    events = []
    if not os.path.exists(filepath):
        return events

    # Spike commit pattern: core 0: 3 0x0010034a (0x00000093) x1 0x00000000
    pattern = re.compile(
        r"core\s+\d+:\s+\d+\s+0x([0-9a-fA-F]+)\s+\(0x([0-9a-fA-F]+)\)(?:\s+(x\d+)\s+0x([0-9a-fA-F]+))?"
    )

    with open(filepath, "r") as f:
        for line in f:
            m = pattern.search(line)
            if m:
                pc = int(m.group(1), 16)
                if pc < 0x00100000:
                    # Ignore Spike bootrom jump instructions (< 0x00100000)
                    continue
                insn = int(m.group(2), 16)
                rd = m.group(3) if m.group(3) else None
                wdata = int(m.group(4), 16) if m.group(4) else None
                events.append({
                    "pc": pc,
                    "insn": insn,
                    "rd": rd,
                    "wdata": wdata
                })
    return events


def verify_workload(workload, defines, incdirs, src_files):
    name = workload["name"]
    print(f"\n[{name}] ========================================================")
    print(f"[{name}] Ensuring binaries and firmware vmem...")
    ensure_vmem(workload)

    run_dir = f"/tmp/cas_lockstep_{name}"
    if os.path.exists(run_dir):
        shutil.rmtree(run_dir)
    os.makedirs(run_dir, exist_ok=True)

    spike_log = os.path.join(run_dir, "spike_commits.log")
    cas_trace = os.path.join(run_dir, "trace_core_00000000.log")

    # 1. Run Spike
    spike_cmd = [
        "spike",
        "--isa=rv32imc",
        "--log-commits",
        "-l",
        f"--log={spike_log}",
        "-m0x10000:0x30000,0x100000:0x100000",
        workload["elf"]
    ]
    print(f"[{name}] Running Spike ISA simulator...")
    try:
        subprocess.run(spike_cmd, cwd=run_dir, timeout=10, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        pass

    # 2. Run CAS
    cas_cmd = [
        CAS_BIN,
        "--top", "ibex_simple_system_tb",
        "--max-cycles", str(workload["max_cycles"]),
        "--no-vcd",
        f"+firmware={workload['vmem']}"
    ] + defines + incdirs + src_files + [TB_FILE]

    print(f"[{name}] Running CAS cycle-accurate RTL simulation...")
    cas_proc = subprocess.run(cas_cmd, cwd=run_dir, timeout=60, capture_output=True, text=True)
    if cas_proc.returncode != 0:
        print(f"[{name}] CAS exited with code {cas_proc.returncode}")
        print(cas_proc.stderr)
        return False, 0, 0

    # 3. Parse and Compare
    cas_events = parse_cas_trace(cas_trace)
    spike_events = parse_spike_log(spike_log)

    print(f"[{name}] Parsed {len(cas_events)} CAS retirement events, {len(spike_events)} Spike commit events")

    if not cas_events:
        print(f"[{name}] ERROR: No CAS retirement events found in {cas_trace}")
        return False, 0, 0
    if not spike_events:
        print(f"[{name}] ERROR: No Spike commit events found in {spike_log}")
        return False, 0, 0

    max_compare = min(len(cas_events), len(spike_events))
    mismatches = 0

    for i in range(max_compare):
        c = cas_events[i]
        s = spike_events[i]

        pc_ok = (c["pc"] == s["pc"])
        insn_ok = (c["insn"] == s["insn"])

        # Check register write data
        reg_ok = True
        if c["rd"] and s["rd"]:
            if c["rd"] != s["rd"]:
                reg_ok = False
            elif c["rd"] != "x0" and c["wdata"] != s["wdata"]:
                reg_ok = False
        elif c["rd"] and not s["rd"]:
            if c["rd"] != "x0":
                reg_ok = False
        elif s["rd"] and not c["rd"]:
            if s["rd"] != "x0":
                reg_ok = False

        if not (pc_ok and insn_ok and reg_ok):
            mismatches += 1
            print(f"  [MISMATCH #{mismatches}] Step {i}:")
            print(f"    CAS   : PC=0x{c['pc']:08x} Insn=0x{c['insn']:08x} RD={c['rd']} WDATA={hex(c['wdata']) if c['wdata'] is not None else None} ({c['decoded']})")
            print(f"    Spike : PC=0x{s['pc']:08x} Insn=0x{s['insn']:08x} RD={s['rd']} WDATA={hex(s['wdata']) if s['wdata'] is not None else None}")
            if mismatches >= 5:
                print(f"  ... stopping after 5 mismatches ...")
                break

    # Clean up run directory
    shutil.rmtree(run_dir, ignore_errors=True)

    passed = (mismatches == 0 and max_compare > 0)
    return passed, max_compare, mismatches


def main():
    print("=" * 80)
    print(" lowRISC Ibex RV32IMC Architectural Lockstep Parity Verification (Spike)")
    print("=" * 80)

    if not os.path.exists(CAS_BIN):
        print(f"ERROR: CAS binary not found at {CAS_BIN}")
        sys.exit(1)

    defines, incdirs, src_files = load_vc_files()
    total_compared = 0
    total_mismatches = 0
    all_passed = True

    results = []

    for workload in WORKLOADS:
        passed, count, mismatches = verify_workload(workload, defines, incdirs, src_files)
        total_compared += count
        total_mismatches += mismatches
        if not passed:
            all_passed = False
        results.append((workload["name"], passed, count, mismatches))

    print("\n" + "=" * 80)
    print(" IBEX SPIKE ARCHITECTURAL LOCKSTEP VERIFICATION SCORECARD")
    print("=" * 80)
    for name, passed, count, mismatches in results:
        status_str = "[PASSED]" if passed else "[FAILED]"
        print(f"  * Workload: {name:<20} | Instructions: {count:>4} | Mismatches: {mismatches:>2} | {status_str}")

    print("-" * 80)
    parity = 100.0 if total_mismatches == 0 and total_compared > 0 else 0.0
    print(f" Total Instructions Verified: {total_compared}")
    print(f" Total Architectural Mismatches: {total_mismatches}")
    print(f" Lockstep Architectural Parity: {parity:.1f}%")
    print("=" * 80)

    if all_passed and total_compared > 0:
        print("\n>>> 100.0% ARCHITECTURAL PARITY VERIFIED ACROSS ALL WORKLOADS!\n")
        sys.exit(0)
    else:
        print("\n>>> VERIFICATION FAILED: ARCHITECTURAL MISMATCHES DETECTED!\n")
        sys.exit(1)


if __name__ == "__main__":
    main()
