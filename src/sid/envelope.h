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

// ----------------------------------------------------------------------------
// A 15 bit counter is used to implement the envelope rates, in effect
// dividing the clock to the envelope counter by the currently selected rate
// period.
// In addition, another counter is used to implement the exponential envelope
// decay, in effect further dividing the clock to the envelope counter.
// The period of this counter is successively set to 1, 2, 4, 8, 16, 30 at
// the envelope counter values 93, 54, 26, 14, 6.
// ----------------------------------------------------------------------------
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
  reg8 step_envelope(reg8 delta_envelope_max,
		     reg8 rate_period_index,
		     reg8 exponential_period_index,
		     cycle_count& delta_t);

  reg16 rate_counter;
  reg16 exponential_counter;
  reg8 envelope_counter;

  reg4 attack;
  reg4 decay;
  reg4 sustain;
  reg4 release;

  bool gate;

  enum { ATTACK, DECAY_SUSTAIN, RELEASE } state;

  // Lookup table to convert from attack, decay, or release value to rate
  // counter period.
  static reg16 rate_counter_period[];

  // The 5 levels at which the exponential counter period is changed.
  static reg8 exponential_counter_level[];

  // Lookup table to directly, from the envelope counter, find the line
  // segment number of the approximation of an exponential.
  static reg8 exponential_counter_segment[];

  // Table to convert from line segment number to actual counter period.
  static reg8 exponential_counter_period[];

  // The 16 selectable sustain levels.
  static reg8 sustain_level[];
  
friend class Voice;
friend class SID;
};


// ----------------------------------------------------------------------------
// Inline functions.
// The following functions are defined inline because they are called every
// time a sample is calculated.
// ----------------------------------------------------------------------------

#if RESID_INLINE || defined(__ENVELOPE_CC__)

// ----------------------------------------------------------------------------
// Step the envelope counter a maximum of delta_envelope_max steps,
// limited by delta_t.
// If delta_envelope_max is zero, the rate and exponential counters keep
// counting without stepping the envelope counter.
// The rate counter counts up to its current comparison value, at which point
// the counter is zeroed. The exponential counter has the same behavior.
// ----------------------------------------------------------------------------
#if RESID_INLINE
inline
#endif
reg8 EnvelopeGenerator::step_envelope(reg8 delta_envelope_max,
				      reg8 rate_period_index,
				      reg8 exponential_period_index,
				      cycle_count& delta_t)
{
  // Fetch the rate divider period.
  reg16 rate_period = rate_counter_period[rate_period_index];
  
  // Fetch the exponential divider period.
  reg16 exponential_period =
    exponential_counter_period[exponential_period_index];

  // Check for ADSR delay bug.
  // If the rate counter comparison value is set below the current value of the
  // rate counter, the counter will continue counting up, wrap to zero at
  // 2^15 = 0x8000, and finally reach the comparison value.
  // This has been verified by sampling ENV3.
  // We assume that the comparison value is actually period - 1.
  int rate_step = rate_counter < rate_period ?
    rate_period - rate_counter : 0x8000 + rate_period - rate_counter;

  reg8 delta_envelope = 0;

  while (delta_t) {
    if (delta_t < rate_step) {
      rate_counter += delta_t;
      rate_counter &= 0x7fff;
      delta_t = 0;
      return delta_envelope;
    }

    rate_counter = 0;
    delta_t -= rate_step;
    rate_step = rate_period;

    // There is no delay bug for the exponential counter since it is reset
    // whenever the gate bit is flipped.
    if (++exponential_counter == exponential_period) {
      exponential_counter = 0;
      if (delta_envelope_max) {
	if (++delta_envelope == delta_envelope_max) {
	  return delta_envelope;
	}
      }
    }
  }

  return delta_envelope;
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
    reg8 delta_envelope =
      step_envelope(0xff - envelope_counter, attack, 0, delta_t);
    
    // Add to the envelope counter.
    envelope_counter += delta_envelope;

    if (envelope_counter != 0xff) {
      return;
    }

    state = DECAY_SUSTAIN;
  }

  // In decay/sustain state.
  // We combine these states to ensure that the envelope counter continues
  // decrementing if the sustain level is lowered.
  if (state == DECAY_SUSTAIN) {
    while (delta_t) {
      // Find the line segment number of the approximation of an exponential
      // from a lookup table.
      reg8 segment = exponential_counter_segment[envelope_counter];

      // The start of the next line segment of the linear approximation of
      // the exponential is found from another lookup table.
      reg8 min_level = exponential_counter_level[segment];

      // Check for sustain level.
      if (min_level < sustain_level[sustain]) {
	min_level = sustain_level[sustain];
      }

      // Check whether the current sustain level is reached.
      // If the sustain level is raised above the current envelope counter
      // value the new sustain level is zero.
      // Use int instead of reg8 because of signed result.
      int delta_envelope_max = envelope_counter - min_level;
      if (delta_envelope_max < 0) {
	delta_envelope_max = envelope_counter;
      }

      reg8 delta_envelope =
	step_envelope(delta_envelope_max, decay, segment, delta_t);

      // Subtract from the envelope counter.
      envelope_counter -= delta_envelope;
    }
  }

  // In release state.
  // Identical to the decay/sustain state except for no sustain level check.
  // if (state == RELEASE) {
  else {
    while (delta_t) {
      reg8 segment = exponential_counter_segment[envelope_counter];
      reg8 min_level = exponential_counter_level[segment];

      reg8 delta_envelope_max = envelope_counter - min_level;

      reg8 delta_envelope =
	step_envelope(delta_envelope_max, release, segment, delta_t);

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
