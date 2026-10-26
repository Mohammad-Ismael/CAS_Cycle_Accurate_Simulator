# CAS: Cycle-Accurate Simulator

**Current Version:** `v0.7.0`  
**License:** MIT  
**Target Standard:** IEEE 1800 SystemVerilog (subset)

CAS is an independent, ground-up discrete-event RTL simulator implemented in C++17. It operates without external compiler chains (LLVM, intermediate C++ transpilation) or third-party frontends. CAS parses SystemVerilog sources directly into an in-memory Abstract Syntax Tree (AST), elaborates a hierarchical netlist graph, and executes simulation events using an IEEE 1800-inspired stratified scheduler (Active, NBA, Combinational Convergence, and Time Wheel).

---

## Architectural Highlights

* **Full ALU Operator Suite & Expression Precedence:** Operator precedence climbing supporting bitwise (`&`, `|`, `^`, `~`), logical shifts (`<<`, `>>`), relational/equality (`==`, `!=`, `<`, `<=`, `>`, `>=`), and ternary conditional evaluation (`cond ? true_expr : false_expr`).
* **Dual-Rail 4-State Bitwise Evaluation:** Exact per-bit IEEE 1800 evaluation rules for unknown bits (e.g., `0 & X = 0`, `1 | X = 1`, `X ^ X = X`, `~X = X`), preserving hardware-accurate uninitialized propagation across wide datapath buses.
* **Native 4-State Logic Engine (`0, 1, X, Z`):** Dual-rail bit-vector arithmetic (`aval` data, `bval` unknown mask). All un-driven nets and uninitialized registers initialize to `X` (unknown) rather than optimistic `0`, catching un-reset design bugs.
* **Project-Centric Workspace Engine:** Isolated directory hierarchies (`projects/<name>/`) storing RTL designs (`design.sv`), testbenches (`testbench.sv`), metadata (`project.json`), and archived waveform traces (`waves/`).
* **Persistent Waveform History:** Timestamped VCD trace runs stored per project with in-browser selection, deletion, and immediate canvas invalidation when dump directives are omitted.
* **4-State Digital Timing Canvas:** Native HTML5 Canvas timing diagram renderer displaying clocks, single-bit logic wires, and multibit transition buses with distinct visual styles for known values vs. `X`/`Z` states.
* **Native VCD Waveform Generation:** Direct export of IEEE 1364/1800 Value Change Dump (`.vcd`) traces supporting `$dumpfile` and `$dumpvars` with hierarchical scope definitions and compact ASCII symbol mapping.
* **Stratified IEEE 1800 Scheduling (Active vs. NBA):** Evaluates non-blocking assignments (`<=`) during the active region and buffers values into an NBA update queue, committing all register transitions simultaneously before triggering combinational delta passes.
* **Edge-Triggered Sensitivity Engine:** Dynamic transition tracking (`is_posedge()`, `is_negedge()`) across hierarchical signal handles for `always_ff` blocks with asynchronous resets (`X -> 0` correctly triggers `negedge`).
* **Native Discrete-Event Time Wheel:** Min-heap priority queue (`std::priority_queue`) that advances simulation time dynamically across `#delay` intervals.

---

## Verified Reference Design (`v0.7.0`)

### RTL Design (`design.sv`)
```systemverilog
module alu (
    input  logic [3:0] a,
    input  logic [3:0] b,
    input  logic [1:0] op,
    output logic [3:0] result,
    output logic       is_zero
);

always_comb begin
    result = (op == 2'd0) ? (a & b) :
             (op == 2'd1) ? (a | b) :
             (op == 2'd2) ? (a ^ b) :
                            (a << 4'd1);

    is_zero = (result == 4'd0) ? 1'd1 : 1'd0;
end

endmodule

```

### Testbench (`testbench.sv`)

