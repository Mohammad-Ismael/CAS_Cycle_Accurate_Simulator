# CAS: Cycle-Accurate Simulator

**Current Version:** `v0.8.0`  
**License:** MIT  
**Target Standard:** IEEE 1800 SystemVerilog (subset)

CAS is an independent, ground-up discrete-event RTL simulator implemented in C++17. It operates without external compiler chains (LLVM, intermediate C++ transpilation) or third-party frontends. CAS parses SystemVerilog sources directly into an in-memory Abstract Syntax Tree (AST), elaborates arbitrary-depth hierarchical netlist graphs, and executes simulation events using an IEEE 1800-inspired stratified scheduler (Active, NBA, Combinational Convergence, and Time Wheel).

---

## Architectural Highlights

* **Multi-Way Control Flow Selection (`case` / `endcase`):** Native statement-level branch selection supporting value matching, `default` fallbacks, and multi-statement `begin ... end` case items.
* **Vector Bit-Select & Part-Select Slicing:** Dynamic scalar extraction (`wire[idx]`) and fixed range slicing (`wire[msb:lsb]`) with dual-rail 4-state bit alignment.
* **Full ALU Operator Suite & Expression Precedence:** Operator precedence climbing supporting bitwise (`&`, `|`, `^`, `~`), logical shifts (`<<`, `>>`), relational/equality (`==`, `!=`, `<`, `<=`, `>`, `>=`), logical short-circuit gates (`&&`, `||`), and ternary conditional evaluation (`cond ? true_expr : false_expr`).
* **Recursive Multi-Level Module Elaboration:** Fully recursive elaborator traversing arbitrary submodule instantiation depths (`top -> dut -> submod -> ...`), dynamically instantiating scoped nets and binding bidirectional port interconnects.
* **Dual-Rail 4-State Bitwise Evaluation:** Exact per-bit IEEE 1800 evaluation rules for unknown bits (`0 & X = 0`, `1 | X = 1`, `X ^ X = X`, `~X = X`), preserving hardware-accurate uninitialized propagation across wide datapath buses.
* **Native 4-State Logic Engine (`0, 1, X, Z`):** Dual-rail bit-vector arithmetic (`aval` data, `bval` unknown mask). All un-driven nets and uninitialized registers initialize to `X` (unknown) rather than optimistic `0`, catching un-reset design bugs.
* **Self-Checking Regression Suite:** Automated verification harness covering all milestone deliverables from `v0.1.0` through `v0.8.0` with diagnostic pass/fail accounting.
* **Project-Centric Workspace Engine:** Isolated directory hierarchies (`projects/<name>/`) storing RTL designs (`design.sv`), testbenches (`testbench.sv`), metadata (`project.json`), and archived waveform traces (`waves/`).
* **Persistent Waveform History:** Timestamped VCD trace runs stored per project with in-browser selection, deletion, and immediate canvas invalidation when dump directives are omitted.
* **4-State Digital Timing Canvas:** Native HTML5 Canvas timing diagram renderer displaying clocks, single-bit logic wires, and multibit transition buses with distinct visual styles for known values vs. `X`/`Z` states.
* **Native VCD Waveform Generation:** Direct export of IEEE 1364/1800 Value Change Dump (`.vcd`) traces supporting `$dumpfile` and `$dumpvars` with hierarchical scope definitions and compact ASCII symbol mapping.
* **Stratified IEEE 1800 Scheduling (Active vs. NBA):** Evaluates non-blocking assignments (`<=`) during the active region and buffers values into an NBA update queue, committing all register transitions simultaneously before triggering combinational delta passes.
* **Edge-Triggered Sensitivity Engine:** Dynamic transition tracking (`is_posedge()`, `is_negedge()`) across hierarchical signal handles for `always_ff` blocks with asynchronous resets (`X -> 0` correctly triggers `negedge`).
* **Native Discrete-Event Time Wheel:** Min-heap priority queue (`std::priority_queue`) that advances simulation time dynamically across `#delay` intervals.

---

## Regression Verification Suite (`projects/regression/`)

The repository includes a comprehensive self-checking regression suite that evaluates the simulation kernel across all language and architectural features prior to releases:

```bash
./CAS/build/cas projects/regression/testbench.sv projects/regression/design.sv

```

