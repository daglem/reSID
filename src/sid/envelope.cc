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

  rate_counter = 0;
  exponential_counter = 0;

  state = RELEASE;
}


// Rate counter periods are calculated from the Envelope Rates table in
// the Programmer's Reference Guide. The rate counter period is the number of
// cycles between each increment of the envelope counter.
// The rates have been verified by sampling ENV3. 
//
// The rate counter is a 15-bit register which is incremented each cycle.
// When the counter reaches a specific comparison value, the envelope counter
// is incremented (attack) or decremented (decay/release) and the
// counter is zeroed.
//
// NB! Sampling ENV3 indicates that the calculated values are not exact.
// It may seem like most calculated values have been rounded (.5 is rounded
// down) and 1 has beed added to the result. A possible explanation for this
// is that the SID designers have used the calculated values directly
// as rate counter comparison values, not considering a one cycle delay to
// zero the counter. This would yield an actual period of comparison value + 1.
//
// The exact rate counter period must be determined using a REU
// (RAM Expansion Unit) DMA to sample ENV3 every cycle. Making a full sample
// from 8 cycle shifted samples is not sufficient for exact values, since
// it is not possible to reset the rate counter. This means that is is not
// possible to exactly control the time of the first count of the envelope
// counter.
//
// NB! To avoid the ADSR delay bug, sampling of ENV3 should be done using
// sustain = release = 0. This ensures that the attack state will not lower
// the current rate counter period.
//
reg16 EnvelopeGenerator::rate_counter_period[] = {
      9,  //   2ms*1.0MHz/256 =     7.81
     32,  //   8ms*1.0MHz/256 =    31.25
     63,  //  16ms*1.0MHz/256 =    62.50
     95,  //  24ms*1.0MHz/256 =    93.75
    149,  //  38ms*1.0MHz/256 =   148.44
    220,  //  56ms*1.0MHz/256 =   218.75
    267,  //  68ms*1.0MHz/256 =   265.63
    313,  //  80ms*1.0MHz/256 =   312.50
    392,  // 100ms*1.0MHz/256 =   390.63
    977,  // 250ms*1.0MHz/256 =   976.56
   1954,  // 500ms*1.0MHz/256 =  1953.13
   3126,  // 800ms*1.0MHz/256 =  3125.00
   3906,  //   1 s*1.0MHz/256 =  3906.25
  11720,  //   3 s*1.0MHz/256 = 11718.75
  19532,  //   5 s*1.0MHz/256 = 19531.25
  31252   //   8 s*1.0MHz/256 = 31250.00
};

// For decay and release, the clock to the envelope counter is sequentially
// divided by 1, 2, 4, 8, 16, 30 to create a piece-wise linear approximation
// of an exponential at the envelope counter values 93, 54, 26, 14, 6.
// This has been verified by sampling ENV3.
//
reg8 EnvelopeGenerator::exponential_counter_level[] = {
  0x5d,
  0x36,
  0x1a,
  0x0e,
  0x06,
  0x00
};

// Lookup table to directly, from the envelope counter, find the line
// segment number of the approximation of an exponential.
//
reg8 EnvelopeGenerator::exponential_counter_segment[] = {
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

// Table to convert from line segment number to actual counter period.
//
reg8 EnvelopeGenerator::exponential_counter_period[] = {
  1,
  2,
  4,
  8,
  16,
  30
};

// From the sustain levels it follows that both the low and high 4 bits of the
// envelope counter are compared to the 4-bit sustain value.
// This has been verified by sampling ENV3.
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

  // Flipping the gate bit resets the exponential counter, however the rate
  // counter is not reset. Thus there will be a delay before the envelope
  // counter starts counting up (attack) or down (release).

  // Gate bit on: Start attack, decay, sustain.
  if (!gate && gate_next) {
    state = ATTACK;
    exponential_counter = 0;
  }
  // Gate bit off: Start release.
  else if (gate && !gate_next) {
    state = RELEASE;
    exponential_counter = 0;
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
