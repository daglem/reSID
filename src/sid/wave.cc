//  ---------------------------------------------------------------------------
//  This file is part of reSID, a MOS6581 SID emulator engine.
//  Copyright (C) 1998  Dag Lem <resid@nimrod.no>
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//  ---------------------------------------------------------------------------

#define __WAVE_CC__
#include "wave.h"

// ----------------------------------------------------------------------------
// Constructor.
// ----------------------------------------------------------------------------
WaveformGenerator::WaveformGenerator(WaveformGenerator* source)
{
  sync_source = source;
  source->sync_dest = this;
  reset();
}

// ----------------------------------------------------------------------------
// Register functions.
// ----------------------------------------------------------------------------
void WaveformGenerator::writeFREQ_LO(reg8 freq_lo)
{
  freq = freq & 0xff00 | freq_lo & 0x00ff;
}

void WaveformGenerator::writeFREQ_HI(reg8 freq_hi)
{
  freq = (freq_hi << 8) & 0xff00 | freq & 0x00ff;
}

void WaveformGenerator::writePW_LO(reg8 pw_lo)
{
  pw = pw & 0xf00 | pw_lo & 0x0ff;
}

void WaveformGenerator::writePW_HI(reg8 pw_hi)
{
  pw = (pw_hi << 8) & 0xf00 | pw & 0x0ff;
}

void WaveformGenerator::writeCONTROL_REG(reg8 control)
{
  waveform = (control >> 4) & 0x0f;
  ring_mod = control & 0x04;
  sync = control & 0x02;

  bool test_next = control & 0x08;

  // Test bit set.
  // The accumulator and the shift register are both cleared.
  // NB! The shift register is not really cleared immediately. It seems like
  // the individual bits in the shift register start to fade down towards
  // zero when test is set. All bits reach zero within approximately
  // $2000 - $4000 cycles.
  // This is not modeled. There should fortunately be little audible output
  // from this weird behavior.
  if (test_next) {
    accumulator = 0;
    shift_register = 0;
  }
  // Test bit cleared.
  // The accumulator starts counting, and the shift register is reset to
  // the value 0x7ffff8.
  // NB! The shift register will not actually be set to this exact value if the
  // shift register bits have not had time to fade to zero.
  // This is not modeled.
  else if (test) {
    shift_register = 0x7ffff8;
  }

  test = test_next;

  // The gate bit is handled by the EnvelopeGenerator.
}

reg8 WaveformGenerator::readOSC()
{
  return (this->*output_function[waveform])() >> 4;
}

// ----------------------------------------------------------------------------
// SID reset.
// ----------------------------------------------------------------------------
void WaveformGenerator::reset()
{
  accumulator = 0;
  shift_register = 0x7ffff8;
  freq = 0;
  pw = 0;

  test = false;
  ring_mod = false;
  sync = false;

  msb_rising = false;
}


// ----------------------------------------------------------------------------
// Output functions.
// ----------------------------------------------------------------------------

// No waveform:
// No output.
//
reg12 WaveformGenerator::output____()
{
  return 0;
}

// Triangle:
// The upper 12 bits of the accumulator are used.
// The MSB is used to create the falling edge of the triangle by inverting
// the lower 11 bits. The MSB is thrown away and the lower 11 bits are
// left-shifted (half the resolution, full amplitude).
// Ring modulation substitutes the MSB with MSB EOR sync_source MSB.
//
reg12 WaveformGenerator::output___T()
{
  bool msb = (ring_mod ? accumulator ^ sync_source->accumulator : accumulator)
    & 0x800000;
  return ((msb ? ~accumulator : accumulator) >> 11) & 0xfff;
}

// Sawtooth:
// The output is identical to the upper 12 bits of the accumulator.
//
reg12 WaveformGenerator::output__S_()
{
  return accumulator >> 12;
}

// Pulse:
// The upper 12 bits of the accumulator are used.
// These bits are compared to the pulse width register by a 12 bit digital
// comparator; output is either all one or all zero bits.
// NB! The output is actually delayed one cycle after the compare.
// This is not modeled.
//
reg12 WaveformGenerator::output_P__()
{
  return (accumulator >> 12) >= pw ? 0xfff : 0x000;
}

// Noise:
// The noise output is taken from intermediate bits of a 23-bit shift register
// which is clocked by bit 19 of the accumulator.
// NB! The output is actually delayed 2 cycles after bit 19 is set high.
// This is not modeled.
//
// Operation: Calculate EOR result, shift register, set bit 0 = result.
//
//                        ----------------------->---------------------
//                        |                                            |
//                   ----EOR----                                       |
//                   |         |                                       |
//                   2 2 2 1 1 1 1 1 1 1 1 1 1                         |
// Register bits:    2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 <---
//                   |   |       |     |   |       |     |   |
// OSC3 bits  :      7   6       5     4   3       2     1   0
//
// Since waveform output is 12 bits the output is left-shifted 4 times.
//
reg12 WaveformGenerator::outputN___()
{
  return
    ((shift_register & 0x400000) >> 11) |
    ((shift_register & 0x100000) >> 10) |
    ((shift_register & 0x010000) >> 7) |
    ((shift_register & 0x002000) >> 5) |
    ((shift_register & 0x000800) >> 4) |
    ((shift_register & 0x000080) >> 1) |
    ((shift_register & 0x000010) << 1) |
    ((shift_register & 0x000004) << 2);
}

