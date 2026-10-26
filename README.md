```markdown
# CAS — Cycle Accurate Simulator (v0.1.0-alpha)

CAS (Cycle Accurate Simulator) is a lightweight, ground-up Electronic Design Automation (EDA) simulation engine built in modern C++ (C++17) for SystemVerilog and Verilog RTL designs.

Unlike tools that rely on pre-existing compiler frameworks or third-party wrappers, CAS implements its own lexical analyzer, AST generator, netlist elaborator, and stratified discrete-event evaluation kernel.

---

## Architecture Overview

CAS is organized into a clean, decoupled 4-stage pipeline:

```
[ .sv / .v Source Files ]

│
▼

┌──────────────────────────────────────┐
│       Frontend (Lexer & AST)         │  <-- Tokenizer, Keyword & Literal Parsing
└──────────────────┬───────────────────┘
│
▼
┌──────────────────────────────────────┐
│       Elaboration & Netlist Graph    │  <-- Signal Descriptor Allocation & Width Slicing
└──────────────────┬───────────────────┘
│
▼
┌──────────────────────────────────────┐
│     Discrete-Event Engine (CAS Core) │  <-- Delta-Cycle Convergence & Active Region Execution
└──────────────────┬───────────────────┘
│
▼
┌──────────────────────────────────────┐
│      Runtime System Tasks & I/O      │  <-- $display formatting, $finish control
└──────────────────┬───────────────────┘

```

### Core Subsystems

1. **Lexical Analyzer (`Lexer`):**
   * Tokenizes SystemVerilog keywords, multi-character punctuation, identifiers, format strings, and Verilog numeric literals with explicit bit-widths and radix prefixes (e.g., `4'd3`, `4'b0101`, `8'hFF`).
2. **Recursive-Descent Parser (`Parser`):**
   * Generates a typed Abstract Syntax Tree (AST).
   * Supports `module` port interfaces, bus declarations (`[MSB:LSB]`), `logic` variables, and process containers (`always_comb`, `initial`).
3. **Netlist & Runtime Registry (`Engine` / `Net`):**
   * Allocates signal handles dynamically inside an indexed registry without hardcoded variable templates.
   * Performs dynamic bit-masking and multi-target LHS concatenation slicing (e.g., `{cout, sum} = a + b`).
4. **Simulation Engine:**
   * Executes procedural statements within an iterative delta-cycle loop until combinational equilibrium is reached or `$finish` triggers termination.

---

## Features Implemented in v0.1.0

- [x] **Lexer & Token Stream:** Full handling of keywords, comments (`//`, `/* */`), symbols, and sized/unsized numbers.
- [x] **Procedural Blocks:** `always_comb begin ... end` and `initial begin ... end`.
- [x] **Data Types & Busses:** `logic` declarations with arbitrary bit-width definitions (`[3:0]`).
- [x] **Expressions & Arithmetic:** Binary addition (`+`), subtraction (`-`), and blocking assignments (`=`).
- [x] **Bit Concat Unpacking:** Dynamic LHS concatenation slicing across heterogeneous bit-widths (e.g., `{cout, sum}`).
- [x] **Simulation Control & I/O:**
  - `$display` with `%d`, `%b`, and `%h` specifier decoding.
  - `$finish` graceful termination.
- [x] **Web-based IDE Integration:** Dockerized workspace featuring Monaco editors for Testbench and RTL panes, resizable layout splitters, and a FastAPI-based execution backend.

---

## Verified Golden Reference Test

### RTL Design (`adder.sv`)
```systemverilog
module adder (
    input  logic [3:0] a,
    input  logic [3:0] b,
    output logic [3:0] sum,
    output logic       cout
);

always_comb begin
    {cout, sum} = a + b;
end

endmodule

```

### Testbench (`tb_adder.sv`)

