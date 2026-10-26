
# Release History & Version Log

All notable architectural enhancements, language grammar additions, and runtime changes for CAS are documented here in reverse chronological order.

---

## [v0.9.0] - 2026-10-26

### Added
* **Continuous `assign` Statements:**
  * Added `TOKEN_ASSIGN` (`assign`) keyword to lexer.
  * Added `ContinuousAssignNode` to the AST and module elaboration pass.
  * Evaluated within `Engine::propagate_comb()`, driving signals continuously alongside port interconnects and combinational blocks.
* **1D Unpacked Memory Arrays:**
  * Grammar support for unpacked dimension declarations: `logic [width-1:0] name [0:depth-1];`.
  * Implemented `MemoryArray` structure in `Engine` with per-word dual-rail 4-state storage.
  * Dynamic array reads via `IndexSelectNode` returning `X` for unknown/out-of-bounds addresses.
  * Added `MemAssignNode` supporting blocking (`mem[addr] = data`) and non-blocking (`mem[addr] <= data`) procedural writes.
  * Implemented `NBAMemUpdate` queue to commit memory writes synchronously in the NBA scheduling region.
* **Regression Suite Milestone Update:**
  * Extended testbench to 35 hierarchical nets, 19 port bindings, 2 continuous assigns, and 1 memory array.
  * Verified continuous assign dataflow, synchronous RAM writing, and asynchronous RAM reading.

---

## [v0.8.0] - 2026-10-26

### Added
* **Case Statement Branch Selection:**
  * Added tokens: `TOKEN_CASE` (`case`), `TOKEN_ENDCASE` (`endcase`), and `TOKEN_DEFAULT` (`default`).
  * Implemented `CaseStatementNode` and `CaseItem` supporting sequential branch testing and fallback defaults.
  * Supported single statements and multi-statement `begin ... end` compound blocks inside case branches.
* **Vector Bit-Select & Part-Select Indexing:**
  * Added `IndexSelectNode` to AST.
  * Parser handles bracketed indices during primary expression parsing (`ident[idx]` and `ident[msb:lsb]`).
  * `Engine::eval_expr()` extracts scalar bits and sub-slices with exact dual-rail `aval` / `bval` alignment, returning `X` on out-of-bounds or unknown indices.
* **Procedural Pipeline Fixes:**
  * Fixed combinational modified-flag reset order in `Engine::propagate_comb()`, preventing missed sequential edge transitions during reset assertion.
  * Updated procedural execution in `step_process()` so blocking updates trigger combinational propagation and sequential edge evaluation immediately.
* **Regression Test Suite Expansion:**
  * Expanded `projects/regression/` to 27 nets and 15 port bindings, verifying `case` branching and bit/part-select slicing with automated pass/fail verification.

---

## [v0.7.0] - 2026-10-26

### Added
* **Recursive Multi-Level Elaboration Engine:**
  * Replaced flat single-level instance walker with recursive `Engine::elaborate_module()` capable of resolving arbitrary-depth module hierarchies (`top -> dut -> submod -> ...`).
  * Hierarchical net mapping correctly handles deeply nested instance scopes and links cross-boundary port driver/receiver bindings.
* **Extended Operator Grammar & Precedence Climbing:**
  * Added tokenization and stratified parsing for bitwise operators (`&`, `|`, `^`, `~`), logical shifts (`<<`, `>>`), relational/equality (`==`, `!=`, `<`, `<=`, `>`, `>=`), logical short-circuit gates (`&&`, `||`), and ternary conditionals (`?:`).
  * Stratified expression precedence climbing eliminates parsing ambiguities in complex ALU operations.
* **Dual-Rail 4-State Bitwise Operations:**
  * Implemented exact per-bit truth tables in `LogicVal` for bitwise AND, OR, XOR, NOT, and shifts.
  * Added 4-state logical equality and relational comparison operators returning `X` on unknown inputs.
* **Procedural Conditionals in Initial Processes:**
  * Updated `Engine::execute_statement()` and `step_process()` to support procedural `if-else` branching inside testbench processes with nested `$display` and `$finish` calls.
