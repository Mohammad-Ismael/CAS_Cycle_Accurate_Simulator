#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CAS_BIN="${ROOT_DIR}/CAS/build/cas"
RV_ROOT="${SCRIPT_DIR}/veer_el2"

FIRMWARE="${1:-${RV_ROOT}/testbench/hex/user_mode0/hello_world.hex}"
MAX_CYCLES="${2:-10000}"

cd "${RV_ROOT}"
cp -f "${FIRMWARE}" program.hex

DEFINES=(
  +define+DEBUG
  "+define+RV_OPENSOURCE"
  "+define+RV_FPGA_OPTIMIZE"
  "+define+GTLSIM"
)

INCDIRS=(
  "+incdir+${RV_ROOT}/snapshots/default"
  "+incdir+${RV_ROOT}/testbench/axi4_mux"
  "+incdir+${RV_ROOT}/testbench"
  "+incdir+${RV_ROOT}/design/include"
  "+incdir+${RV_ROOT}/design/lib"
)

SRC_FILES=(
  "${RV_ROOT}/snapshots/default/common_defines.vh"
  "${RV_ROOT}/design/include/el2_def.sv"
  "${RV_ROOT}/snapshots/default/el2_pdef.vh"
  "${RV_ROOT}/testbench/tb_top_pkg.sv"
)

while IFS= read -r line || [ -n "$line" ]; do
  line="$(echo "$line" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
  [ -z "$line" ] && continue
  [[ "$line" =~ ^// ]] && continue
  if [[ "$line" =~ ^\+incdir\+(.*) ]]; then
    idir="${BASH_REMATCH[1]}"
    idir="${idir/\$RV_ROOT/$RV_ROOT}"
    INCDIRS+=("+incdir+${idir}")
  elif [[ "$line" =~ \.(sv|v)$ ]]; then
    fpath="$line"
    fpath="${fpath/\$RV_ROOT/$RV_ROOT}"
    fpath="$(echo "$fpath" | sed 's/^-v //')"
    if [ -f "$fpath" ]; then
      SRC_FILES+=("${fpath}")
    fi
  fi
done < "${RV_ROOT}/testbench/flist"

EXTRA_FILES=(
  "${RV_ROOT}/testbench/ahb_sif.sv"
  "${RV_ROOT}/testbench/ahb_lite_2to1_mux.sv"
  "${RV_ROOT}/testbench/ahb_lsu_dma_bridge.sv"
  "${RV_ROOT}/testbench/axi4_mux/axi_crossbar_wrap_2x1.v"
  "${RV_ROOT}/testbench/axi4_mux/arbiter.v"
  "${RV_ROOT}/testbench/axi4_mux/axi_crossbar_addr.v"
  "${RV_ROOT}/testbench/axi4_mux/axi_crossbar_rd.v"
  "${RV_ROOT}/testbench/axi4_mux/axi_crossbar.v"
  "${RV_ROOT}/testbench/axi4_mux/axi_crossbar_wr.v"
  "${RV_ROOT}/testbench/axi4_mux/axi_register_rd.v"
  "${RV_ROOT}/testbench/axi4_mux/axi_register_wr.v"
  "${RV_ROOT}/testbench/axi4_mux/priority_encoder.v"
  "${RV_ROOT}/testbench/tb_top.sv"
)

for ef in "${EXTRA_FILES[@]}"; do
  SRC_FILES+=("${ef}")
done

echo "=========================================================="
echo " Starting Western Digital VeeR EL2 Simulation on CAS"
echo " Firmware  : ${FIRMWARE}"
echo " Max Cycles: ${MAX_CYCLES}"
echo " Top Module: tb_top"
echo " Total Files: ${#SRC_FILES[@]}"
echo "=========================================================="

DEBUG_PREFIX=()
if [ "${CAS_DEBUG:-0}" = "1" ]; then
  DEBUG_PREFIX=(gdb -batch -ex run -ex "bt full" --args)
fi

SIM_STEPS=$(( MAX_CYCLES * 2 + 5 ))

"${DEBUG_PREFIX[@]}" "${CAS_BIN}" \
  --top tb_top \
  --max-cycles "${SIM_STEPS}" \
  --no-vcd --quiet \
  "${DEFINES[@]}" \
  "${INCDIRS[@]}" \
  "${SRC_FILES[@]}"
