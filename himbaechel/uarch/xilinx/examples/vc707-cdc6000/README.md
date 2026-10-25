# VC707 CDC 6000 Pascal machine (xc7vx485tffg1761-2)

A CDC 6000 processor that runs the **1972 Zurich Pascal compiler**, whose 1972
source is unmodified, and **compiles that compiler with itself** on the board.
It has the same 1G Ethernet MAC, LiteEth SGMII PCS/PMA over a GTX and UART
console as `vc707-ocaml`, plus a 1 MB block-RAM disk. The core is
`cdc6000_sb`: 60-bit words, CDC floating point, an instruction stack and a
scoreboard, speculation past branches and dual issue.

Like the other SoC entries it ships a **netlist**, not Verilog:
`vc707_cdc_sb.json.gz` is yosys' output, gzipped (98 MB raw, 5.4 MB here).
yosys writes the memory images into the netlist as RAMB `INIT` parameters,
so the netlist alone is the whole machine and needs no CDC toolchain to
rebuild:
- the core's 128K-word memory holds the shell, the NFS driver and the
  compiler, all CDC machine code built by the compiler itself;
- the disk holds the compiler's source (`pascal.pas`) and its image.

## On hardware

This exact netlist was placed and routed with the flags below, assembled,
loaded over JTAG, and run on a VC707 on 2026-10-15:

| at the shell's prompt | result |
|---|---|
| `ram`, `compile pascal` (the compiler compiling itself from the block-RAM disk, no network) | 0 error lines, 3277 ms |
| `ram` (back to NFS), `compile pascal` | 0 error lines, 7433 ms; the listing, the object and the post-mortem dump it wrote over NFS are byte-identical to the host-built reference |

The NFS compile moves about half a megabyte each way through the packet DMA.
An open-flow bitstream of the same design from an older private nextpnr
(`e558304b`, placed against an older prjxray-db than the one it was assembled
against) configured but never got past boot: no UART output, no Ethernet TX.
This layout has 20 `CK_BUFG_CASCO` cascade pips where that one had 10; which
difference killed it was not isolated.

## What it covers

About 59,800 cells. Of those, 376 are RAMB36 (36 % of the part) and 9 are
DSP48E1, with 55 K LUTs and 3 MMCMs. It is the largest block-RAM user in
the gate:

- **Deep, wide block RAM with contents.** The core's memory is 60-bit words
  in 120 RAMB36. The disk is 256 RAMB36 read and written a byte at a time
  through one address register. Both carry `INIT` data that must survive
  assembly, or the machine boots into nothing.
- **A 60-bit datapath.** That means 60-bit integer and floating-point add,
  a multiplier on DSP48E1s, and a long-shift and normalise network.
  These are wide carry chains and wide muxes that the 32- and 64-bit SoCs do
  not reach in the same shape.
- **Hold-fix territory,** as in `vc707-ocaml`: the same packet DMA writes to
  block RAM.
- **The MMCM-to-BUFG cascade** of nextpnr #104, with the PHY's and the
  clock generator's MMCMs feeding BUFGs through the top `CLK_HROW` tiles.

It is `stretch` tier for the same reason as `vc707-ocaml`: the question is
whether it comes out, closes timing and stays correct.

## The clock and the flags

The netlist asks for **50 MHz** on `clk_sys`. The MMCM in the netlist sets
it (`SYS_DIV=20.000`), and the XDC does not. The run behind the board
results reported:

    clk_sys        55.67 MHz  (PASS at 50)
    eth_tx_clk    257.40 MHz  (PASS at 125)
    eth_rx_clk    224.47 MHz  (PASS at 125)
    clk_mac       255.56 MHz  (PASS at 125)
    Hold-fix: no hold violations to fix.

The entry pins the placer flags that run used:
`--placer-heap-timingweight 60 --placer-heap-congestion-spread --tmg-ripup`.
On this design, the congestion spread and timing-driven rip-up measured
better than the gate default alone. P&R took about 80 minutes on a desktop
core, which is why the timeout is long.

Vivado closes the same design at 100 MHz, using overconstrained placement
and post-route physical optimisation with retiming. It runs at 100 MHz on
the board.

## Build recipe

The sources are https://github.com/jrrk2/cdc6000 at `a6a5700`
(`rtl/cdc6000_sb.sv`, `fpga/vc707-eth`). From that checkout:

    fpga/vc707-eth/mkimage_shell.sh <dir>        # memory and disk images -> <dir>/cdc_ram_bram.v, cdc_disk_bram.v
    fpga/vc707-eth/openflow.sh <dir> sb 20.000 50000000

`openflow.sh` does the following:
1. sv2v (v0.0.13-11-g6662fa5) turns the SystemVerilog core into Verilog.
2. yosys synthesises it: 0.63+173, git `66306a8ca`, the pinned yosys of
   xc7-bitstream-tools, with
   `synth_xilinx -flatten -abc9 -arch xc7 -top vc707_cdc_eth`.
3. nextpnr places and routes it with
   `--router router2 --timing-allow-fail -o hold-fix` and the placer flags
   above. That run used upstream nextpnr `c4dc8713`, with Eigen 3.4.0.
4. The bitstream is assembled against openXC7 prjxray-db `b12466df`.

The bitstream from that run is in the release
https://github.com/jrrk2/cdc6000/releases/tag/vc707-2026-10-15 as
`cdc6000_vc707_openflow_50mhz.bit` (sha256 `41c0e7e2...`).
