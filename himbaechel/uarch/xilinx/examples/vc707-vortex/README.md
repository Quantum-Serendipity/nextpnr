# VC707 Vortex GPGPU (xc7vx485tffg1761-2)

The Vortex RISC-V GPGPU (https://github.com/vortexgpgpu/vortex), one core of
4 warps x 4 threads, RV32I with no floating point, standing alone on the
VC707: 256 KB of block RAM as device memory, a DCR launch sequencer in place
of the host driver, and the eight user LEDs as the only output.  The kernel
in the block RAM counts and stores the top bits of its counter to the LED
register; LED7 is the GPU's busy flag, which stays on because the kernel
never finishes.

Like the other SoC entries it ships a **netlist**, not Verilog:
`vortex_vc707.json.gz` (74 MB raw, 3.2 MB here) with the program in the
RAMB36 `INIT` parameters.  Unlike most of them it is not yosys' output: the
Verilator synthesis fork lowered the SystemVerilog (interfaces, modports,
unpacked array ports, streaming operators) to primitive cells.

## What it covers

    cells      171441     the largest netlist in the gate
    LUT         99083     LUT1..LUT6
    FDRE/FDSE   63419
    RAM64X1D     6815     register files, operand collectors, cache tags
    CARRY4       1416
    MUXF7         417
    RAMB36E1      241     23 of them initialised (the kernel)
    DSP48E1        37
    MMCME2_ADV      1     with two BUFGs, LOC-constrained

- **Distributed RAM at scale.** No other entry packs anything like 6,815
  RAM64X1D; the SLICEM supply and the packer's LUTRAM shapes are the first
  thing this design tests.
- **Router cost.** The largest route in the gate; its runtime is the number
  that shows a router or placer regression first.
- **Fmax** is sensitive here and is the number to watch, not gate on.

## On hardware

This netlist, placed and routed with the gate-wide flags
(`--placer-heap-timingweight 60`, router2, hold-fix) and the calibrated
xc7vx485t chipdb, assembled and loaded over JTAG on 2026-11-12: LEDs 6..0
count and LED7 is lit (busy).  That is a functional pass at the 50 MHz the
MMCM makes from the 200 MHz system clock, not a timing limit.

Measured on a 24-thread i9 desktop with this branch: 79 min for place and
route in all, router2 26 iterations in 1,351 s, hold-fix 13 buffers in 40 s;
routed Fmax 83.16 MHz (calibrated chipdb; the gate uses the stock one, so
its figure differs).  In October the route alone took 71 router2 iterations
and 4.6 h, so the entry stays stretch, with the maximum timeout as a margin
on hosted runners.

## Provenance

    netlist   https://github.com/jrrk2/vortex branch rom_update (375fb0301),
              hw/syn/xilinx/vc707, synthesised by synthgui (project
              vortex_vc707) with the Verilator synthesis fork at 8ac1a4992;
              defines SYNTHESIS NDEBUG VX_CFG_XLEN=32 VX_CFG_XLEN_32
              VX_CFG_EXT_F_DISABLE.  sha256 of the uncompressed JSON
              begins 328017d34644b20f
    xdc       hw/syn/xilinx/vc707/vortex_vc707_open.xdc: the 200 MHz LVDS
              system clock pair, CPU_RESET, LD0..7, and the BUFG/MMCM LOCs

The top has four ports (IO_CLK_P/N, IO_RST, LED[7:0]), all constrained by
the xdc.
