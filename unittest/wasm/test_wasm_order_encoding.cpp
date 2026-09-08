// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include "data_plane/ob_order_perserving_encoder.h"
#include <array>
#include <type_traits>
#include <cassert>
#include <cstring>
#include <limits>
#include <cstdio>

using oceanbase::share::ObOrderPerservingEncoder;

template <typename T, typename Encoder>
static void check_order(Encoder encode)
{
  constexpr T low = std::numeric_limits<T>::lowest();
  constexpr T high = std::numeric_limits<T>::max();
  std::array<T, 7> values;
  if constexpr (std::is_signed_v<T>) {
    values = {low, T(low + 1), T(-1), T(0), T(1), T(high - 1), high};
  } else {
    values = {T(0), T(1), T(2), T(high / 2), T(high - 2), T(high - 1), high};
  }
  std::array<unsigned char, sizeof(T)> previous{};
  bool first = true;
  for (T value : values) {
    std::array<unsigned char, sizeof(T) + 2> guarded;
    guarded.fill(0xa5);
    int64_t length = 0;
    assert(encode(value, guarded.data() + 1, length) == 0);
    assert(length == sizeof(T));
    assert(guarded.front() == 0xa5 && guarded.back() == 0xa5);
    if (!first) assert(memcmp(previous.data(), guarded.data() + 1, sizeof(T)) < 0);
    memcpy(previous.data(), guarded.data() + 1, sizeof(T));
    first = false;
    if (value == low || value == high) {
      for (size_t i = 1; i <= sizeof(T); ++i) assert(guarded[i] == (value == low ? 0 : 255));
    }
  }
}

int main()
{
  using E = ObOrderPerservingEncoder;
  check_order<int8_t>(E::encode_from_int8);
  check_order<int16_t>(E::encode_from_int16);
  check_order<int32_t>(E::encode_from_int32);
  check_order<int64_t>(E::encode_from_int);
  check_order<uint8_t>(E::encode_from_uint8);
  check_order<uint16_t>(E::encode_from_uint16);
  check_order<uint32_t>(E::encode_from_uint32);
  check_order<uint64_t>(E::encode_from_uint);
  unsigned char bytes[8]{};
  int64_t length = 0;
  assert(E::encode_from_double(1.0, bytes, length) == 0 && length == 8);
  const unsigned char expected_double[] = {0xbf, 0xf0, 0, 0, 0, 0, 0, 0};
  assert(memcmp(bytes, expected_double, sizeof(bytes)) == 0);
  length = 0;
  assert(E::encode_from_float(1.0f, bytes, length) == 0 && length == 4);
  const unsigned char expected_float[] = {0xbf, 0x80, 0, 0};
  assert(memcmp(bytes, expected_float, sizeof(expected_float)) == 0);
  puts("PASS: seekdb ordered numeric encoding in Wasm");
}