// Combined waveforms:
// By combining waveforms the output bits of each waveform are
// effectively short circuited. A zero bit in one waveform will draw
// the corresponding bit in the other waveform(s) to zero (thus the
// infamous claim that the waveforms are AND'ed).
// However, zero bits will also affect other bits since each waveform
// is actually connected via transistors to a register holding the upper 12
// bits of the accumulator.
//
// Example:
// Triangle is basically sawtooth left-shifted. This means that e.g.
// triangle bit 3 is connected to sawtooth bit 2 via transistors
// (think of this connection as a resistor). By short-circuiting
// triangle bit 3 with sawtooth bit 3 in the figure below, triangle
// bit 3 will be drawn to zero, and in this case there is enough power
// left to draw bit 4 to zero as well (note that all bits are connected!).
// 
//             1 1
//             1 0 9 8 7 6 5 4 3 2 1 0
//             -----------------------
// Sawtooth    0 0 0 1 1 1 1 1 1 0 0 0
//              / / / / / / / / / / /
//             / / / / / / / / / / /
// Triangle    0 0 1 1 1 1 1 1 0 0 0 0
//
// AND         0 0 0 1 1 1 1 1 0 0 0 0
//
// Output      0 0 0 0 1 1 1 0 0 0 0 0
//
//
// This behavior would be quite difficult to model exactly, since the SID
// in this case does not really act as a digital state machine.
// Tests show that minor (1 bit)  differences can actually occur in the
// output from otherwise identical samples from OSC3 when waveforms are
// combined.
//
// It is probably possible to come up with a valid model for the
// behavior, however this would be far too slow for practical use since it
// would have to be based on the mutual influence of individual bits.
//
// The output is instead approximated by using the upper bits of the
// accumulator as an index to look up the combined output in a table
// containing actual combined waveform samples from OSC3.
// These samples are 8 bit, so we lose the lower 4 bits of waveform output.
//
// Experiments show that the MSB of the accumulator and its effect of
// negating accumulator bits for triangle output has no effect on
// combined waveforms including triangle. This is fortunate since it allows
// direct table lookup without having to consider ring modulation.
//
// Pulse+Sawtooth:
// The upper 12 bits of the accumulator is used to look up an OSC3
// sample. This sample is output if pulse output is on.
// OSC3 samples are taken with FREQ=0x1000;
//
// Sawtooth+Triangle:
// The accumulator is left-shifted, and the resulting upper 12 bits of the
// accumulator is used to look up an OSC3 sample.
// OSC3 samples are taken with FREQ=0x0800;
// 
// Pulse+Triangle, Pulse+Sawtooth+Triangle:
// The accumulator is left-shifted, and the resulting upper 12 bits of the
// accumulator is used to look up an OSC3 sample. This sample is output if
// pulse output is on, otherwise zero is output.
// OSC3 samples are taken with FREQ=0x0800;
// 
reg12 WaveformGenerator::output_PS_()
{
  return (sample_PS_[accumulator >> 12] << 4) & output_P__();
}

reg12 WaveformGenerator::output__ST()
{
  return sample__ST[(accumulator >> 11) & 0xfff] << 4;
}

reg12 WaveformGenerator::output_P_T()
{
  return (sample_P_T[(accumulator >> 11) & 0xfff] << 4) & output_P__();
}

reg12 WaveformGenerator::output_PST()
{
  return (sample_PST[(accumulator >> 11) & 0xfff] << 4) & output_P__();
}

// Combined waveforms including noise:
// All waveform combinations including noise output zero after a few cycles.
// NB! The effects such combinations are not fully explored. It is claimed
// that the shift register may be filled with zeroes and locked up, which
// seems to be true.
// We have not attempted to model this behavior, suffice to say that
// there is very little audible output from waveform combinations including
// noise. We hope that nobody is actually using it.
//
reg12 WaveformGenerator::outputN__T()
{
  return 0;
}

reg12 WaveformGenerator::outputN_S_()
{
  return 0;
}

reg12 WaveformGenerator::outputN_ST()
{
  return 0;
}

reg12 WaveformGenerator::outputNP__()
{
  return 0;
}

reg12 WaveformGenerator::outputNP_T()
{
  return 0;
}

reg12 WaveformGenerator::outputNPS_()
{
  return 0;
}

reg12 WaveformGenerator::outputNPST()
{
  return 0;
}


// Array of functions to return waveform output.
//
WaveformGenerator::OutputFunction WaveformGenerator::output_function[] =
{
  &WaveformGenerator::output____,
  &WaveformGenerator::output___T,
  &WaveformGenerator::output__S_,
  &WaveformGenerator::output__ST,
  &WaveformGenerator::output_P__,
  &WaveformGenerator::output_P_T,
  &WaveformGenerator::output_PS_,
  &WaveformGenerator::output_PST,
  &WaveformGenerator::outputN___,
  &WaveformGenerator::outputN__T,
  &WaveformGenerator::outputN_S_,
  &WaveformGenerator::outputN_ST,
  &WaveformGenerator::outputNP__,
  &WaveformGenerator::outputNP_T,
  &WaveformGenerator::outputNPS_,
  &WaveformGenerator::outputNPST
};
