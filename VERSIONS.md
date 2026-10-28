# CAS Simulator Release Notes & Roadmap

## Version History

### `v0.9.3` - Gate Primitives, Positional Port Slices & Continuous Monitoring
* **Built-in Gate Primitives:** Evaluates IEEE 1800 primitives (`and`, `or`, `xor`, `nand`, `nor`, `xnor`, `not`, `buf`) with multi-input fan-in directly within the combinational delta pass.
* **Positional Port Slice Binding:** Submodule instantiation supports positional arguments mapped directly to bus slices (e.g., `full_adder fa0 (sum[0], c[0], a[0], b[0], cin);`).
* **Dynamic Sensitivity Monitoring (`$monitor`):** Automatic callback mechanism tracking sensitivity variable updates across discrete-event steps with formatted console output.
* **Non-ANSI Header Support:** Lexer and parser support for legacy Verilog-1995/2001 port declarations (`module m (a, b); input a; output b;`).
* **Subroutine Task Execution:** User-defined procedural `task` blocks with variable parameters and prefix delays (`#delay`).

---

### `v0.9.0` - Dataflow Modeling & Unpacked Memory Arrays
* **Continuous `assign` Dataflow:** Module-level continuous assignments evaluated concurrently in the discrete-event delta-cycle loop.
* **1D Memory Arrays:** Native syntax support for `logic [W-1:0] mem [0:D-1];` arrays, including asynchronous indexed reads (`mem[addr]`) and synchronous non-blocking writes (`mem[addr] <= data`).
* **Memory Array Unknown Power-On State:** All unwritten memory words initialize to 4-state unknown (`X`) vectors.

---

### `v0.8.0` - Control Flow Selection & Vector Part-Select Slicing
* **Control Selection (`case` / `endcase`):** Multi-way branch selection supporting exact value matching, default fallbacks, and multi-statement procedural blocks.
* **Bit-Select & Part-Select Extraction:** Vector scalar bit selection (`signal[i]`) and variable part-select slicing (`signal[msb:lsb]`) with 4-state logic alignment.

---

### `v0.7.0` - Extended ALU Operators & Recursive Hierarchical Elaboration
* **Comprehensive Operator Set:** Added logical shifts (`<<`, `>>`), relational gates (`<`, `<=`, `>`, `>=`), equality gates (`==`, `!=`), bitwise logic (`&`, `|`, `^`, `~`), and ternary conditional evaluation (`cond ? a : b`).
* **Arbitrary-Depth Recursive Elaboration:** Recursive AST traversal resolving nested submodule instantiations (`top -> dut -> submod -> ...`) and binding cross-boundary net slices.

---

### `v0.6.0` - Dual-Rail 4-State Logic Engine
* **Dual-Word Representation (`0, 1, X, Z`):** Implemented dual 64-bit integer masks (`aval` data word, `bval` unknown mask) across all nets and variables.
* **IEEE 1800 Unknown Propagation:** Bitwise logic gates and arithmetic operations correctly handle unknown inputs (`0 & X = 0`, `1 | X = 1`, `X ^ X = X`).
* **Asynchronous Reset Fall-Through:** Detects 4-state unknown transitions to definite states (`X -> 0` safely activates `negedge`).

---

### `v0.5.1` - Workspace Engine & VCD Timeline Synchronization
* **Project Directory Workspaces:** Directory isolation (`projects/<name>/`) storing RTL designs, testbenches, waveforms, and configuration metadata.
* **Persistent Waveform Archive:** Multi-run VCD history tracking per workspace with deletion and selection controls.

---

### `v0.5.0` - Native VCD Waveform Generation & Digital Canvas
* **Value Change Dump Generator:** High-throughput binary-to-ASCII dumper generating IEEE 1364/1800 `.vcd` files via `$dumpfile` and `$dumpvars`.
* **HTML5 Waveform Viewer:** Digital timing canvas visualizing clocks, single-bit logic transitions, and multi-bit hexadecimal buses.

---

### `v0.4.0` - Stratified IEEE 1800 Event Regions
* **Active vs. NBA Event Scheduling:** Two-phase execution buffering non-blocking assignments (`<=`) and committing register updates synchronously before combinational delta passes.
* **Edge Sensitivity Engine:** Dynamic edge tracking (`is_posedge()`, `is_negedge()`) for sequential processes (`always_ff` / `always @(posedge clk)`).

---

### `v0.3.0` - Discrete-Event Time Wheel
* **Min-Heap Event Scheduler:** Priority-queue discrete-event engine dispatching timed events across `#delay` intervals.
* **System Call `$time`:** Native simulation timestamp queries.

---

### `v0.2.0` - Hierarchical Module Instantiation
* Named port binding (`.port(wire)`) and submodule instantiation.

---

### `v0.1.0` - Core Discrete-Event Kernel
* Standalone AST, delta-cycle convergence loop, primitive netlist representation, and foundational system tasks (`$display`, `$finish`).

---

## Roadmap

| Version | Focus Feature Set | Status |
| :--- | :--- | :--- |
| **`v0.1.0`** | AST, netlist, flat delta-cycle loop, `$display`, `$finish` | **Released** |
| **`v0.2.0`** | Hierarchical module instantiation, named port mapping | **Released** |
| **`v0.3.0`** | Priority-queue event scheduler, `#delay`, `$time` | **Released** |
| **`v0.4.0`** | Sequential logic, IEEE 1800 Active vs. NBA, `always_ff @(posedge/negedge)` | **Released** |
| **`v0.5.0`** | Native VCD generation (`$dumpfile`, `$dumpvars`), canvas waveform engine | **Released** |
| **`v0.6.0`** | 4-State Logic Engine (`0, 1, X, Z`), dual-rail bit vectors | **Released** |
| **`v0.7.0`** | Extended ALU operators, recursive elaboration, regression suite | **Released** |
| **`v0.8.0`** | Control flow (`case`), vector slicing (`wire[idx]`, `wire[msb:lsb]`) | **Released** |
| **`v0.9.0`** | Continuous `assign`, 1D unpacked memory arrays | **Released** |
| **`v0.9.3`** | Gate primitives (`and`, `or`...), positional port slices, `$monitor` | **Released** |
| **`v0.9.4`** | Parameterized module instantiations, localparam overrides | **Active** |
| **`v1.0.0`** | RISC-V Verification: Cycle-accurate RV32I execution | *Next* |