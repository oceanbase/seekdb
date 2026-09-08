// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include "lib/codec/ob_fast_delta.h"
#include "lib/codec/ob_generated_unalign_simd_bp_func.h"
#include "lib/checksum/ob_crc64.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace oceanbase::common;

static void check_deltas()
{
  constexpr uint32_t guard = 0xfeedface;
  for (size_t length = 0; length <= 129; ++length) {
    std::array<uint32_t, 131> original{}, deltas, restored, inplace;
    for (size_t i = 0; i < length; ++i) original[i + 1] = uint32_t(i * UINT32_C(0xa1379bdf));
    for (uint32_t start : {UINT32_C(0), UINT32_MAX}) {
      deltas.fill(guard);
      restored.fill(guard);
      compute_deltas(original.data() + 1, length, deltas.data() + 1, start);
      assert(deltas.front() == guard && deltas[length + 1] == guard);
      compute_prefix_sum(deltas.data() + 1, length, restored.data() + 1, start);
      assert(restored.front() == guard && restored[length + 1] == guard);
      assert(memcmp(original.data() + 1, restored.data() + 1, length * sizeof(uint32_t)) == 0);
      inplace = original;
      compute_deltas_inplace(inplace.data() + 1, length, start);
      assert(memcmp(inplace.data() + 1, deltas.data() + 1, length * sizeof(uint32_t)) == 0);
      compute_prefix_sum_inplace(inplace.data() + 1, length, start);
      assert(inplace == original);
    }
  }
}

template <typename T, typename Pack, typename Unpack>
static void check_bitpacking(Pack pack, Unpack unpack)
{
  constexpr unsigned word_bits = sizeof(T) * 8;
  constexpr unsigned lanes = 16 / sizeof(T);
  constexpr T guard = T(0xa5a5a5a5);
  for (unsigned width = 0; width <= word_bits; ++width) {
    alignas(16) std::array<T, 130> input{}, packed, decoded;
    std::array<T, 128> expected{};
    const T mask = T((UINT64_C(1) << width) - 1);
    for (unsigned i = 0; i < 128; ++i) {
      input[i + 1] = T((UINT32_C(0xa913765b) * i) ^ (i >> 1)) & mask;
      // Independent bit-by-bit reference for the format's interleaved lanes.
      for (unsigned bit = 0; bit < width; ++bit) {
        const unsigned pos = (i / lanes) * width + bit;
        if ((input[i + 1] >> bit) & 1) {
          expected[(pos / word_bits) * lanes + i % lanes] |= T(T(1) << (pos % word_bits));
        }
      }
    }
    packed.fill(guard);
    decoded.fill(guard);
    pack(input.data() + 1, reinterpret_cast<__m128i *>(packed.data() + 1), width);
    const unsigned packed_words = 128 * width / word_bits;
    assert(packed.front() == guard && packed[packed_words + 1] == guard);
    assert(memcmp(packed.data() + 1, expected.data(), packed_words * sizeof(T)) == 0);
    unpack(reinterpret_cast<const __m128i *>(packed.data() + 1), decoded.data() + 1, width);
    assert(decoded.front() == guard && decoded.back() == guard);
    assert(memcmp(input.data() + 1, decoded.data() + 1, 128 * sizeof(T)) == 0);
  }
}

static void check_checksums()
{
  assert((uint32_t(ob_crc64(UINT32_MAX, "123456789", 9)) ^ UINT32_MAX) == UINT32_C(0xe3069283));
  std::array<unsigned char, 144> data;
  for (size_t i = 0; i < data.size(); ++i) data[i] = (i * 177) ^ (i >> 1);
  for (size_t offset = 0; offset < 8; ++offset) {
    for (size_t length = 0; length <= 129; ++length) {
      for (uint64_t seed : {UINT64_C(0), UINT64_C(0x12345678), UINT64_MAX}) {
        uint32_t expected = static_cast<uint32_t>(seed);
        for (size_t i = offset; i < offset + length; ++i) {
          expected ^= data[i];
          for (unsigned bit = 0; bit < 8; ++bit) {
            expected = (expected >> 1) ^ ((expected & 1) ? UINT32_C(0x82f63b78) : 0);
          }
        }
        const uint64_t result = ob_crc64(seed, data.data() + offset, length);
        assert(result == (length == 0 ? seed : expected));
        uint64_t incremental = seed;
        for (size_t pos = 0; pos < length; pos += 17) {
          incremental = ob_crc64(incremental, data.data() + offset + pos, std::min(size_t(17), length - pos));
        }
        assert(result == incremental);
      }
    }
  }
  assert(ob_crc64(UINT64_MAX, nullptr, 0) == UINT64_MAX);
  assert(ob_crc64(UINT64_MAX, data.data(), -1) == UINT64_MAX);
}

int main()
{
  check_deltas();
  check_bitpacking<uint16_t>(uSIMD_fastpackwithoutmask_128_16, uSIMD_fastunpack_128_16);
  check_bitpacking<uint32_t>(uSIMD_fastpackwithoutmask_128_32, uSIMD_fastunpack_128_32);
  check_checksums();
  puts("PASS: seekdb SIMD integer codec, exact packed bytes, deltas and CRC32C");
}
