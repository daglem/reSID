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

#include "sid.h"

// ----------------------------------------------------------------------------
// Constructor.
// ----------------------------------------------------------------------------
SID::SID()
  : voice1(&voice3), voice2(&voice1), voice3(&voice2)
{
  voice[0] = &voice1;
  voice[1] = &voice2;
  voice[2] = &voice3;
}

// ----------------------------------------------------------------------------
// SID reset.
// ----------------------------------------------------------------------------
void SID::reset()
{
  voice1.reset();
  voice2.reset();
  voice3.reset();
  filter.reset();
}


// ----------------------------------------------------------------------------
// Read sample of audio output.
// Both 16-bit and n-bit output is provided.
// The output is inverted just like on a Commodore 64. This should not really
// make any audible difference.
// ----------------------------------------------------------------------------
int SID::output()
{
  return -filter.output()/(4095*255*3*15*2/65536);
}

int SID::output(int bits)
{
  return -filter.output()/(4095*255*3*15*2/(2 << bits));
}


// ----------------------------------------------------------------------------
// SID clocking.
// ----------------------------------------------------------------------------
void SID::clock(cycle_count delta_t)
{
  int i;

  if (!delta_t) {
    return;
  }

  // Clock filter.

  cycle_count delta_t_flt;

  // Bypass filter on/off.
  // This is not really part of SID, but is useful for testing.
  // On slow CPU's it may be necessary to bypass the filter to lower the CPU
  // load.
  if (filter.bypass) {
    delta_t_flt = delta_t;
  }
  else {
    // Maximum delta cycles for filter to work satisfactorily under current
    // cutoff frequency and resonance constraints is approximately 8.
    delta_t_flt = 8;
  }

  while (delta_t > 0) {
    if (delta_t < delta_t_flt) {
      delta_t_flt = delta_t;
    }

    // Clock amplitude modulators.
    for (i = 0; i < 3; i++) {
      voice[i]->envelope.clock(delta_t_flt);
    }

    // Clock and synchronize oscillators.
    // Loop until we reach the current cycle.
    cycle_count delta_t_osc = delta_t_flt;
    while (delta_t_osc > 0) {
      cycle_count delta_t_min = delta_t_osc;

      // Find minimum number of cycles to an oscillator accumulator MSB toggle.
      // We have to clock on each MSB on / MSB off for hard sync and ring
      // modulation to operate correctly.
      for (i = 0; i < 3; i++) {
	WaveformGenerator& wave = voice[i]->wave;

	// It is only necessary to clock on the MSB of an oscillator that has
	// freq != 0 and is a sync source.
	if (!(wave.freq && (wave.sync_dest->sync || wave.sync_dest->ring_mod)))
	{
	  continue;
	}

	reg16 freq = wave.freq;
	reg24 accumulator = wave.accumulator;

	// Clock on MSB off if MSB is on, clock on MSB on if MSB is off.
	reg24 delta_accumulator =
	  (accumulator & 0x800000 ? 0x1000000 : 0x800000) - accumulator;

	cycle_count delta_t_next = delta_accumulator/freq;
	if (delta_accumulator%freq) {
	  delta_t_next++;
	}

	if (delta_t_next < delta_t_min) {
	  delta_t_min = delta_t_next;
	}
      }

      // Clock oscillators.
      for (i = 0; i < 3; i++) {
	voice[i]->wave.clock(delta_t_min);
      }

      // Synchronize oscillators.
      for (i = 0; i < 3; i++) {
	voice[i]->wave.synchronize();
      }

      delta_t_osc -= delta_t_min;
    }

    filter.clock(delta_t_flt,
		 voice1.output(), voice2.output(), voice3.output());

    delta_t -= delta_t_flt;
  }
}
