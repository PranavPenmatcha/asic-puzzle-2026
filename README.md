# ASIC Reverse-Engineering Puzzle

This repository provides the files for the Jane Street ASIC reverse-engineering puzzle! See the [blog post](https://blog.janestreet.com/can-you-reverse-engineer-an-asic/) for more details.

### Solution Summarized
- Extracted the netlist from `puzzle.gds` using Magic + the OpenLane container, via `work/tools/extract.tcl`, producing `work/build/puzzle.spice`
- Wrote a SPICE-to-Verilog converter in C, `work/tools/spice_convert_to_v.c`, producing `work/build/puzzle.v`
- Wrote a netlist/cell parser and validated it against `iverilog`: `work/tools/netlist.c`, `work/tools/cells.c`, `work/tools/validate_cells.c`
- Built and debugged a gate-level simulator to inspect the chip's output: `work/tools/sim.c`
- Added the final solve scripts — fan-in cone tracing, VCD decoding, SMT encoding, and the answer/testbench pipeline: `work/tools/cone.c`, `work/tools/dcone.c`, `work/tools/vcd_decode.c`, `work/tools/solve.c`, `work/tools/extract_bits.py`, `work/tools/mktb.py`, `work/tools/run_solve.sh`

### Puzzle GDS

The puzzle GDS is in this repository, in the file named `work/puzzle.gds or puzzle_given.gds`. You can preview it using [KLayout](https://www.klayout.de/) or the [TinyTapeout Online GDS Viewer](https://gds-viewer.tinytapeout.com/).

See `example_inputs.vcd` which shows some inputs being fed to the design (unfortunately, not the correct inputs to make `success` go high!). You can view it using [Surfer](https://surfer-project.org/) or a similar tool.

To help me get started, below is the image given with some hints. The region labelled as "output generator" is safe to ignore during your initial reverse-engineering steps, but you'll need to simulate it to get your final answer!

![](layout.png)

Thanks for this oppurtunity!



