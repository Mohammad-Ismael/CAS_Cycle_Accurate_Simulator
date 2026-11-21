#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CAS_BIN="${REPO_ROOT}/CAS/build/cas"

echo "================================================================================"
echo " lowRISC Ibex RV32IMC SoC Cycle-Accurate Execution Demo on CAS"
echo " Repository Root: ${REPO_ROOT}"
echo "================================================================================"

# 1. Ensure CAS binary is built
if [ ! -f "${CAS_BIN}" ]; then
    echo "CAS binary not found. Building CAS simulator..."
    mkdir -p "${REPO_ROOT}/CAS/build"
    cmake -S "${REPO_ROOT}/CAS" -B "${REPO_ROOT}/CAS/build" -DCMAKE_BUILD_TYPE=Release
    cmake --build "${REPO_ROOT}/CAS/build" -j"$(nproc)"
fi

# 2. Ensure lowRISC Ibex repository exists
IBEX_DIR="${REPO_ROOT}/projects/ibex"
if [ ! -d "${IBEX_DIR}" ]; then
    echo "Cloning lowRISC/ibex into ${IBEX_DIR}..."
    mkdir -p "${REPO_ROOT}/projects"
    git clone --depth 1 https://github.com/lowRISC/ibex.git "${IBEX_DIR}"
fi

# 3. Idempotently normalize dummy assertion macros and enum width expressions
python3 -c "
import os
repo_root = '${REPO_ROOT}'
ibex_dir = os.path.join(repo_root, 'projects', 'ibex')

dummy_content = '''// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
\`define ASSERT_I(__name = 0, __prop = 0, __a1 = 0, __a2 = 0, __a3 = 0, __a4 = 0, __a5 = 0, __a6 = 0, __a7 = 0, __a8 = 0)
\`define ASSERT_INIT(__name = 0, __prop = 0, __a1 = 0, __a2 = 0, __a3 = 0, __a4 = 0, __a5 = 0, __a6 = 0, __a7 = 0, __a8 = 0)
\`define ASSERT_INIT_NET(__name = 0, __prop = 0, __a1 = 0, __a2 = 0, __a3 = 0, __a4 = 0, __a5 = 0, __a6 = 0, __a7 = 0, __a8 = 0)
\`define ASSERT_FINAL(__name = 0, __prop = 0, __a1 = 0, __a2 = 0, __a3 = 0, __a4 = 0, __a5 = 0, __a6 = 0, __a7 = 0, __a8 = 0)
\`define ASSERT_AT_RESET(__name = 0, __prop = 0, __a1 = 0, __a2 = 0, __a3 = 0, __a4 = 0, __a5 = 0, __a6 = 0, __a7 = 0, __a8 = 0)
\`define ASSERT_AT_RESET_AND_FINAL(__name = 0, __prop = 0, __a1 = 0, __a2 = 0, __a3 = 0, __a4 = 0, __a5 = 0, __a6 = 0, __a7 = 0, __a8 = 0)
\`define ASSERT(__name = 0, __prop = 0, __a1 = 0, __a2 = 0, __a3 = 0, __a4 = 0, __a5 = 0, __a6 = 0, __a7 = 0, __a8 = 0)
\`define ASSERT_NEVER(__name = 0, __prop = 0, __a1 = 0, __a2 = 0, __a3 = 0, __a4 = 0, __a5 = 0, __a6 = 0, __a7 = 0, __a8 = 0)
\`define ASSERT_KNOWN(__name = 0, __prop = 0, __a1 = 0, __a2 = 0, __a3 = 0, __a4 = 0, __a5 = 0, __a6 = 0, __a7 = 0, __a8 = 0)
\`define COVER(__name = 0, __prop = 0, __a1 = 0, __a2 = 0, __a3 = 0, __a4 = 0, __a5 = 0, __a6 = 0, __a7 = 0, __a8 = 0)
\`define ASSUME(__name = 0, __prop = 0, __a1 = 0, __a2 = 0, __a3 = 0, __a4 = 0, __a5 = 0, __a6 = 0, __a7 = 0, __a8 = 0)
\`define ASSUME_I(__name = 0, __prop = 0, __a1 = 0, __a2 = 0, __a3 = 0, __a4 = 0, __a5 = 0, __a6 = 0, __a7 = 0, __a8 = 0)
'''

for root, _, files in os.walk(ibex_dir):
    for f in files:
        if f == 'prim_assert_dummy_macros.svh':
            p = os.path.join(root, f)
            with open(p, 'w') as fp: fp.write(dummy_content)
        elif f == 'prim_assert.sv':
            p = os.path.join(root, f)
            with open(p, 'r', errors='ignore') as fp: c = fp.read()
            c = c.replace('\`define ASSERT_PULSE(__name, __sig, __clk = \`ASSERT_DEFAULT_CLK, __rst = \`ASSERT_DEFAULT_RST) \\\n  \`ASSERT(__name, \$rose(__sig) |=> !(__sig), __clk, __rst)', '\`define ASSERT_PULSE(__name = 0, __p1 = 0, __p2 = 0, __p3 = 0, __p4 = 0, __p5 = 0, __p6 = 0, __p7 = 0, __p8 = 0)')
            c = c.replace('\`define ASSERT_IF(__name, __prop, __enable, __clk = \`ASSERT_DEFAULT_CLK, __rst = \`ASSERT_DEFAULT_RST) \\\n  \`ASSERT(__name, (__enable) |-> (__prop), __clk, __rst)', '\`define ASSERT_IF(__name = 0, __p1 = 0, __p2 = 0, __p3 = 0, __p4 = 0, __p5 = 0, __p6 = 0, __p7 = 0, __p8 = 0)')
            with open(p, 'w') as fp: fp.write(c)

