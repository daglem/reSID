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

#define __ENVELOPE_CC__
#include "envelope.h"

// ----------------------------------------------------------------------------
// Constructor.
// ----------------------------------------------------------------------------
EnvelopeGenerator::EnvelopeGenerator()
{
  reset();
}

// ----------------------------------------------------------------------------
// SID reset.
// ----------------------------------------------------------------------------
void EnvelopeGenerator::reset()
{
  envelope_counter = 0;

  attack = 0;
  decay = 0;
  sustain = 0;
  release = 0;

  gate = false;

  frequency_divider_counter = frequency_divider_number[release];
  delta_t_remainder = 0;

  state = RELEASE;
}


// Frequency divider numbers are calculated from the Envelope Rates table in
// the Programmer's Reference Guide. The rates have been verified by
// sampling ENV3. The frequency divider number is the number of cycles between
// each increment of the envelope counter.
//
// The frequency divider loads a number into a 16-bit counter and decrements
// this counter each cycle. When the counter reaches zero the 8-bit envelope
// counter is incremented (attack) or decremented (decay/release) and the
// frequency divider number is loaded into the 16-bit counter again.
//
// NB! Sampling ENV3 indicates that the calculated values are not exact.
// It may seem like 1 is added to the calculated values. A possible explanation
// for this is that the SID designers have used the calculated values directly
// to feed the frequency divider, not considering that if a register is loaded
// with a number and decremented down to zero before the register is reloaded,
// the period is actually the frequency divider number + 1.

// The exact frequency divider numbers are yet to be verified because
// we have been making a full sample from 8 cycle shifted samples, and
// the envelope counter is sometimes incremented once almost immediately after
// the gate bit is set instead of after the number of cycles indicated by the
// frequency divider number. This is explained by that if the frequency divider
// has counted down to zero on the same cycle that the gate bit is set, the
// envelope will count up to one immediately.
//
// We would need a REU (Ram Expansion Unit) DMA to sample ENV3 every cycle
// or find a way to avoid this behavior to determine the exact values.
// We are using the calculated values even if e.g. the frequency divider
// number for attack is verified to be 9, not 8, until someone volunteers to
// set his REU to work to count e.g. the number of consequtive 1's for each
// attack rate.
//
// NB! Sampling of ENV3 has to be done using sustain = release = 0.
// Using other values for e.g. release may yield constant 0 from ENV3.
// This is not modeled.
//
// Note that this behavior fortunately has no effect on the internal envelope
// counter (the audio output is not silenced), it is just the ENV3 register
// that does not reflect the counter correctly.
//
reg16 EnvelopeGenerator::frequency_divider_number[] = {
      8,  //   2ms*1.0MHz/256 =     7.81
     31,  //   8ms*1.0MHz/256 =    31.25
     63,  //  16ms*1.0MHz/256 =    62.50
     94,  //  24ms*1.0MHz/256 =    93.75
    148,  //  38ms*1.0MHz/256 =   148.44
    219,  //  56ms*1.0MHz/256 =   218.75
    266,  //  68ms*1.0MHz/256 =   265.63
    313,  //  80ms*1.0MHz/256 =   312.50
    391,  // 100ms*1.0MHz/256 =   390.63
    977,  // 250ms*1.0MHz/256 =   976.56
   1953,  // 500ms*1.0MHz/256 =  1953.13
   3125,  // 800ms*1.0MHz/256 =  3125.00
   3906,  //   1 s*1.0MHz/256 =  3906.25
  11719,  //   3 s*1.0MHz/256 = 11718.75
  19531,  //   5 s*1.0MHz/256 = 19531.25
  31250   //   8 s*1.0MHz/256 = 31250.00
};

// For decay and release, the clock to the envelope generator is sequentially
// divided by two to create a piece-wise linear approximation of an
// exponential at the following envelope counter values: 93, 54, 26, 14, 6
//
reg8 EnvelopeGenerator::exponential_divide_level[] = {
  0x5d,
  0x36,
  0x1a,
  0x0e,
  0x06,
  0x00
};

// Lookup table to directly, from the envelope counter, find the number of
// bits to right-shift the clock to the frequency divider.
reg8 EnvelopeGenerator::exponential_shift[] = {
  /* 0x00: */  5, 5, 5, 5, 5, 5, 5, 4,  // 0x06
  /* 0x08: */  4, 4, 4, 4, 4, 4, 4, 3,  // 0x0e
  /* 0x10: */  3, 3, 3, 3, 3, 3, 3, 3,
  /* 0x18: */  3, 3, 3, 2, 2, 2, 2, 2,  // 0x1a
  /* 0x20: */  2, 2, 2, 2, 2, 2, 2, 2,
  /* 0x28: */  2, 2, 2, 2, 2, 2, 2, 2,
  /* 0x30: */  2, 2, 2, 2, 2, 2, 2, 1,  // 0x36
  /* 0x38: */  1, 1, 1, 1, 1, 1, 1, 1,
  /* 0x40: */  1, 1, 1, 1, 1, 1, 1, 1,
  /* 0x48: */  1, 1, 1, 1, 1, 1, 1, 1,
  /* 0x50: */  1, 1, 1, 1, 1, 1, 1, 1,
  /* 0x58: */  1, 1, 1, 1, 1, 1, 0, 0,  // 0x5d
  /* 0x60: */  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0x68: */  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0x70: */  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0x78: */  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0x80: */  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0x88: */  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0x90: */  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0x98: */  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0xa0: */  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0xa8: */  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0xb0: */  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0xb8: */  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0xc0: */  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0xc8: */  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0xd0: */  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0xd8: */  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0xe0: */  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0xe8: */  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0xf0: */  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0xf8: */  0, 0, 0, 0, 0, 0, 0, 0
};

// From the sustain levels it follows that both the low and high 4 bits of the
// envelope counter are compared to the 4-bit sustain value.
//
reg8 EnvelopeGenerator::sustain_level[] = {
  0x00,
  0x11,
  0x22,
  0x33,
  0x44,
  0x55,
  0x66,
  0x77,
  0x88,
  0x99,
  0xaa,
  0xbb,
  0xcc,
  0xdd,
  0xee,
  0xff,
};


// ----------------------------------------------------------------------------
// Register functions.
// ----------------------------------------------------------------------------
void EnvelopeGenerator::writeCONTROL_REG(reg8 control)
{
  bool gate_next = control & 0x01;

  // Gate bit on: Start attack, decay, sustain.
  if (!gate && gate_next) {
    frequency_divider_counter = frequency_divider_number[attack];
    delta_t_remainder = 0;
    state = ATTACK;
  }
  // Gate bit off: Start release.
  else if (gate && !gate_next) {
    frequency_divider_counter = frequency_divider_number[release];
    delta_t_remainder = 0;
    state = RELEASE;
  }

  gate = gate_next;
}

void EnvelopeGenerator::writeATTACK_DECAY(reg8 attack_decay)
{
  attack = (attack_decay >> 4) & 0x0f;
  decay = attack_decay & 0x0f;
}

void EnvelopeGenerator::writeSUSTAIN_RELEASE(reg8 sustain_release)
{
  sustain = (sustain_release >> 4) & 0x0f;
  release = sustain_release & 0x0f;
}

reg8 EnvelopeGenerator::readENV()
{
  return output();
}