```systemverilog
module tb_adder;

    logic [3:0] a;
    logic [3:0] b;
    logic [3:0] sum;
    logic       cout;

    initial begin
        // Test 1: Simple addition
        a = 4'd3;
        b = 4'd5;
        {cout, sum} = a + b;
        $display("Test 1 -> a: %d, b: %d, sum: %d, cout: %d", a, b, sum, cout);

        // Test 2: Addition with overflow
        a = 4'd12;
        b = 4'd7;
        {cout, sum} = a + b;
        $display("Test 2 -> a: %d, b: %d, sum: %d, cout: %d", a, b, sum, cout);

        $finish;
    end

endmodule

```

### Execution Output

```text
CAS (Cycle Accurate Simulator) v1.0.0 Initiated
Elaborated Netlist Size: 4 nets.
--------------------------------------------------------
[CAS $display] Test 1 -> a: 3, b: 5, sum: 8, cout: 0
[CAS $display] Test 2 -> a: 12, b: 7, sum: 3, cout: 1
[CAS $finish] Simulation Terminated Gracefully.

```

---

## Directory Layout

```text
CAS/
├── include/
│   ├── ast.hpp          # AST node classes (Module, Ports, Processes, Exprs)
│   ├── lexer.hpp        # Lexer declarations and token definitions
│   ├── parser.hpp       # Recursive-descent parser declarations
│   ├── netlist.hpp      # Dynamic net descriptors and bit-slice handlers
│   └── engine.hpp       # Delta-cycle scheduler and execution kernel
├── src/
│   ├── lexer.cpp        # Tokenizer implementation
│   ├── parser.cpp       # Grammar and syntax parser
│   ├── engine.cpp       # Elaboration, expression evaluation, and delta cycles
│   └── main.cpp         # CLI driver
├── CMakeLists.txt       # Build system definition
├── Dockerfile           # Isolated build & execution environment
├── server.py            # Web IDE backend API
└── static/
    └── index.html       # Web workspace interface

```

---

## Build and Run

### 1. Build Native CLI Binary via Docker

```bash
docker run --rm -v ${PWD}:/app -w /app/CAS alpine:3.20 sh -c \
    "apk add --no-cache build-base cmake && cmake -B build && cmake --build build"

```

### 2. Run Headless CLI Simulation

```bash
./CAS/build/cas path/to/design.sv path/to/testbench.sv

```

### 3. Launch Web Playground

```bash
docker run --rm -p 8080:8080 -v ${PWD}:/app eda-web

```

Open `http://localhost:8080` in your browser.

---

## Roadmap: What's Next for CAS

To evolve CAS toward parity with commercial simulators (Synopsys VCS, Cadence Xcelium), development is prioritized as follows:

* [ ] **Hierarchical Module Instantiation & Binding (v0.2.0):**
* Parsing named instance ports: `adder u_dut (.a(a), .b(b), .sum(sum), .cout(cout));`.
* Hierarchical net scopes (`tb.u_dut.a`) and continuous driver-receiver fanout graph bindings.


* [ ] **Temporal Delays & Discrete Event Scheduler:**
* Time wheel integration with min-heap priority queues for `#delay` scheduling.
* Built-in `$time` query support.


* [ ] **Sequential Logic & NBA Region:**
* `always_ff @(posedge clk or negedge rst_n)` process sensitivity lists.
* Stratified IEEE 1800 scheduling separating Active (`=`) and NBA (`<=`) assignment updates.


* [ ] **4-State Logic (`0, 1, X, Z`):**
* Dual-rail bitwise representation to accurately model reset conditions and propagation of uninitialized nets.


* [ ] **Waveform Tracing:**
* Native VCD (Value Change Dump) generator module for debugging with GTKWave or Surfer.


* [ ] **Performance Benchmarking:**
* Execution cycle-matching and runtime throughput comparisons against VCS and Xcelium using open-source RISC-V cores.



---

## License

MIT License. Developed for research and educational purposes.

```

```