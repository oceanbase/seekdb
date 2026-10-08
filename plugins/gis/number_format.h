
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

#pragma once

#include <cstdint>
#include <string>

namespace seekdb_gis {
// Bounded original numeric conversion for WKT tokens. Outputs change only on
// success; callers validate token starts and delimiters using WKT grammar.
bool parse_wkt_number(const char *begin, const char *end, double &value,
                      const char *&next);

// EWKT's precision policy: 0/negative/>=25 means unscaled; 1..24 rounds
// decimal mantissa digits, not binary floating-point coordinates.
// Three-dimensional callers pass -1 because the legacy visitor ignores scale.
// False means the legacy 25-byte scaled-output limit was exceeded.
// May throw std::bad_alloc; only call behind the plugin's exception boundary.
bool format_ewkt_number(double value, int64_t precision, std::string &output,
                        int unscaled_width = 256);
} // namespace seekdb_gis
