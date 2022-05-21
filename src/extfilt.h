//  ---------------------------------------------------------------------------
//  This file is part of reSID, a MOS6581 SID emulator engine.
//  Copyright (C) 1998 - 2022  Dag Lem <resid@nimrod.no>
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

#ifndef RESID_EXTFILT_H
#define RESID_EXTFILT_H

#include "siddefs.h"
#include <cmath>

namespace reSID
{

/* ----------------------------------------------------------------------------

The audio output stage in a Commodore 64 consists of two first-order RC
filters, a low-pass filter with 3-dB frequency 16kHz followed by a high-pass
filter with 3-dB frequency 1.6Hz (the latter assuming an audio equipment
input impedance of 10kOhm).
The RC filters are connected with a BJT emitter follower, which for
simplicity is modeled as a unity gain buffer.

C64 audio output stage - component designators from schematic #250469:

                                       9/12V
                                        |
                 10k                    |
AUDIO OUT ---+---R9---+-------+-------|< Q3
             |        |       |         |
             R8 1k   C74 1n   +---C76---+---C77--- AUD OUT
             |        |           470p  |   10u
            GND      GND               R12 1k
                                        |
                                       GND

R8 is not populated for 8580.

Since a high-pass cutoff frequency of only 1.6Hz yields an audio signal
which rarely settles around zero, and since the number of state bits
required increases with decreasing cutoff frequency, we rather assume
a low but not entirely unreasonable Rload of 1kOhm, yielding a high-pass
cutoff frequency of 16Hz.

With w0 = 1/RC, a state space model can be derived as follows:

  (vi(t) - vlp(t))/R9 = C74*dvlp(t)/dt
  vo(t)/Rload = C77*dvhp(t)/dt
  vo(t) = vlp(t) - vhp(t)

  dvlp(t)/dt = -w0lp*vlp(t) + 0           + w0lp*vi(t)
  dvhp(t)/dt =  w0hp*vlp(t) - w0hp*vhp(t) + 0
  vo(t)      =  vlp(t)      - vhp(t)      + 0

I.e. using standard state space model notation:

  A = [ -w0lp,   0    ]
      [  w0hp,  -w0hp ]

  B = [  w0lp ]
      [  0    ]

  C = [  1,  -1 ]

  D = [ 0 ]

In earlier versions of reSID, this model was applied directly using
forward differencing (Euler method).

The model is more accurately discretized using zero order hold, see e.g.
https://en.wikipedia.org/wiki/Discretization#Discretization_of_linear_state_space_models
https://studywolf.wordpress.com/tag/zero-order-hold/

  Ad = e^(A*T) = L^-1((sI - A)^-1)
  Bd = A^-1*(Ad - I)*B
  Cd = C
  Dd = D

This yields

  Ad = [ e^(-w0lp*T)                                      ,  0           ]
       [ (-w0hp/(w0lp - w0hp))*(e^(-w0lp*T) - e^(-w0hp*T)),  e^(-w0hp*T) ]

  Bd = [ 1 - e^(-w0lp*T)                                                    ]
       [ (w0lp/(w0lp - w0hp))*(e^(-w0lp*T) - e^(-w0hp*T)) - e^(-w0lp*T) + 1 ]

  Cd = [  1  -1 ]

  Dd = [ 0 ]

As it turns out, the coefficients for the high-pass state differ by more than
five orders of magnitude, i.e. in the order of 17 bits. This would be
detrimental to accuracy, however we can use a small trick - instead of
calculating new state values directly, we can calculate differences, i.e. by
using Ad_delta = Ad - I. This brings the factors into the same ballpark, and
can also save us one multiply since Ad_delta_11 = -Bd_11.

We now have

  Ad_delta = [ e^(-w0lp*T) - 1                                  ,  0               ]
             [ (-w0hp/(w0lp - w0hp))*(e^(-w0lp*T) - e^(-w0hp*T)),  e^(-w0hp*T) - 1 ]

  Bd = [ 1 - e^(-w0lp*T)                                                    ]
       [ (w0lp/(w0lp - w0hp))*(e^(-w0lp*T) - e^(-w0hp*T)) - e^(-w0lp*T) + 1 ]

  Cd = [  1  -1 ]

  Dd = [ 0 ]

However comparing this model with a simpler model, consisting of two cascaded
zero order hold models, shows extremely little difference in practice.
The latter model can save us two more multiplies, since
Ad_delta_21 = -Ad_delta22, and Bd_21 = 0:

  Ad = [ e^(-w0lp*T)    ,  0           ]
       [ 1 - e^(-w0hp*T),  e^(-w0hp*T) ]

  Ad_delta = [ e^(-w0lp*T) - 1,  0               ]
             [ 1 - e^(-w0hp*T),  e^(-w0hp*T) - 1 ]

  Bd = [ 1 - e^(-w0lp*T) ]
       [ 0               ]

  Cd = [  1  -1 ]

  Dd = [ 0 ]

We are now in essence back to the simple model used in earlier versions of
reSID. However the coefficients are now more accurately calculated, which is
especially noticeable for multi-cycle time periods.

-----------------------------------------------------------------------------*/

class ExternalFilterCoefficients
{
public:
  int shiftlp, shifthp;
  int mullp, mulhp;

