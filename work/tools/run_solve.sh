#!/bin/bash
set -e
cd "$(dirname "$0")/.."

LIB=~/.volare/volare/sky130/versions/0fe599b2afb6708d281543108caf8310912f54af/sky130A/libs.ref/sky130_fd_sc_hd/lib/sky130_fd_sc_hd__tt_025C_1v80.lib
PDK=~/.volare/volare/sky130/versions/0fe599b2afb6708d281543108caf8310912f54af/sky130A/libs.ref/sky130_fd_sc_hd/verilog

echo "== step 1: cell type list =="
grep -oE "sky130_fd_sc_hd__[a-z0-9]+_[0-9]+ " build/puzzle.v | sort -u | sed 's/ $//' > /tmp/celltypes.txt
wc -l /tmp/celltypes.txt

echo "== step 2: generate SMT file =="
./tools/solve build/puzzle.v "$LIB" /tmp/celltypes.txt > /tmp/puzzle.smt2
wc -l /tmp/puzzle.smt2

echo "== step 3: run z3 =="
z3 /tmp/puzzle.smt2 > /tmp/result.txt
head -1 /tmp/result.txt

echo "== step 4: extract the 121 bits =="
python3 tools/extract_bits.py > /tmp/answer.txt
cat /tmp/answer.txt

echo "== step 5: verify with sim.c =="
./tools/sim build/puzzle.v "$LIB" /tmp/celltypes.txt /tmp/answer.txt

echo "== step 6: verify with real iverilog =="
python3 tools/mktb.py
iverilog -g2012 -o /tmp/tb.out "$PDK/primitives.v" "$PDK/sky130_fd_sc_hd.v" build/puzzle.v /tmp/tb.v
vvp /tmp/tb.out
