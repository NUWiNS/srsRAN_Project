/*
 *
 * Copyright 2013-2022 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#pragma once

#include "srsgnb/adt/bounded_integer.h"
#include "srsgnb/ran/modulation_scheme.h"

namespace srsgnb {

/// Physical Downlink and Uplink Shared Channel Modulation and Code Scheme Index, parameter \$fI_{MCS}\$f in TS38.214
/// Section 5.1.3.1.
using sch_mcs_index = bounded_integer<uint8_t, 0, 31>;

/// \brief Physical Downlink and Uplink Shared Channel Modulation and Coding Scheme breakdown.
///
/// Represents one row of an MCS table.
struct sch_mcs_description {
  /// Subcarrier modulation scheme.
  modulation_scheme modulation;
  /// Target code rate, expressed as \f$R\times 1024\f$, range (0, ..., 1024).
  float target_code_rate;
  /// \brief Returns the target spectral efficiency, in bits per subcarrier access.
  /// \note The spectral efficiency is given by the target code rate times the number of bits per modulation symbol.
  constexpr float get_spectral_efficiency() const
  {
    return get_bits_per_symbol(modulation) * target_code_rate / 1024.0F;
  }
};

} // namespace srsgnb