```systemverilog
module tb_alu;

    logic [3:0] a;
    logic [3:0] b;
    logic [1:0] op;
    logic [3:0] result;
    logic       is_zero;

    alu u_dut (
        .a(a),
        .b(b),
        .op(op),
        .result(result),
        .is_zero(is_zero)
    );

    initial begin
        $dumpfile("alu_wave.vcd");
        $dumpvars;

        // 1. Bitwise AND: 4'b1100 & 4'b1010 -> 4'b1000 (8)
        a = 4'b1100;
        b = 4'b1010;
        op = 2'd0;
        #10;
        $display("[Time=%0t] AND -> res=%b (is_zero=%d)", $time, result, is_zero);

        // 2. Bitwise OR: 4'b1100 | 4'b1010 -> 4'b1110 (14)
        op = 2'd1;
        #10;
        $display("[Time=%0t] OR  -> res=%b (is_zero=%d)", $time, result, is_zero);

        // 3. Bitwise XOR: 4'b1100 ^ 4'b1010 -> 4'b0110 (6)
        op = 2'd2;
        #10;
        $display("[Time=%0t] XOR -> res=%b (is_zero=%d)", $time, result, is_zero);

        // 4. Shift Left: 4'b0011 << 1 -> 4'b0110 (6)
        a = 4'b0011;
        op = 2'd3;
        #10;
        $display("[Time=%0t] SHL -> res=%b (is_zero=%d)", $time, result, is_zero);

        // 5. Zero flag verification: 4'b0000 & 4'b1111 -> 0 (is_zero=1)
        a = 4'd0;
        op = 2'd0;
        #10;
        $display("[Time=%0t] ZERO -> res=%d (is_zero=%d)", $time, result, is_zero);

        $finish;
    end

endmodule

```

### Simulation Output

```text
CAS (Cycle Accurate Simulator) v0.7.0 [Extended ALU Kernel] Initiated
Hierarchical Nets Elaborated: 10
Port Interconnects Bound: 5
--------------------------------------------------------
[CAS $display] [Time=10] AND -> res=1000 (is_zero=0)
[CAS $display] [Time=20] OR  -> res=1110 (is_zero=0)
[CAS $display] [Time=30] XOR -> res=0110 (is_zero=0)
[CAS $display] [Time=40] SHL -> res=0110 (is_zero=0)
[CAS $display] [Time=50] ZERO -> res=0 (is_zero=1)
[CAS $finish] Simulation Terminated at time 50.
[CAS VCD] Trace successfully dumped to: alu_wave.vcd
--------------------------------------------------------
Simulation finished at sim_time = 50.

```

---

## Project Structure

```text
CAS/
├── include/
│   ├── ast.hpp          # AST node structs, system tasks, edge types, ternary nodes
│   ├── lexer.hpp        # Lexer declarations, multi-char operator tokens
│   ├── parser.hpp       # Recursive-descent grammar parser with precedence climbing
│   ├── netlist.hpp      # Dual-rail 4-state LogicVal (bitwise AND/OR/XOR/NOT/shifts/rel)
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

### Build Native Statically-Linked Binary via Docker

```bash
docker run --rm -v "${PWD}:/app" -w /app/CAS alpine:3.20 sh -c \
    "apk add --no-cache build-base cmake && cmake -B build -DCMAKE_EXE_LINKER_FLAGS='-static' && cmake --build build"

```

### Run CLI Simulation Directly on Host

```bash
./CAS/build/cas projects/adder/testbench.sv projects/adder/design.sv

```

### Launch Web Environment

```bash
docker run --rm -p 8080:8080 -v "${PWD}:/app" eda-web

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
| **`v0.6.0`** | 4-State Logic Engine (`0, 1, X, Z`): Dual-rail bit vectors (`aval` + `bval`), unknown poisoning, 4-state edge transitions. | **Released** |
| **`v0.7.0`** | **Bitwise & Logical Operators:** Extended ALU operator suite (`&`, `|`, `^`, `~`, `<<`, `>>`, `==`, `!=`, `<`, `<=`, `>`, `>=`) and ternary conditional expressions (`?:`). | **Released** |
| **`v0.8.0`** | **Control Flow & Selection:** `case`, `casex`, `casez` statements and bit-slice indexing (`wire[idx]`, `wire[msb:lsb]`). | *Next* |
| **`v1.0.0`** | **Benchmarking:** Cycle-accurate execution of open-source RISC-V cores with automated throughput comparisons against commercial simulators. | *Planned* |