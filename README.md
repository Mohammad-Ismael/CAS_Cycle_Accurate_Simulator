
---

# CAS: Cycle-Accurate Simulator

**Current Version:** `v0.1.0`

**License:** MIT

**Target Standard:** IEEE 1800 SystemVerilog (subset)

CAS is a ground-up discrete-event RTL simulator written in C++17. It operates without external compilation frameworks (e.g., LLVM, GCC-transpilation) or third-party Verilog frontends. It reads SystemVerilog source files directly into an in-memory Abstract Syntax Tree (AST), elaborates a dynamic netlist, and resolves combinational logic using a delta-cycle event loop.

---

## What Is Implemented in v0.1.0

Version `v0.1.0` implements the minimum functional pipeline required to parse, elaborate, and simulate basic combinational blocks:

* **Lexical Analyzer (`Lexer`):**
* Strips comments (`//` single-line and `/* ... */` multi-line).
* Tokenizes core SystemVerilog keywords: `module`, `endmodule`, `input`, `output`, `logic`, `always_comb`, `initial`, `begin`, `end`, `$display`, `$finish`.
* Parses sized and unsized numeric literals with radix markers (`'d`, `'b`, `'h`).


* **Parser (`Parser`):**
* Recursive-descent grammar parsing into typed AST nodes.
* Port declarations with bit-width bounds (`[msb:lsb]`).
* Process containers (`always_comb`, `initial`).
* LHS concatenation targets (e.g., `{cout, sum}`).
* Binary arithmetic expressions (`+`, `-`).


* **Elaborator & Netlist Graph (`Engine`):**
* Allocates signals into a flat dynamic table (`Net`) indexed by integer ID.
* Bit-width clamping and dynamic bit-slice unpacking for concatenated targets.


* **Kernel & Delta Cycles:**
* Combinational stabilization loop: iteratively executes active processes until net transitions cease across the entire design.


* **System Tasks:**
* `$display` with format specifiers (`%d`, `%b`, `%h`).
* `$finish` for controlled simulation exit.



---

## Current Limitations in v0.1.0

The following constructs are **not yet implemented** in `v0.1.0` and will produce parser or elaboration errors:

1. **Hierarchy / Module Instantiation:** Designs must be flat or self-contained. Submodule instantiation (e.g., `adder u_dut (...)`) is not yet wired to the parent netlist.
2. **Timing & Delays:** No time wheel exists yet. Delays (`#10`) are not recognized.
3. **Sequential Logic:** Non-blocking assignments (`<=`) and edge-triggered processes (`always_ff @(posedge clk)`) are not yet scheduled.
4. **4-State Logic:** Values are stored as unsigned 64-bit integers (`0` and `1`). Unknown (`X`) and High-Impedance (`Z`) states are not modeled.

---

## Verified Golden Test (v0.1.0)

### RTL (`design.sv`)

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

### Test (`testbench.sv`)

```systemverilog
module tb_adder;

    logic [3:0] a;
    logic [3:0] b;
    logic [3:0] sum;
    logic       cout;

    initial begin
        a = 4'd3;
        b = 4'd5;
        {cout, sum} = a + b;
        $display("Test 1 -> a: %d, b: %d, sum: %d, cout: %d", a, b, sum, cout);

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

## Repository Structure

```text
CAS/
├── include/
│   ├── ast.hpp          # AST node structs
│   ├── lexer.hpp        # Token definitions and lexer interface
│   ├── parser.hpp       # Recursive-descent parser declarations
│   ├── netlist.hpp      # Net class and bit-manipulation primitives
│   └── engine.hpp       # Delta-cycle execution engine
├── src/
│   ├── lexer.cpp
│   ├── parser.cpp
│   ├── engine.cpp
│   └── main.cpp         # CLI driver
├── CMakeLists.txt
├── Dockerfile           # Alpine-based build/runtime container
├── server.py            # Local FastAPI backend for UI integration
└── static/
    └── index.html       # Browser UI (Monaco editor + interactive terminal)

```

---

## Build Instructions

### Native Build (Linux / macOS)

Requires CMake 3.16+ and a C++17 compiler (`g++` or `clang++`):

```bash
cd CAS
cmake -B build
cmake --build build

```

The resulting binary will be located at `./build/cas`.

### Docker Build

```bash
docker run --rm -v ${PWD}:/app -w /app/CAS alpine:3.20 sh -c \
    "apk add --no-cache build-base cmake && cmake -B build && cmake --build build"

```

### Running the Simulator

Pass the SystemVerilog files as positional CLI arguments:

```bash
./CAS/build/cas path/to/design.sv path/to/testbench.sv

```

---

## Roadmap

| Milestone | Target Feature Set |
| --- | --- |
| **`v0.2.0`** | **Module Instantiation & Scoping:** Parsing `adder u_dut (.a(a), .b(b))`, resolving hierarchical net names (`tb.u_dut.a`), and continuous driver-receiver fanout propagation across boundaries. |
| **`v0.3.0`** | **Time Wheel & Delays:** Priority-queue discrete-event scheduler supporting `#delay` execution, cooperative process yielding, and `$time`. |
| **`v0.4.0`** | **Sequential Logic:** IEEE 1800 Active vs. NBA event regions, `always_ff @(posedge clk)`, and non-blocking assignments (`<=`). |
| **`v0.5.0`** | **4-State Logic Engine:** Dual-rail integer bit-vectors (`value` + `unknown/mask`) to correctly propagate `X` and `Z` states. |
| **`v0.6.0`** | **Waveform Dumper:** VCD (Value Change Dump) file writer for signal analysis in GTKWave / Surfer. |
| **`v1.0.0`** | **Performance & Benchmarking:** Cycle-accurate execution of open-source RISC-V cores with automated result and throughput comparison against Synopsys VCS and Cadence Xcelium. |