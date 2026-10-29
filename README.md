# CAS: Cycle-Accurate Simulator

**Current Version:** `v2.3.0.0`  
**Status:** Production Milestone v2.3.0.0 Clean (100% Synopsys VCS Golden Parity)  
**License:** MIT  
**Target Standard:** IEEE Std 1800-2023 SystemVerilog  

CAS (Cycle-Accurate Simulator) is an independent, ground-up discrete-event RTL simulator and integrated verification development environment implemented in modular C++17. Operating independently of external compiler toolchains (such as LLVM, GCC intermediate compilation, or transpilation to C++), CAS parses SystemVerilog directly into an in-memory Abstract Syntax Tree (AST), elaborates hierarchical design topologies of arbitrary depth, and executes discrete-event simulations using an IEEE Std 1800-2023 stratified event scheduler.

The system includes a containerized execution environment paired with a Monaco-based multi-file IDE, interactive waveform viewer, source-linked diagnostic console, and hardware profiler.

---

## Supported IEEE Std 1800-2023 Language Features

| Standard Section | Feature Capability | Simulator Implementation |
|---|---|---|
| **§4** | **Stratified Event Scheduler** | Pre-Active, Active, Inactive, NBA, Observed, Reactive, and Postponed regions with deterministic delta loops. |
| **§6.5, §10.3, §10.4.1** | **Net Types & Multi-Driver Resolution** | Net keywords `wire`, `wand`, `wor`, `tri` with bitwise 4-state resolution functions (Tables 6-1, 6-2, 6-3). Concurrent continuous assignments resolve dynamically. Un-driven nets float to `'z'`. |
| **§6.5, §10.3.2** | **Variable Exclusivity Enforcement** | Variables (`logic`, `reg`, `var`) reject multiple continuous assignment drivers with `[CAS-MCD]`. |
| **§6.7** | **4-State Logic System** | Dual-rail 64-bit integer vectors (`aval` data, `bval` control) supporting `0`, `1`, `X`, and `Z`. |
| **§9.2.2.4** | **Driver Exclusivity Enforcement** | Variables driven inside `always_ff`, `always_comb`, or `always_latch` reject multi-process or continuous drives (`[CAS-ICPD]`). |
| **§9.3.2** | **Parallel Blocks (`fork ... join`)** | `fork ... join` (all complete), `fork ... join_any` (any completes, background continuation), `fork ... join_none` (non-blocking concurrent dispatch). Dynamic thread tree with inherited lexical scopes and call frames. |
| **§9.4** | **Procedural Event Controls** | Level and edge-triggered events (`posedge`, `negedge`), asynchronous reset sensitivity lists, and named events. |
| **§9.6.2, §9.6.3** | **Process Control Statements** | `disable fork` (recursive descendant thread cancellation) and `wait fork` (parent suspension until all active descendants complete). |
| **§10.4.2** | **Procedural Continuous Assignments** | Procedural `assign` and `deassign` on variables with dynamic operand sensitivity and procedural write suppression. |
| **§10.6.1** | **Intra-Assignment Delays** | Timing controls within procedural assignments (`a = #delay expr;` and `a <= #delay expr;`). |
| **§10.6.2** | **Event-Based Intra-Assignments** | Intra-assignment event controls (`a = @(edge sig) expr;` and `a <= @(edge sig) expr;`). |
| **§11.4.12** | **Reduction Operators** | Bitwise unary reduction operators: `&`, `~&`, `|`, `~|`, `^`, `~^`, `^~`. |
| **§11.6, §11.8** | **Expression Width & Signed Arithmetic** | Context-determined bit-width expansion, 2's complement sign extension, `$signed()`, and arithmetic shift (`>>>`). |
| **§12.5** | **Selection & Wildcard Statements** | Exact `case`, `casez` (high-Z/`?` don't-cares), `casex` (X/Z/`?` don't-cares), with standard priority resolution. |
| **§12.7, §13.4** | **Procedural Loops & Subroutines** | `for`, `while`, `repeat`, `forever` loops, procedural `task` blocks, and typed `function` subroutines. |
| **§13.4.2, §13.5.1** | **Automatic Routines & Call Frames** | `automatic` functions with recursive activation frames, concurrent re-entrant tasks with local frame isolation across `#delay` intervals. |
| **§21.2** | **System Tasks & I/O** | `$display`, `$monitor` (Postponed region), `$time`, `$finish`, `$dumpfile`, `$dumpvars` with 4-state hex/bin formatters. |

---

## Milestone Regression Pass Matrix (14 / 14 PASS)

