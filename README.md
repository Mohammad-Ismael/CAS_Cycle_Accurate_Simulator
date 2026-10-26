# CAS: Cycle-Accurate Simulator

**Current Version:** `v0.6.0`  
**License:** MIT  
**Target Standard:** IEEE 1800 SystemVerilog (subset)

CAS is an independent, ground-up discrete-event RTL simulator implemented in C++17. It operates without external compiler chains (LLVM, intermediate C++ transpilation) or third-party frontends. CAS parses SystemVerilog sources directly into an in-memory Abstract Syntax Tree (AST), elaborates a hierarchical netlist graph, and executes simulation events using an IEEE 1800-inspired stratified scheduler (Active, NBA, Combinational Convergence, and Time Wheel).

---

## Architectural Highlights

* **Native 4-State Logic Engine (`0, 1, X, Z`):** Dual-rail bit-vector arithmetic (`aval` data, `bval` unknown mask). All un-driven nets and uninitialized registers initialize to `X` (unknown) rather than optimistic `0`, catching un-reset design bugs.
* **4-State Arithmetic Poisoning & Boundary Transitions:** Arithmetic operations automatically propagate unknown states across operations, while edge sensitivity handles transitions across unknown levels (`X -> 0` for `negedge`, `X -> 1` for `posedge`).
* **Project-Centric Workspace Engine:** Isolated directory hierarchies (`projects/<name>/`) storing RTL designs (`design.sv`), testbenches (`testbench.sv`), metadata (`project.json`), and archived waveform traces (`waves/`).
* **Persistent Waveform History:** Timestamped VCD trace runs stored per project with in-browser selection, deletion, and immediate canvas invalidation when dump directives are omitted.
* **4-State Digital Timing Canvas:** Native HTML5 Canvas timing diagram renderer displaying clocks, single-bit logic wires, and multibit transition buses with distinct visual styles for known values vs. `X`/`Z` states.
* **Native VCD Waveform Generation:** Direct export of IEEE 1364/1800 Value Change Dump (`.vcd`) traces supporting `$dumpfile` and `$dumpvars` with hierarchical scope definitions and compact ASCII symbol mapping.
* **Stratified IEEE 1800 Scheduling (Active vs. NBA):** Evaluates non-blocking assignments (`<=`) during the active region and buffers values into an NBA update queue, committing all register transitions simultaneously before triggering combinational delta passes.
* **Edge-Triggered Sensitivity Engine:** Dynamic transition tracking (`is_posedge()`, `is_negedge()`) across hierarchical signal handles for `always_ff` blocks with asynchronous resets.
* **Native Discrete-Event Time Wheel:** Min-heap priority queue (`std::priority_queue`) that advances simulation time dynamically across `#delay` intervals.

---

## Verified Reference Design (`v0.6.0`)

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
        $dumpfile("counter_4state.vcd");
        $dumpvars;

        // T=0: Before reset, q is X (unknown)
        $display("[Time=%0t] Initial state -> q=%b", $time, q);

        #5;
        // T=5: Assert active-low reset
        clk = 0;
        rst_n = 0;
        d = 4'd2;
        #5;
        $display("[Time=%0t] After Reset -> q=%b", $time, q);

        // T=15: Deassert reset and pulse clock
        rst_n = 1;
        #5;
        clk = 1;
        #5;
        $display("[Time=%0t] Clock Edge 1 -> q=%d", $time, q);
        clk = 0;
        #5;

        // T=30: Next pulse
        clk = 1;
        #5;
        $display("[Time=%0t] Clock Edge 2 -> q=%d", $time, q);
        clk = 0;
        #5;

        $finish;
    end

endmodule

```

### Simulation Output

```text
CAS (Cycle Accurate Simulator) v0.6.0 [4-State Kernel] Initiated
Hierarchical Nets Elaborated: 8
Port Interconnects Bound: 4
--------------------------------------------------------
[CAS $display] [Time=0] Initial state -> q=x
[CAS $display] [Time=10] After Reset -> q=0000
[CAS $display] [Time=20] Clock Edge 1 -> q=2
[CAS $display] [Time=30] Clock Edge 2 -> q=4
[CAS $finish] Simulation Terminated at time 35.
[CAS VCD] 4-State trace successfully dumped to: counter_4state.vcd
--------------------------------------------------------
Simulation finished at sim_time = 35.

```

---

## Project Structure

```text
CAS/
├── include/
│   ├── ast.hpp          # AST node structs, system tasks, and edge types
│   ├── lexer.hpp        # Lexer declarations and token types
│   ├── parser.hpp       # Recursive-descent grammar parser
│   ├── netlist.hpp      # Dual-rail 4-state LogicVal, Net descriptors, transition flags
│   └── engine.hpp       # Time wheel, NBA queues, 4-state evaluation, VCD dumper
├── src/
│   ├── lexer.cpp
│   ├── parser.cpp
│   ├── engine.cpp
│   └── main.cpp         # CLI driver
├── CMakeLists.txt       # Build system definition
├── Dockerfile           # Build and execution container
├── server.py            # FastAPI backend (Projects API & execution harness)
├── static/
│   └── index.html       # Studio IDE (Monaco editors + 4-State Canvas waveform engine)
└── projects/            # Persistent local user projects (host volume)

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
| **`v0.4.0`** | Sequential logic, IEEE 1800 Active vs. NBA event regions, `always_ff @(posedge/negedge)`, non-blocking assignments (`<=`). | **Released** |
| **`v0.5.0`** | Native VCD generation (`$dumpfile`, `$dumpvars`), studio IDE dark theme, and HTML5 canvas waveform engine. | **Released** |
| **`v0.5.1`** | Project workspace architecture (`projects/`), persistent trace history stack, and strict canvas state invalidation. | **Released** |
| **`v0.6.0`** | **4-State Logic Engine (`0, 1, X, Z`):** Dual-rail bit vectors (`aval` + `bval`), unknown poisoning, 4-state edge transitions, and visual `X` waveform indicators. | **Released** |
| **`v0.7.0`** | **Bitwise & Logical Operators:** Operator coverage (`&`, ` | `, `^`, `~`, `>>`, `<<`, `==`, `!=`, ternary `?:`). |
| **`v1.0.0`** | **Benchmarking:** Cycle-accurate execution of open-source RISC-V cores with automated throughput comparisons against commercial simulators. | *Planned* |