* **Comprehensive Automated Regression Suite:**
  * Added `projects/regression/` with automated self-checking assertions covering all language features and architectural milestones from `v0.1.0` through `v0.7.0`.

---

## [v0.6.0] - 2026-10-26

### Added
* **4-State Dual-Rail Logic Engine (`0, 1, X, Z`):**
  * Replaced native unsigned integer state with dual-rail `LogicVal` (`aval` for data, `bval` for unknown/Z control mask).
  * All signals and registers now initialize to `X` by default upon elaboration, matching standard IEEE 1800 simulator behavior.
  * Implemented 4-state arithmetic poisoning: arithmetic operations on vectors with any unknown bits propagate `X` to all result bits.
  * Enhanced edge detection (`is_posedge()`, `is_negedge()`) to correctly detect transitions to and from unknown/uninitialized states (`X -> 0` triggers `negedge`, `X -> 1` triggers `posedge`).
* **Waveform & Terminal 4-State Rendering:**
  * Updated VCD dumper to format single-bit unknown scalars (`x<id>`) and multi-bit vectors containing unknown bits (`b...x... <id>`).
  * Updated Canvas timing diagram renderer to display unknown single-bit wires as mid-rail red traces and unknown buses as red-shaded `X` intervals.
  * Fixed terminal selection CSS (`user-select: text`) allowing developers to highlight and copy console simulation logs directly.

---

## [v0.5.1] - 2026-10-26

### Added
* **Project Management Architecture:**
  * Added persistent project storage under `/app/projects/<name>/` preserving `design.sv`, `testbench.sv`, `project.json`, and historical `waves/`.
  * Added REST endpoints in `server.py`: `GET/POST /api/projects`, `GET/DELETE /api/projects/{name}`, and waveform fetching/deletion routes.
  * Added Project Explorer drawer modal to create, open, and delete projects.
* **Persistent Waveform History & State Invalidation:**
  * Simulations inside an active project automatically archive timestamped `.vcd` files into that project's `waves/` directory.
  * Added trace selector dropdown in the Waveform Viewer tab allowing instant playback of historical runs.
  * Implemented strict waveform canvas clearing: re-running without `$dumpfile` or `$dumpvars` immediately invalidates previous canvas renders and disables export.

---

## [v0.5.0] - 2026-10-26

### Added
* **Native VCD (Value Change Dump) Generation:**
  * Added `$dumpfile("...")` and `$dumpvars` language support to `Lexer`, `Parser`, and `AST`.
  * Implemented IEEE 1364/1800 compliant VCD header generation in `Engine`: `$date`, `$version`, `$timescale 1ns`, and hierarchical `$scope ... $var ... $upscope`.
  * Added compact ASCII identifier generation mapping unique runtime net IDs to printable ASCII tokens (`!` to `~`).
  * Real-time signal change dumper tracking delta transitions across simulation time steps (`#0`, `#5`, `#15`, etc.).
* **Studio UI Overhaul & HTML5 Canvas Waveform Viewer:**
  * Replaced static terminal with a multi-pane Studio workspace featuring draggable horizontal and vertical splitters.
  * Added an in-browser digital timing diagram engine rendering clocks, single-bit logic wires, and multibit transition buses with hexadecimal tags.
  * Added one-click `.vcd` file export directly from the browser navigation bar.
  * Integrated raw VCD text source inspection tab.

---

## [v0.4.0] - 2026-10-26

### Added
* **Stratified IEEE 1800 Scheduler (Active & NBA Regions):**
  * Implemented an NBA buffer queue (`std::vector<NBAUpdate>`) in `Engine`.
  * Non-blocking assignments (`<=`) evaluate expressions during the active region without altering target nets immediately.
  * `commit_nba_updates()` synchronously commits all pending non-blocking updates and triggers combinational delta passes.
* **Sequential Logic & Edge Detection:**
  * Added `always_ff` process handling with sensitivity lists supporting `posedge` and `negedge` transitions.
  * Enhanced `Net` struct with `prev_value` history and transition queries (`is_posedge()`, `is_negedge()`).