All milestone regression testbenches are self-checking and verified against Synopsys VCS 2025.06-SP1 golden reference simulation runs:

| Milestone | Benchmark Suite | Focus & Standard Target | Status | Parity |
|:---:|:---|:---|:---:|:---:|
| **v1.0.0.0** | `projects/v1_0_0_0_shift_reg_core` | Shift Registers & Stratified NBA Pipeline (§4) | **PASS** | 100% VCS |
| **v1.1.0.0** | `projects/v1_1_0_0_four_state_core` | 4-State Bitwise (`0, 1, X, Z`) Propagation (§6.7) | **PASS** | 100% VCS |
| **v1.2.0.0** | `projects/v1_2_0_0_width_sign_core` | Context-Determined Width & Signed Operations (§11.6, §11.8) | **PASS** | 100% VCS |
| **v1.3.0.0** | `projects/v1_3_0_0_loop_func_core` | Procedural Loops (`for`, `while`, `repeat`) & Functions (§12.7, §13.4) | **PASS** | 100% VCS |
| **v1.4.0.0** | `projects/v1_4_0_0_reduction_core` | Bitwise Unary Reduction Operators (§11.4.12) | **PASS** | 100% VCS |
| **v1.5.0.0** | `projects/v1_5_0_0_event_ctrl_core` | Sensitivity Lists, Multi-Edge Posedge/Negedge Controls (§9.4) | **PASS** | 100% VCS |
| **v1.6.0.0** | `projects/v1_6_0_0_intra_assign_core` | Intra-Assignment Delays (`#delay` in NBA/blocking) (§10.6.1) | **PASS** | 100% VCS |
| **v1.7.0.0** | `projects/v1_7_0_0_casex_casez` | Wildcard Selection Statements (`casez`, `casex`) (§12.5.1, §12.5.2) | **PASS** | 100% VCS |
| **v1.8.0.0** | `projects/v1_8_0_0_procedural_continuous` | Procedural Continuous (`assign`/`deassign`) & Exclusivity (`[CAS-ICPD]`) (§10.4.2, §9.2.2.4) | **PASS** | 100% VCS |
| **v1.9.0.0** | `projects/v1_9_0_0_intra_event` | Event-Based Intra-Assignments (`@(edge sig)`) (§10.6.2) | **PASS** | 100% VCS |
| **v2.0.0.0** | `projects/v2_0_0_0_multi_driver_resolution` | Net Types (`wire`, `wand`, `wor`, `tri`) & Multi-Driver Resolution (§6.5, §10.3) | **PASS** | 100% VCS |
| **v2.1.0.0** | `projects/v2_1_0_0_automatic_routines` | Automatic Functions, Re-entrant Tasks, & Call Frames (§13.4.2, §13.5.1) | **PASS** | 100% VCS |
| **v2.2.0.0** | `projects/v2_2_0_0_fork_join` | Parallel Blocks (`fork ... join`, `join_any`, `join_none`) (§9.3.2) | **PASS** | 100% VCS |
| **v2.3.0.0** | `projects/v2_3_0_0_process_control` | Process Controls (`disable fork`, `wait fork`) (§9.6.2, §9.6.3) | **PASS** | 100% VCS |

---

## Architectural Highlights

### 1. Dual-Rail 4-State Logic Engine (`0, 1, X, Z`)
* **Bit Vector Representation:** Logic states are tracked via pairs of 64-bit integer bitplanes: `aval` (data payload) and `bval` (unknown/high-Z mask). Known states (`0`, `1`) hold `bval == 0`. Uninitialized nets, high-impedance pins, and indeterminate transitions hold `bval == 1`.
* **Hardware-Accurate X-Poisoning:** Strict IEEE Std 1800-2023 unknown propagation:
  * `0 & X = 0` (definite zero dominance)
  * `1 | X = 1` (definite one dominance)
  * `X ^ X = X`, `~X = X`, `X + val = X`
* **Safe Power-On Initialization:** 4-state variables (`logic`, `reg`) power on in the indeterminate `X` state (`bval=1, aval=1`), while un-driven net types (`wire`, `wand`, `wor`, `tri`) initialize and float in the high-impedance `Z` state (`bval=1, aval=0`).

