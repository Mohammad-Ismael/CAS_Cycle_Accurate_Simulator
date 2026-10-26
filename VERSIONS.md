
# Release History & Version Log

All notable architectural enhancements, language grammar additions, and runtime changes for CAS are documented here in reverse chronological order.

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