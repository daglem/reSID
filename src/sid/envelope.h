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

#ifndef __ENVELOPE_H__
#define __ENVELOPE_H__

#include "siddefs.h"

class EnvelopeGenerator
{
public:
  EnvelopeGenerator();
  void writeCONTROL_REG(reg8);
  void writeATTACK_DECAY(reg8);
  void writeSUSTAIN_RELEASE(reg8);
  reg8 readENV();

  // 8-bit envelope output.
  reg8 output();
private:
  void reset();
  void clock(cycle_count delta_t);
  reg16 step_envelope(reg16 delta_envelope_max,
		      reg8 divider_index,
		      reg8 shifts,
		      cycle_count& delta_t);

  reg16 frequency_divider_counter;
  reg8 envelope_counter;

  // Unused clocks to the frequency divider.
  cycle_count delta_t_remainder;

  reg4 attack;
  reg4 decay;
  reg4 sustain;
  reg4 release;

  bool gate;

  enum { ATTACK, DECAY_SUSTAIN, RELEASE } state;

  // Frequency divider numbers.
  static reg16 frequency_divider_number[];

  // The 5 levels at which the clock to the envelope generator is
  // sequentially divided by two.
  static reg8 exponential_divide_level[];

  // Lookup table to directly, from the envelope counter, find the number of
  // bits to right-shift the clock to the frequency divider.
  static reg8 exponential_shift[];

  // The 16 selectable sustain levels.
  static reg8 sustain_level[];
  
friend class Voice;
friend class SID;
};


// ----------------------------------------------------------------------------
// Inline functions.
// The following functions may be defined inline because they are called every
// time a sample is calculated.
// ----------------------------------------------------------------------------

#if RESID_INLINE || defined(__ENVELOPE_CC__)

// ----------------------------------------------------------------------------
// Step the envelope counter a maximum of delta_envelope_max steps,
// limited by delta_t.
// If delta_envelope_max is zero, the frequency divider counter keeps counting
// without stepping the envelope counter.
// The frequency divider is modeled using a frequency divider counter to
// ensure that things work correctly even if the divider changes during ADS
// or R.
// The counter is counted down to zero at which point the the divider is
// reloaded with the current divider number.
// delta_envelope_max cannot be 8 bit since the maximum value is 256.
// This also holds for the returned value.
// ----------------------------------------------------------------------------
#if RESID_INLINE
inline
#endif
reg16 EnvelopeGenerator::step_envelope(reg16 delta_envelope_max,
				       reg8 divider_index,
				       reg8 shifts,
				       cycle_count& delta_t)
{
  // Divide the clock to the frequency counter with a multiple of 2^n.
  // Add the remaining unused cycles from last time.
  cycle_count delta_t_total = delta_t + delta_t_remainder;
  cycle_count delta_t_divided = delta_t_total >> shifts;

  // Calculate remaining unused cycles.
  cycle_count remainder_mask = 0x1f >> (5 - shifts);
  delta_t_remainder = delta_t_total & remainder_mask;

  // Check whether delta_t_divided is large enough to step the envelope.
  if (delta_t_divided < static_cast<cycle_count>(frequency_divider_counter)) {
    frequency_divider_counter -= delta_t_divided;
    delta_t = 0;
    return 0;
  }

  // First step completed.
  delta_t_divided -= frequency_divider_counter;

  // Fetch the frequency divider number (the period of the divider).
  reg16 divider = frequency_divider_number[divider_index];
  
  // Envelope step allowed (not at sustain or zero level).
  if (delta_envelope_max) {
    // The number of times to clock the divider.
    cycle_count delta_t_min = delta_t_divided;

    // The number of clocks needed to increment the envelope counter
    // delta_envelope_max - 1  times. The first step is already completed.
    cycle_count delta_t_next = divider*(delta_envelope_max - 1);

    // Step no more than delta_envelope_max steps.
    if (delta_t_next < delta_t_min) {
      delta_t_min = delta_t_next;
    }

    // Subtract a multiple of 2^n cycles from delta_t.
    delta_t = (delta_t_divided - delta_t_min) << shifts;

    // Calculate new counter value.
    frequency_divider_counter = divider - delta_t_min%divider;

    // The first step is already completed, so 1 is added to the number
    // of steps.
    return (1 + delta_t_min/divider);
  }
  // No envelope step allowed, still clock the frequency divider.
  else {
    // All available cycles are used.
    delta_t = 0;

    // Calculate new counter value.
    frequency_divider_counter = divider - delta_t_divided%divider;

    return 0;
  }
}


// ----------------------------------------------------------------------------
// SID clocking.
// ----------------------------------------------------------------------------
#if RESID_INLINE
inline
#endif
void EnvelopeGenerator::clock(cycle_count delta_t)
{
  // In attack state.
  if (state == ATTACK) {
    reg16 delta_envelope =
      step_envelope(0x100 - envelope_counter, attack, 0, delta_t);
    
    // Add to the envelope counter.
    if (envelope_counter + delta_envelope == 0x100) {
      envelope_counter = 0xff;
      state = DECAY_SUSTAIN;
    }
    else {
      envelope_counter += delta_envelope;
      return;
    }
  }

  // In decay/sustain state.
  // We combine these states to ensure that the envelope counter continues
  // decrementing if the sustain level is lowered.
  if (state == DECAY_SUSTAIN) {
    while (delta_t) {
      // The clock to the envelope generator is sequentially divided by two at
      // 5 specific envelope counter levels. We find the number of bits
      // to right-shift the clock from a lookup table.
      reg8 shifts = exponential_shift[envelope_counter];

      // The start of the next line segment of the linear approximation of
      // the exponential is found from another lookup table.
      reg8 min_level = exponential_divide_level[shifts];

      // Check for sustain level.
      if (min_level < sustain_level[sustain]) {
	min_level = sustain_level[sustain];
      }

      // Check whether the current sustain level is reached.
      reg16 delta_envelope_max;
      if (envelope_counter <= min_level) {
	delta_envelope_max = 0;
      }
      else {
	delta_envelope_max = envelope_counter - min_level;
      }
      
      reg16 delta_envelope =
	step_envelope(delta_envelope_max, decay, shifts, delta_t);

      // Subtract from the envelope counter.
      envelope_counter -= delta_envelope;
    }
  }

  // In release state.
  // Identical to the decay/sustain state except for no sustain level check.
  // if (state == RELEASE) {
  else {
    while (delta_t) {
      reg8 shifts = exponential_shift[envelope_counter];
      reg8 min_level = exponential_divide_level[shifts];

      reg16 delta_envelope_max = envelope_counter - min_level;
      reg16 delta_envelope =
	step_envelope(delta_envelope_max, release, shifts, delta_t);

      // Subtract from the envelope counter.
      envelope_counter -= delta_envelope;
    }
  }
}


// ----------------------------------------------------------------------------
// Read the envelope generator output.
// ----------------------------------------------------------------------------
#if RESID_INLINE
inline
#endif
reg8 EnvelopeGenerator::output()
{
  return envelope_counter;
}

#endif // RESID_INLINE || defined(__ENVELOPE_CC__)

#endif // not __ENVELOPE_H__
