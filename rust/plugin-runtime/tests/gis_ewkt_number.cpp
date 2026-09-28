
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

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>
#include "../../../plugins/gis/number_format.h"
#include "common/number/ob_number_v2.h"
#include "lib/utility/data_buffer.h"
#include "lib/charset/ob_dtoa.h"

using namespace oceanbase::common;
#include "legacy_ewkt_number_probe.h"

static uint64_t checked = 0;
static void check(double value, int64_t precision, int width)
{
  char expected[256] = {};
  uint64_t length = 0;
  int ret = OB_SUCCESS;
  if (precision > 0 && precision < 25) {
    ret = legacy_append_double_with_prec(expected, 25, length, value,
                                        static_cast<int16_t>(precision));
  } else {
    length = ob_gcvt(value, OB_GCVT_ARG_DOUBLE, width, expected, nullptr);
    if (length == 0) ret = OB_SIZE_OVERFLOW;
  }
  std::string actual = "unchanged";
  const bool success = seekdb_gis::format_ewkt_number(value, precision, actual, width);
  if (success != (ret == OB_SUCCESS) ||
      (success && actual != std::string(expected, length)) ||
      (!success && actual != "unchanged")) {
    std::cerr.precision(17);
    std::cerr << "value=" << value << " precision=" << precision << " width=" << width
              << " ret=" << ret << " expected=" << std::string(expected, ret == OB_SUCCESS ? length : 0)
              << " actual=" << actual << std::endl;
    std::abort();
  }
  ++checked;
}

int main()
{
  std::vector<double> values = {0., -0., 1., -1., 1.25, -1.25, 9.999, -9.999,
      0.0001, -0.0001, 1e-8, 1e15, 1e16, 1e-14, -1.234567891234567e-14,
      std::numeric_limits<double>::min(), std::numeric_limits<double>::max(),
      std::numeric_limits<double>::denorm_min()};
  for (double boundary : {1e-8, 1e15, 0.05, 0.005, 9.95, 99.95, 999.95}) {
    for (double value : {std::nextafter(boundary, 0.), boundary,
                        std::nextafter(boundary, std::numeric_limits<double>::infinity())}) {
      values.push_back(value);
      values.push_back(-value);
    }
  }
  uint64_t state = 0xc0ffee123456789ULL;
  for (int i = 0; i < 12000; ++i) {
    state ^= state << 13; state ^= state >> 7; state ^= state << 17;
    double value;
    std::memcpy(&value, &state, sizeof(value));
    if (std::isfinite(value)) values.push_back(value);
  }
  for (double value : values) {
    for (int precision = -1; precision <= 26; ++precision) check(value, precision, 256);
    check(value, 0, 25);
    check(value, std::numeric_limits<int64_t>::min(), 256);
    check(value, std::numeric_limits<int64_t>::max(), 256);
  }
  std::cout << "PASS: " << checked
            << " EWKT number controls against original dtoa + ObNumber formatter" << std::endl;
}
