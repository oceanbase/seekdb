/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ob_parity_check.h"
#include "deps/oblib/src/lib/ob_define.h"

namespace oceanbase
{
namespace common
{
#define P2(n) n, n^1, n^1, n
#define P4(n) P2(n), P2(n^1), P2(n^1), P2(n)
#define P6(n) P4(n), P4(n^1), P4(n^1), P4(n)

// The table shows the number of 1 for 0~255.
// If it contains an even number of 1, the value is 0, otherwise the value is 1.
const bool ParityTable[256] =
{
  P6(0), P6(1), P6(1), P6(0)
};

// If val contains an even number of 1, the value is 0, otherwise the value is 1.
bool parity_check(const uint16_t value)
{
  bool bool_ret = false;
  uint16_t val = value;
  val ^= val >> 8;
  if (ParityTable[val & 0xff])
  {
    bool_ret = true;
  } else {
    bool_ret = false;
  }
  return bool_ret;
}

// If val contains an even number of 1, the value is 0, otherwise the value is 1.
bool parity_check(const uint32_t value)
{
  bool bool_ret = false;
  uint32_t val = value;
  val ^= val >> 16;
  val ^= val >> 8;
  if (ParityTable[val & 0xff])
  {
    bool_ret = true;
  } else {
    bool_ret = false;
  }
  return bool_ret;
}

// If val contains an even number of 1, the value is 0, otherwise the value is 1.
bool parity_check(const uint64_t value)
{
  bool bool_ret = false;
  uint64_t val = value;
  val ^= val >> 32;
  val ^= val >> 16;
  val ^= val >> 8;
  if (ParityTable[val & 0xff])
  {
    bool_ret = true;
  } else {
    bool_ret = false;
  }
  return bool_ret;
}

}
}
