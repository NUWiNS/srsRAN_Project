/*
 *
 * Copyright 2021-2024 Software Radio Systems Limited
 *
 * This file is part of srsRAN.
 *
 * srsRAN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsRAN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#pragma once

#include "neon_helpers.h"
#include "srsran/adt/span.h"
#include "srsran/support/error_handling.h"

namespace srsran {
namespace ofh {
namespace neon {

/// \brief Reads eight 16bit IQ values from input NEON register and packs them to the first 72 bits of the output NEON
/// register in big-endian format, thus occupying 9 output bytes.
///
/// \param[in] regs NEON register storing 16bit IQ samples of a resource block.
/// \return NEON register storing 9 packed bytes.
inline uint8x16_t pack_neon_register_9b_big_endian(int16x8_t reg)
{
  // Input IQ samples need to be shifted in order to align bits before final packing.
  // 0:  i0 0  0  0  0  0  0  0   i8 i7 i6 i5 i4 i3 i2 i1   <- rotate right by 1 (shift left by 7, swap bytes later)
  // 1:  0 q8 q7 q6 q5 q4 q3 q2   q1 q0  0  0  0  0  0  0   <- shift left by 6
  // 2:  0  0 i8 i7 i6 i5 i4 i3   i2 i1 i0  0  0  0  0  0   <- shift left by 5
  // 3:  0  0  0 q8 q7 q6 q5 q4   q3 q2 q1 q0  0  0  0  0   <- shift left by 4
  // 4:  0  0  0  0 i8 i7 i6 i5   i4 i3 i2 i1 i0  0  0  0   <- shift left by 3
  // 5:  0  0  0  0  0 q8 q7 q6   q5 q4 q3 q2 q1 q0  0  0   <- shift left by 2
  // 6:  0  0  0  0  0  0 i8 i7   i6 i5 i4 i3 i2 i1 i0  0   <- shift left by 1
  // 7:  0  0  0  0  0  0  0 q8   q7 q6 q5 q4 q3 q2 q1 q0   <- no shift

  // Shift data according to the mask described above.
  const int16x8_t shift_mask_s16 = vcombine_s16(vcreate_s16(0x0004000500060007), vcreate_s16(0x0000000100020003));
  int16x8_t       iq_shl_s16     = vshlq_s16(reg, shift_mask_s16);

  // Mask 16bit words to keep only 9 shifted bits.
  const int16x8_t mask_s16 = vcombine_s16(vcreate_s16(0x1ff03fe07fc0ff80), vcreate_s16(0x01ff03fe07fc0ff8));
  iq_shl_s16               = vandq_s16(iq_shl_s16, mask_s16);

  // Shuffle it and create two new vectors that can be OR'ed to produce final result. Temporal vectors look as follows:
  // 0  0  0  0  0  0  0  0  | i0 0  0  0  0  0  0  0 | q1 q0  0  0  0  0  0  0 | i2 i1 i0  0  0  0  0  0 | ...
  // i8 i7 i6 i5 i4 i3 i2 i1 | 0 q8 q7 q6 q5 q4 q3 q2 |  0  0 i8 i7 i6 i5 i4 i3 | 0  0  0  q8 q7 q6 q5 q4 | ...
  int8x16_t iq_shl_s8 = vreinterpretq_s8_s16(iq_shl_s16);
  int8x16_t tmp_iq_0_s8 =
      vqtbl1q_s8(iq_shl_s8, vcombine_u8(vcreate_u8(0x0c0a0806040200ff), vcreate_u8(0xffffffffffffff0e)));
  int8x16_t tmp_iq_1_s8 =
      vqtbl1q_s8(iq_shl_s8, vcombine_u8(vcreate_u8(0x0f0d0b0907050301), vcreate_u8(0xffffffffffffffff)));

  // Perform 'bitwise OR'.
  return vorrq_u8(vreinterpretq_u8_s8(tmp_iq_0_s8), vreinterpretq_u8_s8(tmp_iq_1_s8));
}

/// \brief Packs 16bit IQ values of the PRB using given bit width and big-endian format.
///
/// \param[out] comp_prb_buffer Buffer dedicated for storing compressed packed bytes of the PRB.
/// \param[in]  regs            NEON registers storing 16bit IQ samples of the PRB.
///
/// \note Each of the input registers stores four unique REs.
inline void pack_prb_9b_big_endian(span<uint8_t> comp_prb_buffer, int16x8x3_t regs)
{
  /// Number of bytes used by 1 packed PRB with IQ samples compressed to 9 bits.
  static constexpr unsigned BYTES_PER_PRB_9BIT_COMPRESSION = 27;

  static constexpr unsigned bytes_per_half_reg = 8;

  srsran_assert(comp_prb_buffer.size() == BYTES_PER_PRB_9BIT_COMPRESSION,
                "Output buffer has incorrect size for packing compressed samples");

  // Pack input registers.
  uint8x16_t res_packed_bytes_0_u8 = pack_neon_register_9b_big_endian(regs.val[0]);
  uint8x16_t res_packed_bytes_1_u8 = pack_neon_register_9b_big_endian(regs.val[1]);
  uint8x16_t res_packed_bytes_2_u8 = pack_neon_register_9b_big_endian(regs.val[2]);

  uint8_t* data = comp_prb_buffer.data();

  // Store first 9 bytes of every register storing packed bytes.
  vst1_u64(reinterpret_cast<uint64_t*>(data), vreinterpret_u64_u8(vget_low_u8(res_packed_bytes_0_u8)));
  vst1q_lane_u8(data + bytes_per_half_reg, res_packed_bytes_0_u8, bytes_per_half_reg);
  data += bytes_per_half_reg + 1;

  vst1_u64(reinterpret_cast<uint64_t*>(data), vreinterpret_u64_u8(vget_low_u8(res_packed_bytes_1_u8)));
  vst1q_lane_u8(data + bytes_per_half_reg, res_packed_bytes_1_u8, bytes_per_half_reg);
  data += bytes_per_half_reg + 1;

  vst1_u64(reinterpret_cast<uint64_t*>(data), vreinterpret_u64_u8(vget_low_u8(res_packed_bytes_2_u8)));
  vst1q_lane_u8(data + bytes_per_half_reg, res_packed_bytes_2_u8, bytes_per_half_reg);
}

/// \brief Packs 16bit IQ values of the PRB using given bit width and big-endian format.
///
/// \param[out] comp_prb_buffer Buffer dedicated for storing compressed packed bytes of the PRB.
/// \param[in]  regs            NEON registers storing 16bit IQ samples of the PRB.
///
/// \note Each of the input registers stores four unique REs.
inline void pack_prb_16b_big_endian(span<uint8_t> comp_prb_buffer, int16x8x3_t regs)
{
  /// Number of bytes used by 1 packed PRB with IQ samples compressed to 16 bits.
  static constexpr unsigned BYTES_PER_PRB_16BIT_COMPRESSION = 48;
  static constexpr unsigned NEON_REG_SIZE_BYTES             = 16;

  srsran_assert(comp_prb_buffer.size() == BYTES_PER_PRB_16BIT_COMPRESSION,
                "Output buffer has incorrect size for packing compressed samples");

  static const uint8x16_t shuffle_mask_u8 = vcombine_u8(vcreate_u8(0x0607040502030001), vcreate_u8(0x0e0f0c0d0a0b0809));

  int8x16x3_t regs_shuffled_s16;
  regs_shuffled_s16.val[0] = vqtbl1q_s8(vreinterpretq_s8_s16(regs.val[0]), shuffle_mask_u8);
  regs_shuffled_s16.val[1] = vqtbl1q_s8(vreinterpretq_s8_s16(regs.val[1]), shuffle_mask_u8);
  regs_shuffled_s16.val[2] = vqtbl1q_s8(vreinterpretq_s8_s16(regs.val[2]), shuffle_mask_u8);

  int8_t* data = reinterpret_cast<int8_t*>(comp_prb_buffer.data());
  vst1q_s8(data, regs_shuffled_s16.val[0]);
  vst1q_s8(data + NEON_REG_SIZE_BYTES, regs_shuffled_s16.val[1]);
  vst1q_s8(data + NEON_REG_SIZE_BYTES * 2, regs_shuffled_s16.val[2]);
}

/// \brief Packs 16bit IQ values of a resource block using the specified width and big-endian format.
///
/// \param[out] comp_prb_buffer Buffer dedicated for storing compressed packed bytes of the PRB.
/// \param[in]  reg             Vector of three NEON registers storing 16bit IQ pairs of the PRB.
/// \param[in] iq_width         Bit width of the resulting packed IQ samples.
inline void pack_prb_big_endian(span<uint8_t> comp_prb_buffer, int16x8x3_t regs, unsigned iq_width)
{
  if (iq_width == 9) {
    return pack_prb_9b_big_endian(comp_prb_buffer, regs);
  }
  if (iq_width == 16) {
    return pack_prb_16b_big_endian(comp_prb_buffer, regs);
  }
  report_fatal_error("Unsupported bit width");
}

/// \brief Unpacks packed 9bit IQ samples stored as bytes in big-endian format to an array of 16bit signed values.
///
/// \param[out] unpacked_iq_data A sequence of 24 integers, corresponding to \c NOF_CARRIERS_PER_RB unpacked IQ pairs.
/// \param[in]  packed_data      A sequence of 27 packed bytes.
inline void unpack_prb_9b_big_endian(span<int16_t> unpacked_iq_data, span<const uint8_t> packed_data)
{
  // Load input (we need two NEON register to load 27 bytes).
  // The first 16 bytes are loaded directly.
  uint8x16x2_t packed_vec_u8x2;
  packed_vec_u8x2.val[0] = vld1q_u8(packed_data.data());
  // Load from 11th byte, which gives us the last 11 bytes plus 5 extra bytes without exceeding a read of 27 bytes.
  packed_vec_u8x2.val[1] = vld1q_u8(packed_data.data() + 11);
  // Discard the first 5 bytes.
  packed_vec_u8x2.val[1] = vextq_u8(packed_vec_u8x2.val[1], vdupq_n_u8(0), 5);

  // Duplicate input words (it is required since below in the code every byte will be used twice:
  // to provide MSB bits of the current IQ sample and LSB bits of the previous IQ sample).
  uint8x16_t tmp_packed_0_u8 =
      vqtbl2q_u8(packed_vec_u8x2, vcombine_u8(vcreate_u8(0x0304020301020001), vcreate_u8(0x0708060705060405)));
  uint8x16_t tmp_packed_1_u8 =
      vqtbl2q_u8(packed_vec_u8x2, vcombine_u8(vcreate_u8(0x0c0d0b0c0a0b090a), vcreate_u8(0x10110f100e0f0d0e)));
  uint8x16_t tmp_packed_2_u8 =
      vqtbl2q_u8(packed_vec_u8x2, vcombine_u8(vcreate_u8(0x1516141513141213), vcreate_u8(0x191a181917181617)));

  // Shift left to align to 16bit boundary.
  const int16x8_t shl_mask_s16        = vcombine_s16(vcreate_s16(0x0003000200010000), vcreate_s16(0x0007000600050004));
  uint16x8_t      shl_tmp_packed_0_u8 = vshlq_u16(vreinterpretq_u16_u8(tmp_packed_0_u8), shl_mask_s16);
  uint16x8_t      shl_tmp_packed_1_u8 = vshlq_u16(vreinterpretq_u16_u8(tmp_packed_1_u8), shl_mask_s16);
  uint16x8_t      shl_tmp_packed_2_u8 = vshlq_u16(vreinterpretq_u16_u8(tmp_packed_2_u8), shl_mask_s16);

  // Arithmetically shift right by 7 positions to put bits of interest into LSB positions while preserving the sign.
  int16x8_t unpacked_data_0_s16 = vshrq_n_s16(vreinterpretq_s16_u16(shl_tmp_packed_0_u8), 7);
  int16x8_t unpacked_data_1_s16 = vshrq_n_s16(vreinterpretq_s16_u16(shl_tmp_packed_1_u8), 7);
  int16x8_t unpacked_data_2_s16 = vshrq_n_s16(vreinterpretq_s16_u16(shl_tmp_packed_2_u8), 7);

  // Write results to the output buffer.
  vst1q_s16(unpacked_iq_data.data(), unpacked_data_0_s16);
  vst1q_s16(unpacked_iq_data.data() + 8, unpacked_data_1_s16);
  vst1q_s16(unpacked_iq_data.data() + 16, unpacked_data_2_s16);
}

/// \brief Unpacks packed 16bit IQ samples stored as bytes in big-endian format to an array of 16bit signed values.
///
/// \param[out] unpacked_iq_data A sequence of 24 integers, corresponding to \c NOF_CARRIERS_PER_RB unpacked IQ pairs.
/// \param[in]  packed_data      A sequence of 48 packed bytes.
inline void unpack_prb_16b_big_endian(span<int16_t> unpacked_iq_data, span<const uint8_t> packed_data)
{
  static const uint8x16_t shuffle_mask_u8 = vcombine_u8(vcreate_u8(0x0607040502030001), vcreate_u8(0x0e0f0c0d0a0b0809));

  // Load input (we need three NEON register to load 48 bytes).
  uint8x16x3_t packed_vec_u8x3 = vld1q_u8_x3(packed_data.data());

  uint8x16x3_t packed_shuffled_s16;
  packed_shuffled_s16.val[0] = vqtbl1q_u8(packed_vec_u8x3.val[0], shuffle_mask_u8);
  packed_shuffled_s16.val[1] = vqtbl1q_u8(packed_vec_u8x3.val[1], shuffle_mask_u8);
  packed_shuffled_s16.val[2] = vqtbl1q_u8(packed_vec_u8x3.val[2], shuffle_mask_u8);

  // Write results to the output buffer.
  vst1q_s16(unpacked_iq_data.data(), vreinterpretq_s16_u8(packed_shuffled_s16.val[0]));
  vst1q_s16(unpacked_iq_data.data() + 8, vreinterpretq_s16_u8(packed_shuffled_s16.val[1]));
  vst1q_s16(unpacked_iq_data.data() + 16, vreinterpretq_s16_u8(packed_shuffled_s16.val[2]));
}

/// \brief Unpacks packed IQ samples stored as bytes in big-endian format to an array of 16bit signed values.
///
/// \param[out] unpacked_iq_data A sequence of 24 integers, corresponding to \c NOF_CARRIERS_PER_RB unpacked IQ pairs.
/// \param[in]  packed_data      A sequence of input packed bytes.
/// \param[in] iq_width          Bit width of the packed IQ samples.
inline void unpack_prb_big_endian(span<int16_t> unpacked_iq_data, span<const uint8_t> packed_data, unsigned iq_width)
{
  if (iq_width == 9) {
    return unpack_prb_9b_big_endian(unpacked_iq_data, packed_data);
  }
  if (iq_width == 16) {
    return unpack_prb_16b_big_endian(unpacked_iq_data, packed_data);
  }
  report_fatal_error("Unsupported bit width");
}

/// \brief Checks whether the requested bit width is supported by the NEON implementation.
/// \param[in] iq_width Requested bit width.
///
/// \return True in case packing/unpacking with the requested bit width is supported.
inline bool iq_width_packing_supported(unsigned iq_width)
{
  return ((iq_width == 9) || (iq_width == 16));
}

} // namespace neon
} // namespace ofh
} // namespace srsran
