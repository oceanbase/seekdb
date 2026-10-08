
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

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include "../../../plugins/gis/number_format.cpp"

#define CHECK(condition) do { if (!(condition)) { \
  std::cerr << __LINE__ << ": " << #condition << std::endl; std::abort(); \
} } while (false)

static void check(double value, int64_t precision, const char *expected, int width = 256)
{
  std::string actual;
  CHECK(seekdb_gis::format_ewkt_number(value, precision, actual, width));
  CHECK(actual == expected);
}

int main()
{
  check(1.25, 1, "1.3");
  check(-1.25, 1, "-1.3");
  check(9.999, 2, "10");
  check(-9.999, 2, "-10");
  check(-0.0001, 2, "0");
  check(-0., 15, "0");
  check(1.2345, 0, "1.2345");
  check(1.2345, -1, "1.2345");
  check(1.2345, 25, "1.2345");
  check(1.2345, INT64_MIN, "1.2345");
  check(1.2345, INT64_MAX, "1.2345");
  check(1.2345e-9, 2, "1.23e-9");
  check(1e16, 2, "1e16");
  check(std::numeric_limits<double>::denorm_min(), 15, "5e-324");
  check(1.2345, -1, "1.2345", 25);
  std::string output = "unchanged";
  CHECK(!seekdb_gis::format_ewkt_number(1.0, -1, output, 24));
  CHECK(output == "unchanged");

  const char token[] = {'1', '.', '2', '5', ','}; // Deliberately not NUL-terminated.
  const char *next = token;
  double value = 17;
  CHECK(seekdb_gis::parse_wkt_number(token, token + sizeof(token), value, next));
  CHECK(value == 1.25 && next == token + 4);
  const char exact[] = {'1', 'e', '2'};
  CHECK(seekdb_gis::parse_wkt_number(exact, exact + sizeof(exact), value, next));
  CHECK(value == 100 && next == exact + sizeof(exact));
  const char overflow[] = "1e309";
  const char *saved = next;
  CHECK(!seekdb_gis::parse_wkt_number(overflow, overflow + 5, value, next));
  CHECK(value == 100 && next == saved);
  CHECK(!seekdb_gis::parse_wkt_number(nullptr, nullptr, value, next));

  // Verify fallback allocator unlinking and scope restoration on exceptions.
  // The oversized allocation deterministically injects failure before malloc.
  using namespace seekdb_gis::legacy_dtoa;
  CHECK(active_scope == nullptr);
  {
    AllocationScope outer;
    void *first = seekdb_gis::legacy_dtoa::malloc(32);
    void *second = seekdb_gis::legacy_dtoa::malloc(64);
    seekdb_gis::legacy_dtoa::free(first);
    CHECK(outer.head_->previous == nullptr && outer.head_->next == nullptr);
    seekdb_gis::legacy_dtoa::free(second);
    CHECK(outer.head_ == nullptr);
    try {
      AllocationScope inner;
      (void)seekdb_gis::legacy_dtoa::malloc(128);
      (void)seekdb_gis::legacy_dtoa::malloc(std::numeric_limits<size_t>::max());
      CHECK(false);
    } catch (const std::bad_alloc &) {
      CHECK(active_scope == &outer);
    }
  }
  CHECK(active_scope == nullptr);
  std::cout << "PASS: EWKT number golden controls and allocation-scope failure cleanup" << std::endl;
}
