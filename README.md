# CAS: Cycle-Accurate Simulator

**Current Version:** `v0.2.0`  
**License:** MIT  
**Target Standard:** IEEE 1800 SystemVerilog (subset)

CAS is an independent, ground-up discrete-event RTL simulator implemented in C++17. It bypasses external compilation chains (e.g., LLVM, intermediate C++ transpilation) and third-party frontends. CAS parses SystemVerilog sources directly into an in-memory Abstract Syntax Tree (AST), elaborates a hierarchical netlist graph, and resolves logic interactions via an iterative delta-cycle convergence engine.

---

## Architectural Highlights

* **Pure Native Execution:** Compiles and runs directly from source files through a custom lexer, recursive-descent parser, and dynamic runtime engine.
* **Hierarchical Scoping:** Supports module definitions, port directionality (`input`, `output`), and named submodule instantiations.
* **Continuous Interconnect Engine:** Binds parent-child net boundaries dynamically and resolves combinational fanouts across instances.
* **Dynamic Bit Slicing:** Handles arbitrary bit-width vectors and dynamic unpacking of LHS concatenation targets (`{cout, sum} = a + b`).
* **Interactive Web Playground:** Shipped with a Dockerized IDE featuring dual Monaco editors (RTL and Testbench), resizable splitters, and a FastAPI-driven execution backend.

---

## Verified Reference Design (`v0.2.0`)

### RTL Design (`design.sv`)
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

### Testbench (`testbench.sv`)

```systemverilog
module tb_adder;

    logic [3:0] a;
    logic [3:0] b;
    logic [3:0] sum;
    logic       cout;

    adder u_dut (
        .a(a),
        .b(b),
        .sum(sum),
        .cout(cout)
    );

    initial begin
        a = 4'd4;
        b = 4'd5;
        $display("Input drive -> a: %d, b: %d | DUT Output -> sum: %d, cout: %d", a, b, sum, cout);

        a = 4'd14;
        b = 4'd3;
        $display("Input drive -> a: %d, b: %d | DUT Output -> sum: %d, cout: %d", a, b, sum, cout);

        $finish;
    end

endmodule

```

### Simulation Output

```text
CAS (Cycle Accurate Simulator) v0.2.0 Initiated
Hierarchical Nets Elaborated: 8
Port Interconnects Bound: 4
--------------------------------------------------------
[CAS $display] Input drive -> a: 4, b: 5 | DUT Output -> sum: 9, cout: 0
[CAS $display] Input drive -> a: 14, b: 3 | DUT Output -> sum: 1, cout: 1
[CAS $finish] Simulation Terminated Gracefully.

```

---

## Project Structure

```text
CAS/
├── include/
│   ├── ast.hpp          # Typed AST representation
│   ├── lexer.hpp        # Lexer interface and Token declarations
│   ├── parser.hpp       # Recursive-descent grammar parser
│   ├── netlist.hpp      # Runtime Net descriptors and dynamic bit-masks
│   └── engine.hpp       # Elaboration, delta convergence, and execution kernel
├── src/
│   ├── lexer.cpp
│   ├── parser.cpp
│   ├── engine.cpp
│   └── main.cpp         # CLI driver
├── CMakeLists.txt       # Build system definition
├── Dockerfile           # Build and runtime environment
├── server.py            # Web IDE backend API
└── static/
    └── index.html       # Web workspace interface

```

---

## Build and Execution

### Build Native Binary via Docker

```bash
docker run --rm -v ${PWD}:/app -w /app/CAS alpine:3.20 sh -c \
    "apk add --no-cache build-base cmake && cmake -B build && cmake --build build"

```

### Run CLI Simulation

```bash
./CAS/build/cas path/to/testbench.sv path/to/design.sv

```

### Launch Web Environment

```bash
docker run --rm -p 8080:8080 -v ${PWD}:/app eda-web

```

Open `http://localhost:8080` in your browser.

---

## Roadmap

Detailed release history is tracked in [`VERSIONS.md`](https://github.com/Mohammad-Ismael/CAS_Cycle_Accurate_Simulator/blob/main/VERSIONS.md).

| Target Version | Milestone Feature Focus | Status |
| --- | --- | --- |
| **`v0.1.0`** | Standalone AST, dynamic netlist, flat delta-cycle loop, `$display`, `$finish`. | **Released** |
| **`v0.2.0`** | Hierarchical module instantiation, named port mapping (`.port(wire)`), cross-boundary net propagation. | **Released** |
| **`v0.3.0`** | **Time Wheel & Delays:** Priority-queue event scheduler, `#delay` statements, and `$time`. | *Next* |
| **`v0.4.0`** | **Sequential Logic:** Active vs. NBA event regions, `always_ff @(posedge clk)`, and non-blocking assignments (`<=`). | *Planned* |
| **`v0.5.0`** | **4-State Logic Engine:** Dual-rail bit vectors (`value` + `unknown/mask`) to model uninitialized nets and `X`/`Z` states. | *Planned* |
| **`v0.6.0`** | **Waveform Tracing:** Native VCD (Value Change Dump) file generation for GTKWave / Surfer. | *Planned* |
| **`v1.0.0`** | **Benchmarking:** Cycle-accurate execution of open-source RISC-V cores with automated throughput comparisons against Synopsys VCS and Cadence Xcelium. | *Planned* |