```text
CAS (Cycle Accurate Simulator) v0.8.0 [Control Flow Kernel] Initiated
Hierarchical Nets Elaborated: 27
Port Interconnects Bound: 15
--------------------------------------------------------
[CAS $display] ========================================================
[CAS $display]    CAS COMPREHENSIVE REGRESSION SUITE (v0.1.0 - v0.8.0) [CAS$display] ========================================================
[CAS $display] [PASS] v0.6.0: Uninitialized register holds 'X' prior to reset (raw=x).
[CAS $display] [PASS] v0.3.0: Time Wheel dynamic event scheduler & $time valid at T=5. [CAS$display] [PASS] v0.4.0: Asynchronous reset edge (X -> 0) cleared register to 0.
[CAS $display] [PASS] v0.2.0: Submodule port interconnect & delta comb pass verified (alu_result=8). [CAS$display] [PASS] v0.8.0: Case branch 3'd1 (|) selected correctly (res=1110).
[CAS $display] [PASS] v0.8.0: Case branch 3'd4 (<<) selected correctly (res=0110). [CAS$display] [PASS] v0.8.0: Bit-select a[2]=1 and part-select a[3:2]=11 verified.
[CAS $display] [PASS] v0.4.0: Stratified NBA assignment commit loop verified (accum=14). [CAS$display] [PASS] v0.5.0: Native VCD generation running without runtime faults.
[CAS $display] [PASS] v0.1.0: AST generation, literals, and system tasks verified.
[CAS $display] ========================================================
[CAS $display]    VERDICT: ALL TESTS PASSED (CAS READY FOR COMMIT)     
[CAS $display] ========================================================
[CAS $finish] Simulation Terminated at time 60.
[CAS VCD] Trace successfully dumped to: regression_suite.vcd
--------------------------------------------------------
Simulation finished at sim_time = 60.

```

---

## Build and Execution

### Build Statically-Linked Binary via Docker

```bash
docker run --rm -v "${PWD}:/app" -w /app/CAS alpine:3.20 sh -c \
    "apk add --no-cache build-base cmake && cmake -B build -DCMAKE_EXE_LINKER_FLAGS='-static' && cmake --build build"

```

### Run Native CLI Binary

```bash
./CAS/build/cas path/to/testbench.sv path/to/design.sv

```

### Launch Web Studio IDE

```bash
docker run --rm -p 8080:8080 -v "${PWD}:/app" eda-web

```

Access `http://localhost:8080` in the browser.

---

## Roadmap

Detailed release history is tracked in [`VERSIONS.md`](VERSIONS.md).

| Target Version | Milestone Feature Focus | Status |
| --- | --- | --- |
| **`v0.1.0`** | Standalone AST, dynamic netlist, flat delta-cycle loop, `$display`, `$finish`. | **Released** |
| **`v0.2.0`** | Hierarchical module instantiation, named port mapping (`.port(wire)`), cross-boundary net propagation. | **Released** |
| **`v0.3.0`** | Priority-queue event scheduler, `#delay` statements, cooperative process execution, and `$time`. | **Released** |
| **`v0.4.0`** | Sequential logic, IEEE 1800 Active vs. NBA event regions, `always_ff @(posedge/negedge)`, non-blocking assignments (`<=`). | **Released** |
| **`v0.5.0`** | Native VCD generation (`$dumpfile`, `$dumpvars`), studio IDE dark theme, and HTML5 canvas waveform engine. | **Released** |
| **`v0.5.1`** | Project workspace architecture (`projects/`), persistent trace history stack, and strict canvas state invalidation. | **Released** |
| **`v0.6.0`** | 4-State Logic Engine (`0, 1, X, Z`): Dual-rail bit vectors (`aval` + `bval`), unknown poisoning, 4-state edge transitions. | **Released** |
| **`v0.7.0`** | Extended ALU operators (`&`, `|`, `^`, `~`, `<<`, `>>`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `||`, `?:`), recursive elaboration, and regression suite. | **Released** |
| **`v0.8.0`** | **Control Flow & Slicing:** `case`, `default` branch selection, dynamic bit-select (`wire[idx]`), and part-select slicing (`wire[msb:lsb]`). | **Released** |
| **`v0.9.0`** | **Memory Arrays & Continuous Assignments:** Multi-element memory arrays (`logic [31:0] mem [0:255]`), dynamic array read/write, and continuous `assign` statements. | *Next* |
| **`v1.0.0`** | **Benchmarking:** Cycle-accurate execution of open-source RISC-V cores with automated throughput comparisons against commercial simulators. | *Planned* |
