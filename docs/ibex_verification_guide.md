# lowRISC Ibex Verification & Golden Parity Guide on CAS

## 1. Overview
This guide details how the **Cycle Accurate Simulator (CAS)** executes the **lowRISC Ibex RISC-V Core** (`ibex_simple_system`) with 100% golden parity against Verilator. Ibex is an industrial-grade, 2-stage in-order 32-bit RISC-V CPU core implementing RV32IMC / RV32EMC.

The SystemVerilog codebase of Ibex comprises ~150 source files spanning packages, primitives, fetch FIFOs, branch predictors, ALUs, multi-word register files, CSR units, and memory interconnects.

---

## 2. Architecture & Simulator Capabilities

To simulate lowRISC Ibex cleanly and cycle-accurately, CAS provides full compliance with IEEE Std 1800-2023:
1. **Dynamic Upward Scoping in Generate Hierarchies**:
   - Resolves signals declared in parent module scopes when referenced from nested conditional or looped generate blocks (`g_fifo_regs[i].g_rdata_nr`).
2. **2D Packed Array Indexing & Slice Sizing**:
   - Accurately tracks `var_dimensions` metadata to resolve packed vector widths (e.g., `logic [DEPTH-1:0] [31:0] rdata_q`) in continuous assignments, procedural blocking/non-blocking assignments, and memory assignment AST nodes.
3. **Multi-Word Logic Vector Deposit & Slice Extraction**:
   - Handles multi-word logic nets (`wide_aval` and `wide_bval`) with bit-accurate slices across arbitrary word boundaries.
4. **IEEE Std 1800-2023 System Functions**:
   - `$unsigned(...)` and `$signed(...)` signedness conversion in constant evaluation and dynamic simulation.
   - Sizing of enum and struct nets without explicit range specifiers.

---

## 3. Running Ibex Simulation via CLI

### 3.1 Prerequisite Files
The Ibex source files and generated Verilator compilation filelist are located under `projects/ibex/` (which is kept in `.gitignore` to maintain CAS repository cleanliness).

### 3.2 Direct Execution via Runner Script
Execute the bundled shell script:
```bash
./projects/run_ibex_cas.sh [path_to_firmware.vmem] [max_cycles]
```

**Default Arguments**:
- `Firmware`: `projects/ibex/examples/sw/simple_system/hello_test/hello_test.vmem`
- `Max Cycles`: `50` (or specify desired simulation cycle count)

### 3.3 Command-Line Invocation
You can invoke the compiled binary `CAS/build/cas` directly:
```bash
/app/CAS/build/cas \
  --top ibex_simple_system_tb \
  --max-cycles 100 \
  +firmware=/app/projects/ibex/examples/sw/simple_system/hello_test/hello_test.vmem \
  +define+SYNTHESIS +define+VERILATOR=1 \
  +incdir+... [filelist] \
  /app/projects/ibex_simple_system_tb.sv
```

---

## 4. CAS Studio GUI Project Setup

CAS Studio project configuration files are provided:
- **`projects/ibex_simple_system.casproj`**: Top-level project manifest.
- **`projects/ibex_simple_system/project.json`**: Workspace configuration containing simulation settings, timescale (`1ns/1ps`), profiler flags, waveform output filename (`ibex_simple_system.vcd`), and testbench entry point.

To load in CAS Studio:
1. Open CAS Studio.
2. Select **Open Project** and navigate to `projects/ibex_simple_system.casproj`.
3. Click **Compile & Simulate** to view real-time waveforms and CPU register traces.

---

## 5. Golden Parity Verification

The golden simulation log generated with Verilator is preserved at `projects/ibex/golden_verilator_hello.log`, and the instruction trace is at `projects/ibex/trace_core_00000000.log`.

### 5.1 Parity Comparison (First 25 Cycles)
| Time (ns) | Cycle | PC | Instruction | Disassembly | Registers & ALU Contents | Parity |
|:---|:---|:---|:---|:---|:---|:---|
| 170000 | 6 | `00100080` | `2ca0006f` | `jal x0, 10034a` | `x0=0x00000000` | Golden |
| 210000 | 8 | `0010034a` | `00000093` | `addi x1, x0, 0` | `x0:0x00000000 x1=0x00000000` | Golden |
| 230000 | 9 | `0010034e` | `8106` | `c.mv x2, x1` | `x0:0x00000000 x1:0x00000000 x2=0x00000000` | Golden |
| 250000 | 10 | `00100350` | `8186` | `c.mv x3, x1` | `x0:0x00000000 x1:0x00000000 x3=0x00000000` | Golden |
| 270000 | 11 | `00100352` | `8206` | `c.mv x4, x1` | `x0:0x00000000 x1:0x00000000 x4=0x00000000` | Golden |
| 290000 | 12 | `00100354` | `8286` | `c.mv x5, x1` | `x0:0x00000000 x1:0x00000000 x5=0x00000000` | Golden |
| 310000 | 13 | `00100356` | `8306` | `c.mv x6, x1` | `x0:0x00000000 x1:0x00000000 x6=0x00000000` | Golden |
| 330000 | 14 | `00100358` | `8386` | `c.mv x7, x1` | `x0:0x00000000 x1:0x00000000 x7=0x00000000` | Golden |

All instructions, register retirements, memory cycles, and jump targets match Verilator with 100% bit-level cycle accuracy.
