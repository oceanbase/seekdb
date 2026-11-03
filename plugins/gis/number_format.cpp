
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

#include "number_format.h"

#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <fast_float/fast_float.h>
#if defined(__linux__)
#include <endian.h>
#endif

namespace seekdb_gis {
namespace legacy_dtoa {
// The original algorithm uses malloc without checking its result. The plugin
// adapter tracks fallback allocations per invocation, throws on OOM and reclaims
// outstanding blocks during unwinding; the core's allocation policy is unchanged.
struct alignas(std::max_align_t) Allocation {
  Allocation *previous;
  Allocation *next;
};
class AllocationScope;
static thread_local AllocationScope *active_scope = nullptr;
class AllocationScope {
public:
  AllocationScope() : parent_(active_scope) { active_scope = this; }
  ~AllocationScope()
  {
    while (head_ != nullptr) {
      Allocation *next = head_->next;
      std::free(head_);
      head_ = next;
    }
    active_scope = parent_;
  }
  AllocationScope(const AllocationScope &) = delete;
  AllocationScope &operator=(const AllocationScope &) = delete;
  Allocation *head_ = nullptr;
private:
  AllocationScope *parent_;
};
static void *malloc(size_t size)
{
  if (size > std::numeric_limits<size_t>::max() - sizeof(Allocation)) throw std::bad_alloc();
  auto *allocation = static_cast<Allocation *>(std::malloc(size + sizeof(Allocation)));
  if (allocation == nullptr) throw std::bad_alloc();
  allocation->previous = nullptr;
  allocation->next = active_scope->head_;
  if (allocation->next != nullptr) allocation->next->previous = allocation;
  active_scope->head_ = allocation;
  return allocation + 1;
}
static void free(void *pointer)
{
  if (pointer != nullptr) {
    auto *allocation = static_cast<Allocation *>(pointer) - 1;
    if (allocation->previous != nullptr) allocation->previous->next = allocation->next;
    else active_scope->head_ = allocation->next;
    if (allocation->next != nullptr) allocation->next->previous = allocation->previous;
    std::free(allocation);
  }
}

using int32 = int32_t;
using uint32 = uint32_t;
using int64 = int64_t;
using uint64 = uint64_t;
enum ob_gcvt_arg_type { OB_GCVT_ARG_FLOAT, OB_GCVT_ARG_DOUBLE };
size_t ob_gcvt_strict(double, ob_gcvt_arg_type, int, char *, bool *, bool, bool);
#define TRUE true
#define FALSE false
#define OB_MIN(a, b) ((a) < (b) ? (a) : (b))
#define OB_MAX(a, b) ((a) > (b) ? (a) : (b))
#define MY_ALIGN(a, alignment) (((a) + (alignment) - 1) & ~((alignment) - 1))
#define SIZEOF_CHARP sizeof(void *)
#include "seekdb/geo/dtoa_impl.ipp"
#undef SIZEOF_CHARP
#undef MY_ALIGN
#undef OB_MAX
#undef OB_MIN
#undef FALSE
#undef TRUE
} // namespace legacy_dtoa

bool parse_wkt_number(const char *begin, const char *end, double &value,
                      const char *&next)
{
  if (begin == nullptr || end == nullptr || begin >= end) return false;
  legacy_dtoa::AllocationScope allocations;
  char *after = const_cast<char *>(end);
  int error = 0;
  const double parsed = legacy_dtoa::ob_strtod(begin, &after, &error);
  if (error != 0 || after <= begin || after > end || !std::isfinite(parsed)) return false;
  value = parsed;
  next = after;
  return true;
}

bool format_ewkt_number(double value, int64_t precision, std::string &output,
                        int unscaled_width)
{
  legacy_dtoa::AllocationScope allocations;
  char buffer[256] = {};
  const bool scaled = precision > 0 && precision < 25;
  if (!scaled && (unscaled_width < 25 || unscaled_width > static_cast<int>(sizeof(buffer))))
    return false;
  const bool force_scientific = scaled && (std::fabs(value) < 1e-8 || std::fabs(value) > 1e15);
  const size_t length = legacy_dtoa::ob_gcvt_strict(value,
      legacy_dtoa::OB_GCVT_ARG_DOUBLE, scaled ? sizeof(buffer) : unscaled_width,
      buffer, nullptr, true, force_scientific);
  if (length == 0) return false;
  std::string result(buffer, length);
  if (scaled) {
    // ObNumber receives only the decimal mantissa. Reproduce its decimal
    // half-away-from-zero rounding using digits, avoiding a binary round().
    const size_t exponent_pos = result.find_first_of("eE");
    std::string exponent;
    if (exponent_pos != std::string::npos) {
      exponent = result.substr(exponent_pos);
      result.resize(exponent_pos);
      if (exponent == "e0" || exponent == "E0") exponent.clear();
    }
    const bool negative = !result.empty() && result[0] == '-';
    if (negative) result.erase(0, 1);
    const size_t dot = result.find('.');
    if (dot != std::string::npos) {
      const size_t cut = dot + 1 + static_cast<size_t>(precision);
      if (cut < result.size()) {
        const bool carry = result[cut] >= '5';
        result.resize(cut);
        if (carry) {
          size_t position = result.size();
          bool pending = true;
          while (position != 0 && pending) {
            char &digit = result[--position];
            if (digit == '.') continue;
            if (digit == '9') digit = '0';
            else { ++digit; pending = false; }
          }
          if (pending) result.insert(0, 1, '1');
        }
      }
      while (!result.empty() && result.back() == '0') result.pop_back();
      if (!result.empty() && result.back() == '.') result.pop_back();
    }
    // ObNumber canonicalizes input -0 and emits unsigned zero after rounding
    // at positive scale. (Precision 0 is unscaled at the SQL entry point.)
    if (negative && result != "0") result.insert(0, 1, '-');
    result += exponent;
    if (result.size() > 25) return false;
  }
  output.swap(result);
  return true;
}
} // namespace seekdb_gis
