//  ---------------------------------------------------------------------------
//  This file is part of reSID, a MOS6581 SID emulator engine.
//  Copyright (C) 2010 - 2022  Dag Lem <resid@nimrod.no>
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

#ifndef RESID_DAC_H
#define RESID_DAC_H

#include <cmath>

namespace reSID
{

/* ----------------------------------------------------------------------------

The SID DACs are built up as follows:

         n  n-1      2   1   0    VGND
         |   |       |   |   |      |   Termination
        2R  2R      2R  2R  2R     2R   only for
         |   |       |   |   |      |   MOS 8580
     Vo  --R---R--...--R---R--    ---


All MOS 6581 DACs are missing a termination resistor at bit 0. This causes
pronounced errors for the lower 4 - 5 bits (e.g. the output for bit 0 is
actually equal to the output for bit 1), resulting in DAC discontinuities
for the lower bits.
In addition to this, the 6581 DACs exhibit further severe discontinuities
for higher bits, which may be explained by a less than perfect match between
the R and 2R resistors, or by output impedance in the NMOS transistors
providing the bit voltages. A good approximation of the actual DAC output is
achieved for 2R/R ~ 2.20.

The MOS 8580 DACs, on the other hand, do not exhibit any discontinuities.
These DACs include the correct termination resistor, and also seem to have
very accurately matched R and 2R resistors (2R/R = 2.00).

-----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// Calculation of lookup tables for SID DACs.
// ----------------------------------------------------------------------------
template<int bits> class DAC
{
  using T = unsigned short;

public:
  // FIXME: This constructor is a temporary workaround for filter.cc,
  // which currently depends on dynamic initialization.
  DAC<bits>() {}

  constexpr DAC<bits>(double _2R_div_R, bool term)
  {
    double vbit[bits];

    // Calculate voltage contribution by each individual bit in the R-2R ladder.
    for (int set_bit = 0; set_bit < bits; set_bit++) {
      int bit = 0;

      double Vn = 1.0;          // Normalized bit voltage.
      double R = 1.0;           // Normalized R
      double _2R = _2R_div_R*R; // 2R
      double Rn = term ?        // Rn = 2R for correct termination,
        _2R : INFINITY;         // INFINITY for missing termination.

      // Calculate DAC "tail" resistance by repeated parallel substitution.
      for (; bit < set_bit; bit++) {
        if (Rn == INFINITY) {
          Rn = R + _2R;
        }
        else {
          Rn = R + _2R*Rn/(_2R + Rn); // R + 2R || Rn
        }
      }

      // Source transformation for bit voltage.
      if (Rn == INFINITY) {
        Rn = _2R;
      }
      else {
        Rn = _2R*Rn/(_2R + Rn);  // 2R || Rn
        Vn = Vn*Rn/_2R;
      }

      // Calculate DAC output voltage by repeated source transformation from
      // the "tail".
      for (++bit; bit < bits; bit++) {
        Rn += R;
        double I = Vn/Rn;
        Rn = _2R*Rn/(_2R + Rn);  // 2R || Rn
        Vn = Rn*I;
      }

      vbit[set_bit] = Vn;
      // Single bit values, scaled by 2^4.
      dac_bits[set_bit] = ((1 << bits) - 1)*Vn*(1 << 4) + 0.5;
    }

    // Calculate the voltage for any combination of bits by superpositioning.
    for (int i = 0; i < (1 << bits); i++) {
      int x = i;
      double Vo = 0;
      for (int j = 0; j < bits; j++) {
        Vo += (x & 0x1)*vbit[j];
        x >>= 1;
      }

      // Scale maximum output to 2^bits - 1.
      dac_table[i] = (T)(((1 << bits) - 1)*Vo + 0.5);
    }
  }

  // FIXME: This operator is a temporary workaround for filter.cc,
  // which currently depends on dynamic initialization.
  T& operator[](std::size_t pos)
  {
    return dac_table[pos];
  }

  // Read value from DAC lookup table.
  constexpr const T& operator[](std::size_t pos) const
  {
    return dac_table[pos];
  }

  // Calculate DAC value by bit superpositioning, as a template for
  // FPGA implementations.
  T operator()(T val) const
  {
    T bitsum = 0;
    for (int bit = 0; bit < bits; bit++) {
        bitsum += (val & 0x1) ? dac_bits[bit] : 0;
        val >>= 1;
    }

    return (T)((bitsum + (1 << 3)) >> 4);
  }

  // Bit values, scaled by 2^4.
  // Kept public for FPGA implementors.
  T dac_bits[bits];
private:
  // Lookup table, more suitable for CPU implementations.
  T dac_table[1 << bits];
};

} // namespace reSID

#endif // not RESID_DAC_H
