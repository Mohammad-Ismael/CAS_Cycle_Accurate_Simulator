#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CAS_BIN="${ROOT_DIR}/CAS/build/cas"

FIRMWARE="${1:-${ROOT_DIR}/projects/ibex/examples/sw/simple_system/hello_test/hello_test.vmem}"
MAX_CYCLES="${2:-50}"

VC_FILE="${ROOT_DIR}/projects/ibex/build/lowrisc_ibex_ibex_simple_system_0/sim-verilator/lowrisc_ibex_ibex_simple_system_0.vc"
SRC_DIR="${ROOT_DIR}/projects/ibex/build/lowrisc_ibex_ibex_simple_system_0/sim-verilator"

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

TB_FILE="${SCRIPT_DIR}/ibex_simple_system_tb.sv"

echo "=========================================================="
echo " Starting lowRISC Ibex Simulation on Cycle Accurate Simulator (CAS)"
echo " Firmware  : ${FIRMWARE}"
echo " Max Cycles: ${MAX_CYCLES}"
echo " Top Module: ibex_simple_system_tb"
echo "=========================================================="

"${CAS_BIN}" \
  --top ibex_simple_system_tb \
  --max-cycles "${MAX_CYCLES}" \
  --no-vcd \
  "+firmware=${FIRMWARE}" \
  "${DEFINES[@]}" \
  "${INCDIRS[@]}" \
  "${SRC_FILES[@]}" \
  "${TB_FILE}"
