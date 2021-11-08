# reSID

## MOS 6581 / 8580 SID software emulation

This repository is intended to become the official source for the
reSID MOS6581 / MOS8580 SID emulator. However for now, I recommend
fetching reSID from [VICE](https://vice-emu.sourceforge.io/) :-)

reSID is widely used in two different incarnations - reSID 0.16, which
was released in 2004, and a reSID 1.0 prerelease, which was included
in VICE in 2010 - 2011, and has been patched by others since
then. Unfortunately I never got around to make an official reSID 1.0
release.

I put in a lot of work to further reverse engineer the SID chip and
advance the state of the art of SID emulation in 2010 - 2011. This
work was in large part based on [SID die
shots](https://retronn.de/imports/commodore_chips.html), photographed
and stitched by Michael Huth, and revectorized and annotated by Tommi
Lempinen, with some further analysis by Frank Wolf. Several
improvements were made in the digital domain, however the major
achievement was vastly more accurate emulation in the analog domain by
simulation of the actual analog circuits, using detailed models of
DACs, VCRs, and op-amps. Op-amp transfer functions were obtained by
feeding and measuring voltages directly on SID filter capacitor pins
(with capacitors removed).

A few years later, in 2016, Leandro Nini (drfiemost) started the
thread ["Understanding the SID"](http://forum.6502.org/viewtopic.php?f=8&t=4150)
on [forum.6502.org](http://forum.6502.org). The goal was to do a
*complete* reverse engineering of the SID chip, based on the same die
shots I had worked with earlier. Dieter Mueller (ttlworks) was a major
contributor to the reverse engineering effort. Leandro Nini's and
Dieter Mueller's work resulted in detailed
[SID internals documentation](https://sourceforge.net/p/sidplay-residfp/wiki/SID%20internals/)
and transistor level
[SID schematics](https://github.com/libsidplayfp/SID_schematics).
Leandro Nini even went as far as to create a complete transistor level
simulation of the digital parts of the SID chip called
[perfect6581](https://github.com/libsidplayfp/perfect6581),
based on
[perfect6502](https://github.com/mist64/perfect6502).

In this repository I am going to pick up from where I left some ten
years ago, cherry picking from the reverse engineering mentioned
above, patches in VICE, and my own research, with the goal of making
reSID even better.

I also plan to use a refined reSID as the basis for an FPGA Verilog
implementation for my take on a hardware SID replacement, the [reDIP
SID](https://github.com/daglem/reDIP-SID).