  RESID_CONSTEVAL ExternalFilterCoefficients(double w0lp, double w0hp, double T) :
    // Cutoff frequency accuracy (4 bits) is traded off for filter state
    // accuracy (27 bits). This is crucial since w0lp and w0hp are so far apart.
    shiftlp( log2(((1 << 4) - 1.0)/(1.0 - exp(-w0lp*T))) ),
    shifthp( log2(((1 << 4) - 1.0)/(1.0 - exp(-w0hp*T))) ),
    mullp( (1.0 - exp(-w0lp*T))*(1 << shiftlp) + 0.5 ),
    mulhp( (1.0 - exp(-w0hp*T))*(1 << shifthp) + 0.5 )
  {}
};

class ExternalFilter
{
public:
  ExternalFilter();

  void enable_filter(bool enable);

  void clock(short vi);
  void clock(cycle_count delta_t, short vi);
  void reset();

  // Audio output (16 bits).
  short output();

protected:
  // Filter enabled.
  bool enabled;

  // Filter coefficients.
  // w0lp = 1/(R8*C74) = 1/(1e3*1e-9)     = 100000
  // w0hp = 1/(Rload*C77) = 1/(1e3*10e-6) =    100
  static constexpr double w0lp = 1.0/(10e3*1e-9);
  static constexpr double w0hp = 1.0/(1e3*10e-6);
  // Assume a 1MHz clock.
  static constexpr double T = 1.0/1e6;
  static constexpr cycle_count MAX_CYCLES = 10;
  // Filter parameters for delta_t = 1 and delta_t > 1.
  RESID_CONSTEXPR ExternalFilterCoefficients t1 =
    ExternalFilterCoefficients(w0lp, w0hp, T);
  RESID_CONSTEXPR ExternalFilterCoefficients tmax =
    ExternalFilterCoefficients(w0lp, w0hp, MAX_CYCLES*T);

  // Filter states (27 bits).
  int vlp, vhp;

friend class SID;
};


// ----------------------------------------------------------------------------
// Inline functions.
// The following functions are defined inline because they are called every
// time a sample is calculated.
// ----------------------------------------------------------------------------

#if RESID_INLINING || defined(RESID_EXTFILT_CC)

// ----------------------------------------------------------------------------
// SID clocking - 1 cycle.
// ----------------------------------------------------------------------------
RESID_INLINE
void ExternalFilter::clock(short vi)
{
  // This is handy for testing.
  if (unlikely(!enabled)) {
    vlp = vi << 11;
    vhp = 0;
    return;
  }

  // Calculate filter state.
  // Note calculation order, avoiding temporary variables.
  vhp += t1.mulhp*(vlp - vhp) >> t1.shifthp;
  vlp += t1.mullp*((vi << 11) - vlp) >> t1.shiftlp;
}

// ----------------------------------------------------------------------------
// SID clocking - delta_t cycles.
// ----------------------------------------------------------------------------
RESID_INLINE
void ExternalFilter::clock(cycle_count delta_t, short vi)
{
  // This is handy for testing.
  if (unlikely(!enabled)) {
    vlp = vi << 11;
    vhp = 0;
    return;
  }

  while (delta_t) {
    if (unlikely(delta_t < MAX_CYCLES)) {
      while (delta_t--) {
        clock(vi);
      }
      break;
    }

    // Calculate filter state.
    // Note calculation order, avoiding temporary variables.
    vhp += tmax.mulhp*(vlp - vhp) >> tmax.shifthp;
    vlp += tmax.mullp*((vi << 11) - vlp) >> tmax.shiftlp;

    delta_t -= MAX_CYCLES;
  }
}


// ----------------------------------------------------------------------------
// Audio output (16 bits).
// ----------------------------------------------------------------------------
RESID_INLINE
short ExternalFilter::output()
{
  // Calculate filter output, shifting down from 27 to 16 bits.
  return (vlp - vhp) >> 11;
}

#endif // RESID_INLINING || defined(RESID_EXTFILT_CC)

} // namespace reSID

#endif // not RESID_EXTFILT_H
