# Release History & Version Log

All notable architectural enhancements, language grammar additions, and runtime changes for CAS are documented here in reverse chronological order.

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

### Fixed
* Fixed parser cursor misalignment where invoking the lexer directly inside `parse_statement` swallowed `begin` tokens.
* Fixed zero-value readouts in testbench `$display` by resolving inter-module delta latency.

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