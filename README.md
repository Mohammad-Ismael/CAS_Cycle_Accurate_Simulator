Here are the comprehensive, production-grade documentation files (`README.md` and `VERSIONS.md`) fully aligned with your exact project tree, reflecting the verified features up to `v0.9.3` and the `v0.9.4` active milestone, with no mention of CASDB.

---

### 1. Full File: `README.md`

Replace `README.md` with:

```markdown
# CAS: Cycle-Accurate Simulator

**Current Version:** `v0.9.3`  
**License:** MIT  
**Target Standard:** IEEE 1800 SystemVerilog (subset)

CAS (Cycle-Accurate Simulator) is an independent, ground-up discrete-event RTL simulator and integrated verification development environment implemented in C++17. Operating independently of external compiler toolchains (such as LLVM, GCC intermediate compilation, or intermediate transpilation to C++), CAS parses SystemVerilog directly into an in-memory Abstract Syntax Tree (AST), dynamically elaborates hierarchical design topologies of arbitrary depth, and executes discrete-event simulations using an IEEE 1800-inspired stratified scheduler.

The system includes a containerized web execution server paired with a Monaco-based multi-file IDE, interactive waveform viewer, source-linked diagnostic console, and hardware profiler.

---

## Repository Tree Structure

```text
CAS_Cycle_Accurate_Simulator/
│   .gitignore
│   Dockerfile
│   README.md
│   requirements.txt
│   server.py
│   VERSIONS.md
│
├───CAS
│   │   CMakeLists.txt
│   │
│   ├───include
│   │   │   ast.hpp
│   │   │   engine.hpp
│   │   │   lexer.hpp
│   │   │   netlist.hpp
│   │   │   parser.hpp
│   │   │
│   │   ├───common
│   │   │       types.hpp
│   │   │
│   │   ├───frontend
│   │   │       ast_base.hpp
│   │   │       ast_expr.hpp
│   │   │       ast_module.hpp
│   │   │       ast_stmt.hpp
│   │   │       lexer.hpp
│   │   │       parser.hpp
│   │   │
│   │   ├───kernel
│   │   │       elaborator.hpp
│   │   │       evaluator.hpp
│   │   │       netlist.hpp
│   │   │
│   │   ├───profiler
│   │   │       profiler.hpp
│   │   │
│   │   └───tracer
│   │           vcd_dumper.hpp
│   │
│   └───src
│       │   engine.cpp
│       │   main.cpp
│       │
│       ├───frontend
│       │       lexer.cpp
│       │       parser.cpp
│       │       parser_expr.cpp
│       │       parser_module.cpp
│       │       parser_stmt.cpp
│       │
│       ├───kernel
│       │       elaborator.cpp
│       │       evaluator.cpp
│       │
│       ├───profiler
│       │       profiler.cpp
│       │
│       └───tracer
│               vcd_dumper.cpp
│
├───projects
│   ├───IntegratedDigitalGates/
│   ├───MasterRegression_v100/
│   ├───Non-ANSIHeaderResolutionsupport/
│   ├───Ram/
│   ├───Testingforversion070/
│   ├───V090_Regression_9_Pass/
│   └───v093Regression/
│
└───static
    │   index.html
    │
    ├───css
    │       style.css
    │
    └───js
            app.js
            console.js
            editor.js
            projects.js
            waveform.js

```

---

## Architectural Highlights

### 1. Dual-Rail 4-State Logic Engine (`0, 1, X, Z`)

* **Two-Word Bit Vector Representation:** Logic states are tracked via pairs of 64-bit integer masks: `aval` (data payload) and `bval` (unknown mask). Known states (`0`, `1`) hold `bval == 0`. Uninitialized nets, high-impedance pins, and indeterminate transitions hold `bval == 1`.
* **Hardware-Accurate X-Poisoning:** Implements strict IEEE 1800 unknown propagation:
* `0 & X = 0` (definite zero dominance)
* `1 | X = 1` (definite one dominance)
* `X ^ X = X`, `~X = X`, `X + val = X`
* Relational comparisons against `X` produce `X`.


* **Safe Power-On Initialization:** All un-driven signals, registers without reset assertions, and memory cells power on in the indeterminate `X` state, immediately surfacing uninitialized register bugs.

### 2. Stratified IEEE 1800 Scheduler

* **Active vs. NBA Event Partitioning:** Procedural non-blocking assignments (`<=`) evaluate expressions during active event regions and enqueue updates into an NBA transaction queue. Register states commit synchronously across all processes before delta combinational passes trigger, eliminating delta-cycle race conditions.
* **Combinational Delta Loop:** Iterative fixed-point propagation continuously recalculates gate primitives, continuous assignments, and `always_comb` sensitivity lists until global topological convergence is reached.
* **Discrete-Event Time Wheel:** Min-heap priority queue (`std::priority_queue`) scheduling discrete process wakeups across explicit `#delay` operations and timing markers.
* **Dynamic Edge Detection:** Tracks state transitions across clock boundaries (`posedge`, `negedge`) and supports mixed asynchronous resets (e.g., `always @(posedge clk or negedge rst_n)`).

