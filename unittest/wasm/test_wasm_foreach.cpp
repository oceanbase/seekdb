// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
#include <cassert>
#include <cstdint>
#include <cstdio>
#include "lib/utility/ob_macro_utils.h"

namespace {
struct Array {
  int values[4] = {2, 3, 5, 7};
  int64_t size = 4;
  mutable int accesses = 0;
  int64_t count() const { return size; }
  const int &at(int64_t index) const {
    assert(index >= 0 && index < size);
    ++accesses;
    return values[index];
  }
  int &at(int64_t index) {
    return const_cast<int &>(static_cast<const Array &>(*this).at(index));
  }
};
}

int main()
{
  Array values;
  int visits = 0;
  FOREACH_CNT(value, values) {
    assert(visits < 4); // Bound a corrupted counter's repeated iterations.
    assert(value == &values.values[visits]);
    *value += 1;
    ++visits;
  }
  assert(visits == 4 && values.values[0] == 3 && values.values[3] == 8);

  const Array &immutable = values;
  int sum = 0;
  FOREACH_CNT(value, immutable) {
    if (*value == 4) continue;
    if (*value == 8) break;
    sum += *value;
  }
  assert(sum == 9);

  visits = 0;
  FOREACH_CNT_X(value, immutable, visits < 2) { ++visits; }
  assert(visits == 2);
  values.accesses = 0;
  FOREACH_CNT_X(value, values, false) { assert(false); }
  assert(values.accesses == 0);
  values.size = 0;
  FOREACH_CNT(value, values) { assert(false); }
  assert(values.accesses == 0);

  values.size = 4;
  visits = 0;
  FOREACH_CNT(value, values) {
    ++visits;
    values.size = 1;
  }
  assert(visits == 1);

  values.size = 4;
  visits = 0;
  FOREACH_CNT(value, values) {
    FOREACH_CNT(value, immutable) {
      ++visits;
      break;
    }
  }
  assert(visits == 4);
  // The macro must remain one statement, including in an unbraced if/else.
  if (true)
    FOREACH_CNT(value, values) { break; }
  else
    assert(false);
  std::puts("foreach: traversal, mutation, control flow and empty inputs passed");
}
