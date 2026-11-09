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

namespace oceanbase {
namespace common {
class ObILobCursor;
namespace json {

// Borrows a stable cursor for one synchronous native call. A full-mode view
// remains valid until that call returns; native writes are staged separately.
class BinarySource {
public:
  explicit BinarySource(const ObILobCursor &cursor) : cursor_(cursor) {}
  uint64_t binary_length() const noexcept;
  rust::Slice<const uint8_t> binary_view() const noexcept;
  int32_t binary_read(uint64_t offset, rust::Slice<uint8_t> output) const noexcept;

private:
  const ObILobCursor &cursor_;
};

} // namespace json
} // namespace common
} // namespace oceanbase