### 3. Parser & Modular AST Architecture

* **Direct Lexing & Preprocessing:** Custom token stream generator handling string constants, based numeric literals (`4'b1010`, `16'hDEAD`, `8'd42`), comments (`//`, `/* */`), and recursive ``include` file ingestion.
* **Precedence-Climbed Expression Parsing:** Complete evaluation of bitwise (`&`, `|`, `^`, `~`), logical (`&&`, `||`, `!`), arithmetic (`+`, `-`, `*`), relational (`<`, `<=`, `>`, `>=`), equality (`==`, `!=`), shift (`<<`, `>>`), concatenation (`{a, b}`), and ternary conditional (`cond ? a : b`) expressions.
* **Vector Indexing & Slicing:** Scalar indexing (`signal[idx]`) and fixed-range part-slicing (`signal[msb:lsb]`) for multi-bit vectors.
* **Gate Primitives:** Structural gate primitives (`and`, `or`, `xor`, `nand`, `nor`, `xnor`, `not`, `buf`) instantiated directly in module scope.
* **Control Selection:** Multi-branch conditional selection using `case`, `endcase`, and `default` blocks.
* **Procedural Subroutines:** User-defined `task` blocks with internal control logic and prefix delays.

### 4. Memory Modeling & Continuous Dataflow

* **1D Unpacked Memory Arrays:** Native syntax support for register arrays (`logic [W-1:0] mem [0:D-1];`) featuring asynchronous read access (`assign out = mem[addr]`) and synchronous procedural write access (`mem[addr] <= data`).
* **Continuous `assign` Interconnects:** Zero-delay continuous assignments evaluated within the delta loop alongside gate instances.

### 5. Diagnostics, Profiling & VCD Engine

* **Hierarchical VCD Trace Generation:** Emits standard IEEE 1364/1800 Value Change Dump (`.vcd`) traces supporting `$dumpfile` and `$dumpvars` with hierarchy levels and compact identifier hashing.
* **Kernel Resource Profiler:** Real-time resource tracking reporting wallclock duration, user/kernel CPU consumption (via `getrusage`), peak Resident Set Size (RSS), heap netlist allocations, delta passes, and discrete event dispatch throughput.
* **Diagnostic Console Linking:** Standard output (`$display`, `$monitor`, compiler warnings, and runtime errors) contains clickable filename and line-number anchors navigating directly to the source file in the IDE.

---

## Web Studio Environment

The included web environment (`server.py` and `static/`) provides an IDE built on FastAPI and Monaco Editor:

* **Dual-Pane Source Editor:** Independent, synchronized Monaco editors for testbenches and design modules with syntax highlighting, live tabs, and multi-file management.
* **Interactive Waveform Viewer:** Client-side HTML5 canvas timing diagram viewer rendering 1-bit wires, multi-bit buses with hexadecimal labels, cursor tracking, timescale alignment, and export capabilities.
* **Project Persistence:** Projects are saved in isolated workspaces under `projects/<name>/` containing RTL source files, testbench code, VCD waveforms, and compiler settings (`project.json`).

---

## Build and Execution

### 1. Build Native Static Executable (via Docker)

```powershell
docker run --rm -v "${PWD}:/app" -w /app/CAS alpine:3.20 sh -c "apk add --no-cache build-base cmake && cmake -B build -DCMAKE_EXE_LINKER_FLAGS='-static' && cmake --build build"

```

### 2. Run CLI Simulator

```powershell
# Run a multi-file project directly:
./CAS/build/cas projects/MasterRegression_v100/testbench.sv projects/MasterRegression_v100/top_mac.sv

# Command-line options:
#   --quiet        Disable compiler phase logging
#   --no-profile   Bypass kernel profiling and telemetry metrics
#   --no-vcd       Suppress VCD trace dumping
#   --no-display   Suppress $display output
#   --no-monitor   Suppress $monitor output
#   --top <name>   Manually specify top-level root module
#   --timescale <s> Specify time precision (e.g., 1ns, 100ps)

```

### 3. Run Self-Checking Regression Suites

```powershell
# Execute the comprehensive master regression suite:
./CAS/build/cas projects/MasterRegression_v100/testbench.sv

```

### 4. Launch Web Studio

```powershell
# Build and run the local container:
docker build -t eda-web .
docker run --rm -p 8080:8080 -v "${PWD}:/app" --name eda-web eda-web

```

Open `http://localhost:8080` in your browser.
