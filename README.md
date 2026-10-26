# CAS: Cycle-Accurate Simulator

**Current Version:** `v0.3.0`  
**License:** MIT  
**Target Standard:** IEEE 1800 SystemVerilog (subset)

CAS is an independent, ground-up discrete-event RTL simulator implemented in C++17. It bypasses external compilation chains (e.g., LLVM, intermediate C++ transpilation) and third-party frontends. CAS parses SystemVerilog sources directly into an in-memory Abstract Syntax Tree (AST), elaborates a hierarchical netlist graph, and executes simulation events using an IEEE 1800-inspired discrete-event time wheel with combinational delta-cycle convergence.

---

## Architectural Highlights

* **Native Discrete-Event Engine:** Features a min-heap priority queue time wheel (`std::priority_queue`) that coordinates event execution across discrete simulation time steps.
* **Cooperative Coroutine Scheduling:** Procedural processes (`initial` blocks) maintain an execution program counter (`PC`), cleanly yielding on `#delay` and resuming when scheduled by the time wheel.
* **Hierarchical Scoping & Port Interconnects:** Resolves top-level root modules and submodule instantiations (`.port(net)`), continuously propagating driver and receiver nets across hierarchy boundaries.
* **Immediate Combinational Convergence:** Procedural blocking assignments (`=`) trigger immediate delta-cycle propagation passes (`propagate_comb()`) before subsequent evaluation points.
* **Dynamic Bit Slicing:** Arbitrary bit-width vectors and dynamic unpacking of LHS concatenation targets (`{cout, sum} = a + b`).
* **Dockerized Web Playground:** Integrated Monaco editor IDE featuring resizable RTL/Testbench panes and a FastAPI backend streaming simulation logs to a live terminal.

---

## Verified Reference Design (`v0.3.0`)

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
        a = 4'd2;
        b = 4'd3;
        #10;
        $display("[Time=%0t] a=%d, b=%d -> sum=%d, cout=%d", $time, a, b, sum, cout);

        a = 4'd7;
        b = 4'd8;
        #15;
        $display("[Time=%0t] a=%d, b=%d -> sum=%d, cout=%d", $time, a, b, sum, cout);

        a = 4'd15;
        b = 4'd1;
        #20;
        $display("[Time=%0t] a=%d, b=%d -> sum=%d, cout=%d", $time, a, b, sum, cout);

        $finish;
    end

endmodule

```

### Simulation Output

```text
CAS (Cycle Accurate Simulator) v0.3.0 Initiated
Hierarchical Nets Elaborated: 8
Port Interconnects Bound: 4
--------------------------------------------------------
[CAS $display] [Time=10] a=2, b=3 -> sum=5, cout=0
[CAS $display] [Time=25] a=7, b=8 -> sum=15, cout=0
[CAS $display] [Time=45] a=15, b=1 -> sum=0, cout=1
[CAS $finish] Simulation Terminated at time 45.
--------------------------------------------------------
Simulation finished at sim_time = 45.

```

---

## Project Structure

```text
CAS/
├── include/
│   ├── ast.hpp          # Typed AST definitions (Module, Ports, Processes, Exprs)
│   ├── lexer.hpp        # Lexer interface and Token declarations
│   ├── parser.hpp       # Recursive-descent grammar parser
│   ├── netlist.hpp      # Runtime Net descriptors and dynamic bit-masks
│   └── engine.hpp       # Time wheel, event queues, and delta execution kernel
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

Detailed release history is tracked in [`VERSIONS.md`](https://www.google.com/search?q=VERSIONS.md).

| Target Version | Milestone Feature Focus | Status |
| --- | --- | --- |
| **`v0.1.0`** | Standalone AST, dynamic netlist, flat delta-cycle loop, `$display`, `$finish`. | **Released** |
| **`v0.2.0`** | Hierarchical module instantiation, named port mapping (`.port(wire)`), cross-boundary net propagation. | **Released** |
| **`v0.3.0`** | **Time Wheel & Delays:** Priority-queue event scheduler, `#delay` statements, and `$time`. | **Released** |
| **`v0.4.0`** | **Sequential Logic:** IEEE 1800 Active vs. NBA event regions, `always_ff @(posedge clk)`, and non-blocking assignments (`<=`). | *Next* |
| **`v0.5.0`** | **4-State Logic Engine:** Dual-rail bit vectors (`value` + `unknown/mask`) to model uninitialized nets and `X`/`Z` states. | *Planned* |
| **`v0.6.0`** | **Waveform Tracing:** Native VCD (Value Change Dump) file generation for GTKWave / Surfer. | *Planned* |
| **`v1.0.0`** | **Benchmarking:** Cycle-accurate execution of open-source RISC-V cores with automated throughput comparisons against Synopsys VCS and Cadence Xcelium. | *Planned* |

```