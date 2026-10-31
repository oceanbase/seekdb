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
#include <type_traits>
#include "lib/ob_errno.h"

namespace oceanbase
{
namespace common
{

// Execution value only. A Type or a prepared operation determines which member
// is active; an ordinary integer always uses its inline scalar member. The
// legacy pointer Datum and the stored-cell codec are separate boundary types.
// This carrier owns neither referenced bytes nor resources. A shallow copy of
// a reference does not prevent its backing storage from being reused.
class alignas(8) Datum
{
public:
  enum class State : uint8_t { Null, Present, Nop, Min, Max };
  enum ReferenceFlag : uint16_t {
    NoReferenceFlags = 0, OutRow = 1, HasLobHeader = 2, LegacyExtended = 4
  };

  Datum() : payload_(), length_(0), state_(State::Null), reserved_(0), flags_(0) {}

  State state() const { return state_; }
  bool is_null() const { return state_ == State::Null; }
  bool is_present() const { return state_ == State::Present; }
  bool is_nop() const { return state_ == State::Nop; }
  bool is_min() const { return state_ == State::Min; }
  bool is_max() const { return state_ == State::Max; }

  // Scalar accessors require a present value whose prepared type selects the
  // corresponding canonical representation. They do not inspect payload bits
  // or perform runtime type lookup. Signed/unsigned conversion belongs to cast.
  int64_t get_int() const { return payload_.i64_; }
  uint64_t get_uint() const { return payload_.u64_; }
  float get_float() const { return payload_.f32_; }
  double get_double() const { return payload_.f64_; }

  void set_int(int64_t value) { clear(State::Present); payload_.i64_ = value; }
  void set_uint(uint64_t value) { clear(State::Present); payload_.u64_ = value; }
  void set_float(float value) { clear(State::Present); payload_.f32_ = value; }
  void set_double(double value) { clear(State::Present); payload_.f64_ = value; }

  void set_null() { clear(State::Null); }
  void set_nop() { clear(State::Nop); }
  void set_min() { clear(State::Min); }
  void set_max() { clear(State::Max); }

  // Reference access requires the caller's type/layout to select a reference
  // representation. An empty, non-NULL byte string may have a null address.
  const void *get_reference() const { return payload_.reference_; }
  uint32_t length() const { return length_; }
  uint16_t reference_flags() const { return flags_; }
  int set_reference(const void *bytes, uint32_t length, uint16_t flags = NoReferenceFlags)
  {
    int ret = OB_SUCCESS;
    if (length > 0 && bytes == nullptr) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      clear(State::Present);
      payload_.reference_ = bytes;
      length_ = length;
      flags_ = flags;
    }
    return ret;
  }

private:
  friend class DatumCopyPlan;

  union Payload
  {
    int64_t i64_;
    uint64_t u64_;
    double f64_;
    float f32_;
    const void *reference_;
    Payload() : u64_(0) {}
  } payload_;
  uint32_t length_;
  State state_;
  uint8_t reserved_;
  uint16_t flags_;

  void clear(State state)
  {
    payload_.u64_ = 0;
    length_ = 0;
    state_ = state;
    reserved_ = 0;
    flags_ = 0;
  }
};

static_assert(sizeof(Datum) == 16, "Execution Datum must remain 16 bytes");
static_assert(alignof(Datum) == 8, "Inline 64-bit scalars require 8-byte alignment");
static_assert(std::is_standard_layout<Datum>::value, "Datum needs a stable in-memory layout");
static_assert(std::is_trivially_copyable<Datum>::value, "Datum copying must not acquire ownership");

} // namespace common
} // namespace oceanbase
