# CAS: Cycle-Accurate Simulator

**Current Version:** `v0.4.0`  
**License:** MIT  
**Target Standard:** IEEE 1800 SystemVerilog (subset)

CAS is an independent, ground-up discrete-event RTL simulator implemented in C++17. It bypasses external compilation chains (e.g., LLVM, intermediate C++ transpilation) and third-party frontends. CAS parses SystemVerilog sources directly into an in-memory Abstract Syntax Tree (AST), elaborates a hierarchical netlist graph, and executes simulation events using an IEEE 1800-inspired stratified scheduler (Active, NBA, Combinational Convergence, and Time Wheel).

---

## Architectural Highlights

* **Stratified IEEE 1800 Scheduling (Active vs. NBA):** Evaluates non-blocking assignments (`<=`) during the active region and buffers values into a dedicated NBA update queue, committing all register transitions simultaneously before triggering combinational delta convergence.
* **Edge-Triggered Sensitivity Engine:** Dynamic transition tracking (`is_posedge()`, `is_negedge()`) across hierarchical signal handles for `always_ff` blocks with asynchronous resets.
* **Native Discrete-Event Time Wheel:** Min-heap priority queue (`std::priority_queue`) that advances simulation time dynamically across `#delay` intervals.
* **Cooperative Coroutine Scheduling:** Procedural processes (`initial` blocks) maintain an internal program counter (`PC`), cleanly yielding on `#delay` and resuming when scheduled by the time wheel.
* **Hierarchical Scoping & Port Interconnects:** Resolves top-level root modules and submodule instantiations (`.port(net)`), continuously propagating driver and receiver nets across hierarchy boundaries.
* **Immediate Combinational Convergence:** Procedural blocking assignments (`=`) trigger immediate delta-cycle propagation passes (`propagate_comb()`) before subsequent evaluation points.
* **Dockerized Web Playground:** Integrated Monaco editor IDE featuring resizable RTL/Testbench panes and a FastAPI backend streaming simulation logs to a live terminal.

---

## Verified Reference Design (`v0.4.0`)

### RTL Design (`design.sv`)
```systemverilog
module counter (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [3:0] d,
    output logic [3:0] q
);

always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        q <= 0;
    end else begin
        q <= q + d;
    end
end

endmodule

```

### Testbench (`testbench.sv`)

```systemverilog
module tb_counter;

    logic       clk;
    logic       rst_n;
    logic [3:0] d;
    logic [3:0] q;

    counter u_dut (
        .clk(clk),
        .rst_n(rst_n),
        .d(d),
        .q(q)
    );

    initial begin
        clk = 0;
        rst_n = 0;
        d = 4'd3;

        #5;
        $display("[Time=%0t] Reset active -> q=%d", $time, q);

        // Deassert reset
        rst_n = 1;
        #5;

        // Clock Pulse 1 (posedge at Time=15)
        clk = 1;
        #5;
        $display("[Time=%0t] Clock Edge 1 -> q=%d", $time, q);
        clk = 0;
        #5;

        // Clock Pulse 2 (posedge at Time=25)
        d = 4'd5;
        clk = 1;
        #5;
        $display("[Time=%0t] Clock Edge 2 -> q=%d", $time, q);
        clk = 0;
        #5;

        // Clock Pulse 3 (posedge at Time=35)
        clk = 1;
        #5;
        $display("[Time=%0t] Clock Edge 3 -> q=%d", $time, q);
        clk = 0;
        #5;

        $finish;
    end

endmodule

```

### Simulation Output

```text
CAS (Cycle Accurate Simulator) v0.4.0 Initiated
Hierarchical Nets Elaborated: 8
Port Interconnects Bound: 4
--------------------------------------------------------
[CAS $display] [Time=5] Reset active -> q=0
[CAS $display] [Time=15] Clock Edge 1 -> q=3
[CAS $display] [Time=25] Clock Edge 2 -> q=8
[CAS $display] [Time=35] Clock Edge 3 -> q=13
[CAS $finish] Simulation Terminated at time 40.
--------------------------------------------------------
Simulation finished at sim_time = 40.

```

---

## Project Structure

```text
CAS/
├── include/
│   ├── ast.hpp          # Typed AST definitions (Module, Ports, Processes, Exprs)
│   ├── lexer.hpp        # Lexer interface and Token declarations
│   ├── parser.hpp       # Recursive-descent grammar parser
│   ├── netlist.hpp      # Runtime Net descriptors, transition flags, and masks
│   └── engine.hpp       # Time wheel, NBA queues, and execution kernel
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
| **`v0.3.0`** | Priority-queue event scheduler, `#delay` statements, cooperative process execution, and `$time`. | **Released** |
| **`v0.4.0`** | **Sequential Logic:** IEEE 1800 Active vs. NBA event regions, `always_ff @(posedge/negedge)`, non-blocking assignments (`<=`), and `if-else` branching. | **Released** |
| **`v0.5.0`** | **Waveform Tracing (VCD):** Value Change Dump generator to export `.vcd` traces for visualization in GTKWave and Surfer. | *Next* |
| **`v0.6.0`** | **4-State Logic Engine:** Dual-rail bit vectors (`value` + `unknown/mask`) to model uninitialized nets and `X`/`Z` states. | *Planned* |
| **`v1.0.0`** | **Benchmarking:** Cycle-accurate execution of open-source RISC-V cores with automated throughput comparisons against Synopsys VCS and Cadence Xcelium. | *Planned* |