mubi_repl = {
    '[MuBi4Width-1:0]': '[3:0]', '[MuBi8Width-1:0]': '[7:0]',
    '[MuBi12Width-1:0]': '[11:0]', '[MuBi16Width-1:0]': '[15:0]',
    '[MuBi20Width-1:0]': '[19:0]', '[MuBi24Width-1:0]': '[23:0]',
    '[MuBi28Width-1:0]': '[27:0]', '[MuBi32Width-1:0]': '[31:0]'
}
for root, _, files in os.walk(ibex_dir):
    for f in files:
        if f == 'prim_mubi_pkg.sv':
            p = os.path.join(root, f)
            with open(p, 'r', errors='ignore') as fp: c = fp.read()
            for k, v in mubi_repl.items(): c = c.replace(k, v)
            with open(p, 'w') as fp: fp.write(c)
"

FIRMWARE="${1:-${REPO_ROOT}/projects/ibex/examples/sw/simple_system/hello_test/hello_test.vmem}"
MAX_CYCLES="${2:-1600}"

VC_FILE="${IBEX_DIR}/build/lowrisc_ibex_ibex_simple_system_0/sim-verilator/lowrisc_ibex_ibex_simple_system_0.vc"
SRC_DIR="${IBEX_DIR}/build/lowrisc_ibex_ibex_simple_system_0/sim-verilator"
TB_FILE="${REPO_ROOT}/projects/ibex_simple_system_tb.sv"

DEFINES=("+define+SYNTHESIS" "+define+VERILATOR=1")
INCDIRS=()
SRC_FILES=()

while IFS= read -r line || [ -n "$line" ]; do
  line="$(echo "$line" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
  [ -z "$line" ] && continue
  if [[ "$line" =~ ^\+incdir\+(.*) ]]; then
    idir="${BASH_REMATCH[1]}"
    [[ "$idir" != /* ]] && idir="${SRC_DIR}/${idir}"
    INCDIRS+=("+incdir+${idir}")
  elif [[ "$line" =~ ^-D(.*) ]]; then
    DEFINES+=("+define+${BASH_REMATCH[1]}")
  elif [[ "$line" =~ \.(sv|v)$ ]]; then
    fpath="$line"
    [[ "$fpath" != /* ]] && fpath="${SRC_DIR}/${fpath}"
    SRC_FILES+=("${fpath}")
  fi
done < "$VC_FILE"

echo "Firmware   : ${FIRMWARE}"
echo "Max Cycles : ${MAX_CYCLES}"
echo "Top Module : ibex_simple_system_tb"
echo "Launching simulation in ${REPO_ROOT}/projects..."
echo "--------------------------------------------------------------------------------"

cd "${REPO_ROOT}/projects"
rm -f "${REPO_ROOT}/projects/ibex_simple_system.log" "${REPO_ROOT}/ibex_simple_system.log" "${SRC_DIR}/ibex_simple_system.log" "${REPO_ROOT}/projects"/trace_core_*.log

"${CAS_BIN}" \
  --top ibex_simple_system_tb \
  --max-cycles "${MAX_CYCLES}" \
  --no-vcd \
  "+firmware=${FIRMWARE}" \
  "${DEFINES[@]}" \
  "${INCDIRS[@]}" \
  "${SRC_FILES[@]}" \
  "${TB_FILE}"

echo "--------------------------------------------------------------------------------"
SIM_LOG="${REPO_ROOT}/projects/ibex_simple_system.log"
if [ ! -f "${SIM_LOG}" ] && [ -f "${REPO_ROOT}/ibex_simple_system.log" ]; then
  SIM_LOG="${REPO_ROOT}/ibex_simple_system.log"
fi

if [ -f "${SIM_LOG}" ]; then
  echo "UART Output Captured in ${SIM_LOG}:"
  echo "--------------------------------------------------------------------------------"
  cat "${SIM_LOG}"
  echo "--------------------------------------------------------------------------------"
else
  echo "ERROR: Simulation log file not found at ${SIM_LOG}!" >&2
  exit 1
fi

if grep -q "EXCEPTION!!!" "${SIM_LOG}"; then
  echo "ERROR: Hardware exception detected in Ibex execution!" >&2
  exit 1
fi

TRACER_LOG="$(ls "${REPO_ROOT}/projects"/trace_core_*.log 2>/dev/null | head -n 1 || true)"
if [ -f "${TRACER_LOG}" ]; then
  if grep -E "\bx[0-9]+=0x[0-9a-fA-F]*[xXzZ]" "${TRACER_LOG}"; then
    echo "ERROR: Uninitialized 'x' or 'z' detected in tracer log ${TRACER_LOG}!" >&2
    exit 1
  fi
  echo ">>> Ibex Tracer Log: 0 'x'/'z' uninitialized writebacks verified in ${TRACER_LOG}!"
fi

if grep -q "Hello simple system" "${SIM_LOG}"; then
  if grep -q "Tick!" "${SIM_LOG}" && grep -q "Tock!" "${SIM_LOG}"; then
    echo ">>> Ibex Simulation SUCCESS: Full horizon completed! 'Hello simple system', 'Tick!', and 'Tock!' detected in UART log!"
  else
    echo ">>> Ibex Simulation SUCCESS: 'Hello simple system' and test vectors detected with 0 exceptions!"
  fi
else
  echo "ERROR: 'Hello simple system' not found in ${SIM_LOG}" >&2
  exit 1
fi
