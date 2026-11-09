// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once
#include "rust/cxx.h"

namespace oceanbase { namespace common { namespace json {
struct HashResult;
class HashCapability {
public:
  using Algorithm = uint64_t (*)(const void *, uint64_t, uint64_t);
  explicit HashCapability(Algorithm algorithm) : algorithm_(algorithm) {}
  uint64_t append_bytes(uint64_t seed, rust::Slice<const uint8_t> input) const noexcept;
  HashResult hash_database_scalar(uint64_t seed, uint8_t kind, int64_t integer,
      uint64_t unsigned_value, double real, rust::Slice<const uint8_t> payload) const;
private:
  Algorithm algorithm_;
};
struct FormatResult;
struct OrderResult;
FormatResult format_database_scalar(rust::Slice<const uint8_t> input, bool quoted);
OrderResult compare_temporal(rust::Slice<const uint8_t> left, rust::Slice<const uint8_t> right);
}}}
