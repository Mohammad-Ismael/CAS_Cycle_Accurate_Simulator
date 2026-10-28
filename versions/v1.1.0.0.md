# Release v1.1.0.0: Strict IEEE 1800-2023 4-State Logic Core & Modularization

## 1. Milestone Overview
This release establishes **v1.1.0.0** of the Cycle Accurate Simulator (CAS). This milestone implements the strict **IEEE Std 1800-2023 Chapters 6, 7 & 11 (Data Types, 4-State Logic & Operator Semantics)** core. All expressions, constants, literals, and gate operations now correctly preserve and propagate 4-state logic (`0`, `1`, `X`, `Z`) with bit-level precision, verified character-for-character against Synopsys VCS.

---

## 2. Key Architecture Achievements & Refactorings

### A. Modular 4-State Types Decomposition
Separated previously coupled structures from `types.hpp` into single-responsibility header and implementation units:
- **`edge_types.hpp` (`CAS/include/common/edge_types.hpp`)**:
  - Isolated clock edge enumerations (`POSEDGE`, `NEGEDGE`, `LEVEL`).
- **`gate_types.hpp` (`CAS/include/common/gate_types.hpp`)**:
  - Isolated gate primitive enumerations (`AND`, `OR`, `XOR`, `NAND`, `NOR`, `XNOR`, `NOT`, `BUF`).
- **`logic_val.hpp` (`CAS/include/common/logic_val.hpp`)**:
  - Clean 2-bit dual-plane data representation (`aval` for data bitplane, `bval` for unknown/control bitplane):
    - `0`: `aval = 0, bval = 0`
    - `1`: `aval = 1, bval = 0`
    - `X`: `aval = 1, bval = 1`
    - `Z`: `aval = 0, bval = 1`
  - Encapsulates bit-masking, formatting, and conversion routines without arithmetic dependencies.
- **`logic_ops.hpp` (`CAS/include/kernel/logic_ops.hpp`)**:
  - Dedicated static operator engine implementing standard IEEE 1800 Section 11 truth tables for bitwise, logical, equality, and ternary operations.

### B. Dedicated Numeric Literal Parser (`NumberParser`)
- Extracted literal lexical scanning from `lexer.cpp` into a dedicated modular component: `CAS/include/frontend/number_parser.hpp` and `CAS/src/frontend/number_parser.cpp`.
- Resolved an issue where sized literals containing unknown bits (e.g., `4'b01x0`) collapsed entirely into `4'bxxxx`. Exact bit positions of `0`, `1`, `x`, and `z` are now preserved in AST `NumberNode` using explicit `aval` and `bval` fields.
- Implemented unbased unsized literals (`'0`, `'1`, `'x`, `'z`) expanding to 64-bit dual-plane masks.

### C. Operator Compliance Highlights (IEEE Std 1800-2023)
- **Dominant Bitwise Logic (§11.4.1)**:
  - `0 & X = 0` (Dominant 0)
  - `1 | X = 1` (Dominant 1)
  - `X ^ X = X` (Preserved unknown propagation)
- **Equality Semantics (§11.4.5)**:
  - Logical equality (`==`, `!=`) evaluates to `1'bx` when unknown bits are present.
  - Case equality (`===`, `!==`) performs strict 4-state bit comparisons (`X === X` is `1`, `Z === Z` is `1`).
- **Ternary Ambiguity Merge (§11.4.11)**:
  - When the ternary condition `cond` is unknown (`1'bx`), matching bits from true and false branches pass through cleanly, and only conflicting bit positions resolve to `X` (e.g., `1'bx ? 4'b1100 : 4'b1010` yields `4'b1xx0`).

---

## 3. Verification & Golden Comparison
- **Benchmark Target**: `tb_v1_1_0_0_regression` driving `v1_1_0_0_four_state_core`.
- **Golden Reference**: Synopsys VCS X-2025.06-SP1 / EDA Playground.
- **Assertion Result**: `PASS (100% VCS COMPLIANT)` across all operator tests.

### Comparison Summary
| Test Condition | Synopsys VCS Output | CAS v1.1.0.0 Output | Result |
| :--- | :--- | :--- | :--- |
| `4'b01x0 & 4'b011x` | `01x0` | `01x0` | **MATCH** |
| `4'b01x0 \| 4'b011x` | `011x` | `011x` | **MATCH** |
| `4'b01x0 ^ 4'b011x` | `00xx` | `00xx` | **MATCH** |
| `4'b01x0 == 4'b011x` | `x` | `x` | **MATCH** |
| `4'b01x0 === 4'b011x` | `0` | `0` | **MATCH** |
| `4'b01x0 === 4'b01x0` | `1` | `1` | **MATCH** |
| `1'bx ? 4'b1100 : 4'b1010` | `1xx0` | `1xx0` | **MATCH** |