### 2. Multi-Driver Net Resolution Engine
* **Branchless 64-bit Parallel Primitives:** High-performance bitplane resolution functions implementing IEEE Std 1800-2023 Tables 6-1, 6-2, and 6-3.
* **Wired AND (`wand` / `triand`):** `0` dominates all drivers; `Z` acts as a neutral pull-up.
* **Wired OR (`wor` / `trior`):** `1` dominates all drivers; `Z` acts as a neutral pull-down.
* **Wired Interconnect (`wire` / `tri`):** High-Z floats yield to driven values; conflicting driven states resolve to `X`.
* **Driver Exclusivity:** Compile-time elaboration catches and rejects multiple continuous drivers targeting variables with `[CAS-MCD]`.

### 3. Stratified IEEE Std 1800-2023 Event Scheduler
* **Stratified Event Regions:** Pre-Active -> Active -> Inactive -> NBA -> Observed -> Reactive -> Postponed.
* **Active vs. NBA Event Partitioning:** Procedural non-blocking assignments (`<=`) sample RHS values during the Active region and enqueue updates into an NBA transaction queue, committing synchronously in the NBA region before subsequent delta combinational passes.
* **Combinational Delta Loop:** Iterative fixed-point propagation continuously recalculates gate primitives, continuous assignments, and hierarchical slice bindings until global topological convergence is reached.
* **Postponed Sampling:** `$monitor` values are sampled exclusively in the Postponed region at the conclusion of each time slot, guaranteeing 100% line-for-line output alignment with golden EDA tools.

### 4. Direct AST & Elaboration Hierarchy
* Direct AST generation with recursive ``include` file handling.
* Hierarchical scoping (`top.submod.sig`) with automatic bidirectional slice bindings for port interconnections.
* Driver exclusivity checking during elaboration prevents multi-process write conflicts on sequential and combinational variables (`[CAS-ICPD]`).

---

## Repository Structure

```text
CAS_Cycle_Accurate_Simulator/
├── CAS/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── common/             # Common definitions (types, logic_val, net_types, edge_types, gate_types)
│   │   ├── frontend/           # AST nodes, lexer, and parser headers
│   │   ├── kernel/             # Evaluator, scheduler, elaborator, netlist, logic_ops
│   │   ├── profiler/           # Runtime hardware resource profiler
│   │   └── tracer/             # VCD trace dumper
│   └── src/                    # Implementation sources
├── projects/                   # Milestone regression suites (v1_0_0_0 through v2_3_0_0)
├── versions/                   # Milestone release documentation (v1.0.0.0.md through v2.3.0.0.md)
├── server.py                   # Containerized FastAPI simulation and IDE server
├── Dockerfile                  # Production container definition
└── README.md                   # Simulator architecture and documentation
```

---

## Build and Execution

### 1. Build Native Executable (via Docker)

```powershell
docker exec -w /app/CAS/build cas_dev make
```

Or from scratch:
```powershell
docker run --rm -v "${PWD}:/app" -w /app/CAS alpine:3.20 sh -c "apk add --no-cache build-base cmake && cmake -B build && cmake --build build"
```

### 2. Run CLI Simulator

```powershell
# Run a multi-file project directly:
./CAS/build/cas projects/v2_3_0_0_process_control/testbench.sv projects/v2_3_0_0_process_control/design.sv

# Command-line options:
#   --quiet        Disable compiler phase logging
#   --no-profile   Bypass kernel profiling and telemetry metrics
#   --no-vcd       Suppress VCD trace dumping
#   --no-display   Suppress $display output
#   --no-monitor   Suppress $monitor output
#   --top <name>   Manually specify top-level root module
#   --timescale <s> Specify time precision (e.g., 1ns, 100ps)
```

### 3. Run All Milestone Regressions

```powershell
python -c "
import subprocess, os
suites = ['v1_0_0_0_shift_reg_core', 'v1_1_0_0_four_state_core', 'v1_2_0_0_width_sign_core',
          'v1_3_0_0_loop_func_core', 'v1_4_0_0_reduction_core', 'v1_5_0_0_event_ctrl_core',
          'v1_6_0_0_intra_assign_core', 'v1_7_0_0_casex_casez', 'v1_8_0_0_procedural_continuous',
          'v1_9_0_0_intra_event', 'v2_0_0_0_multi_driver_resolution', 'v2_1_0_0_automatic_routines', 'v2_2_0_0_fork_join', 'v2_3_0_0_process_control']
for s in suites:
    cmd = ['docker', 'exec', '-w', f'/app/projects/{s}', 'cas_dev', '/app/CAS/build/cas', 'testbench.sv', 'design.sv']
    r = subprocess.run(cmd, capture_output=True, text=True)
    status = 'PASS' if ('PASS' in r.stdout and r.returncode == 0) else 'FAIL'
    print(f'[{status}] {s}')
"
```