* **Control Flow & Operator Grammar:**
  * Added `if (...) begin ... end else begin ... end` grammar constructs and `IfStatementNode`.
  * Added logical NOT (`!`) operator support via `UnaryOpNode`.
  * Added non-blocking assignment operator (`<=` / `TOKEN_LE`) support in `Lexer` and `Parser`.

---

## [v0.3.0] - 2026-10-26

### Added
* **Discrete-Event Time Wheel:**
  * Implemented an ordered min-heap priority queue (`std::priority_queue<SimEvent>`) in `Engine`.
  * Simulation time (`sim_time`) advances dynamically to scheduled timestamps rather than executing exclusively at $T=0$.
* **Cooperative Process Scheduling:**
  * Procedural processes (`initial` blocks) track execution progress using an internal Program Counter (`size_t pc`).
  * Yields execution upon reaching `#delay`, inserting a wake-up event into the time wheel at `sim_time + delay`.
* **Delay Syntax & Language Extensions:**
  * Added tokenization for the `#` operator (`TOKEN_HASH`) in `Lexer`.
  * Implemented `DelayStatementNode` in `include/ast.hpp` and integrated parsing into `Parser::parse_statement()`.
* **System Task `$time`:**
  * Added `$time` token and expression evaluation resolving directly to current simulation time.
  * Supported `%0t` and `%t` format specifiers in `$display`.

---

## [v0.2.0] - 2026-10-26

### Added
* **Hierarchical Module Instantiation:**
  * Added grammar support for named port connections: `module_type instance_name (.port(wire), ...);`.
  * Added `InstantiationNode` and `PortConnection` AST structures to `include/ast.hpp`.
  * Implemented dot-tokenization (`TOKEN_DOT` / `.`) in `Lexer`.
* **Hierarchical Netlist Scoping:**
  * Elaborator automatically determines the design's top-level root module.
  * Signals are scoped systematically (e.g., `tb_adder.a`, `tb_adder.u_dut.a`).
  * Continuous port-binding table (`NetBinding`) links driving parent nets to receiving child input ports, and driving child output ports back to parent nets.
* **Immediate Combinational Delta Propagation:**
  * Implemented `propagate_comb()` in the evaluation kernel to guarantee that procedural blocking assignments (`=`) immediately trigger iterative combinational convergence across module boundaries before sequential calls (such as `$display`) execute.

---

## [v0.1.0] - 2026-10-26

### Added
* **Frontend Tokenizer & AST Parser:**
  * Pure C++ recursive-descent parser without third-party compiler dependencies.
  * Keyword support: `module`, `endmodule`, `input`, `output`, `logic`, `always_comb`, `initial`, `begin`, `end`, `$display`, `$finish`.
  * Radix literal parsing for sized and unsized numbers (`'d`, `'b`, `'h`).
  * Support for single-line (`//`) and multi-line (`/* */`) comments.
* **Core Netlist & Slicing:**
  * Dynamic integer-indexed signal table (`Net`) with runtime width masking.
  * LHS dynamic multi-signal concatenation unpacking (e.g., `{cout, sum} = a + b`).
* **Simulation Loop:**
  * Iterative combinational delta-cycle convergence loop.
  * Controlled termination via `$finish`.
  * `$display` string decoding supporting `%d`, `%b`, and `%h` format specifiers.
* **Dockerized Web Playground:**
  * Monaco editor frontend with split testbench/RTL panes and dynamic resizers.
  * FastAPI server executing native simulation runs inside an isolated container.

```

---

### Git Commit & Push Commands

Run these commands from your repository root (`I:\CAS_Cycle_Accurate_Simulator`):

```bash
git add CAS/include/ast.hpp CAS/include/lexer.hpp CAS/include/engine.hpp CAS/src/lexer.cpp CAS/src/parser.cpp CAS/src/engine.cpp projects/regression/ README.md VERSIONS.md
git commit -m "feat(kernel): release v0.9.0 - continuous assign, 1D memory arrays, and regression suite pass"
git tag -a v0.9.0 -m "CAS v0.9.0: Continuous Assign & Unpacked Memory Arrays"
git push origin main --tags