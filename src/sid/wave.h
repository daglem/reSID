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

#ifndef __WAVE_H__
#define __WAVE_H__

#include "siddefs.h"

// ----------------------------------------------------------------------------
// A 24 bit accumulator is the basis for waveform generation. FREQ is added to
// the lower 16 bits of the accumulator each cycle.
// The accumulator is set to zero when TEST is set, and starts counting
// when TEST is cleared.
// The noise waveform is taken from intermediate bits of a 23 bit shift
// register. This register is clocked by bit 19 of the accumulator.
// ----------------------------------------------------------------------------
class WaveformGenerator
{
public:
  // Constructor parameter is sync_source.
  WaveformGenerator(WaveformGenerator*);
  void writeFREQ_LO(reg8);
  void writeFREQ_HI(reg8);
  void writePW_LO(reg8);
  void writePW_HI(reg8);
  reg8 readOSC();

  // 12-bit waveform output.
  reg12 output();
private:
  void writeCONTROL_REG(reg8);

  void clock(cycle_count delta_t);
  void synchronize();
  void reset();

  const WaveformGenerator* sync_source;
  const WaveformGenerator* sync_dest;

  // Tell whether the accumulator MSB was set high on this cycle.
  bool msb_rising;

  reg24 accumulator;
  reg24 shift_register;

  // Fout  = (Fn*Fclk/16777216)Hz
  reg16 freq;
  // PWout = (PWn/40.95)%
  reg12 pw;

  // The control register right-shifted 4 bits; used for output function
  // table lookup.
  reg8 waveform;

  // The remaining control register bits.
  bool test;
  bool ring_mod;
  bool sync;
  // The gate bit is handled by the EnvelopeGenerator.

  // 16 possible combinations of waveforms.
  reg12 output____();
  reg12 output___T();
  reg12 output__S_();
  reg12 output__ST();
  reg12 output_P__();
  reg12 output_P_T();
  reg12 output_PS_();
  reg12 output_PST();
  reg12 outputN___();
  reg12 outputN__T();
  reg12 outputN_S_();
  reg12 outputN_ST();
  reg12 outputNP__();
  reg12 outputNP_T();
  reg12 outputNPS_();
  reg12 outputNPST();

  // Array of member functions to return waveform output.
  typedef reg12 (WaveformGenerator::*OutputFunction)();
  static OutputFunction output_function[];

  // Sample data for combinations of waveforms.
  static reg8 sample__ST[];
  static reg8 sample_P_T[];
  static reg8 sample_PS_[];
  static reg8 sample_PST[];

friend class Voice;
friend class SID;
};


// ----------------------------------------------------------------------------
// Inline functions.
// The following functions are defined inline because they are called every
// time a sample is calculated.
// ----------------------------------------------------------------------------

#if RESID_INLINE || defined(__WAVE_CC__)

// ----------------------------------------------------------------------------
// SID clocking.
// ----------------------------------------------------------------------------
#if RESID_INLINE
inline
#endif
void WaveformGenerator::clock(cycle_count delta_t)
{
  // No operation if test bit is set.
  if (test) {
    return;
  }

  // Calculate value to add to accumulator.
  reg24 delta_accumulator = delta_t*freq;

  // Calculate new accumulator value;
  reg24 accumulator_next = accumulator + delta_accumulator;

  // Shift noise register once for each time accumulator bit 19 is set high.
  // Bit 19 is set high each time 2^20 (0x100000) is added to the accumulator.
  reg24 shift_period = 0x100000;
  reg24 shifts = delta_accumulator/shift_period;
  reg24 accumulator_prev = (accumulator + shift_period*shifts);

  // Determine whether bit 19 is set after the 2^20 multiple.
  if (!(accumulator_prev & 0x080000) && (accumulator_next & 0x080000)) {
    shifts++;
  }

  // Shift the noise/random register.
  // NB! The shift is actually delayed 2 cycles, this is not modeled.
  for (reg24 i = 0; i < shifts; i++) {
    reg24 bit0 = ((shift_register >> 22) ^ (shift_register >> 17)) & 0x1;
    shift_register <<= 1;
    shift_register &= 0x7fffff;
    shift_register |= bit0;
  }

  // Check whether the msb is set high. This is used for synchronization.
  msb_rising = !(accumulator & 0x800000) && (accumulator_next & 0x800000);

  // Set new accumulator value.
  accumulator = accumulator_next & 0xffffff;
}


// ----------------------------------------------------------------------------
// Synchronize oscillators.
// This must be done after all the oscillators have been clock()'ed since the
// oscillators operate in parallel.
// Note that the oscillators must be clocked exactly on the cycle when the
// msb is set high for hard sync and ring modulation to operate correctly.
// See SID::clock().
// ----------------------------------------------------------------------------
#if RESID_INLINE
inline
#endif
void WaveformGenerator::synchronize()
{
  if (sync && sync_source->msb_rising) {
    accumulator = 0;
  }
}


// ----------------------------------------------------------------------------
// Select one of 16 possible combinations of waveforms.
// ----------------------------------------------------------------------------
#if RESID_INLINE
inline
#endif
reg12 WaveformGenerator::output()
{
  return (this->*output_function[waveform])();
}

#endif // RESID_INLINE || defined(__WAVE_CC__)

#endif // not __WAVE_H__
