# verilog_preprocessor
pre-processes verilog to be interpretable by ODIN II

It was for doing some research I did using VTR (
https://github.com/verilog-to-routing/vtr-verilog-to-routing,
there's a copy of it in their source tree too).
There were some benchmarks that I got off of opencores that had some features that ODIN II didn't support,
and there was no way I was going to write the massive switch statements needed,
so I decided to hack together a program to do it for me.

The input were some verilog files with the occasional special comment,
and the output was a file that should be acceptable to ODIN II.
The output was a new version of that file, with
  - two dimensional wires/regs expanded into many 1 dimensional wires/regs,
  - some operations(such as modulo, and accessing 2 dimensional wires/regs) changed into big switch statements,
  - `define statments expanded properly
  - (and possibly some other things I don't remember).

The special comments told the preprocessor what to expect,
and and the effect of there being a particular `define statement defined from that line on,
which code later in the file would use.

I believe you can find all the inputs & outputs I was working with here:
https://github.com/verilog-to-routing/vtr-verilog-to-routing/tree/master/vtr_flow/benchmarks/arithmetic/open_cores/verilog/processed_files .
