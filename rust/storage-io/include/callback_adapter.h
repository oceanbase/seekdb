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
#include <cstdint>
#include <memory>

namespace oceanbase {
namespace common { class ObIOCallback; }
namespace blocksstable {
class ObMicroBlockCacheValue;
struct ObMultiBlockIOResult;
struct ObMicroBlockData;
}
namespace storage_io {

class CallbackAdapter final {
public:
  ~CallbackAdapter();
  int32_t process(rust::Slice<const uint8_t> input);
  const char *data() const;
  const blocksstable::ObMicroBlockCacheValue *micro_block() const;
  const blocksstable::ObMultiBlockIOResult *micro_blocks() const;
  const blocksstable::ObMicroBlockData *decoded_micro_block() const;
private:
  friend std::unique_ptr<CallbackAdapter> adopt_callback(common::ObIOCallback *callback) noexcept;
  explicit CallbackAdapter(common::ObIOCallback *callback) noexcept : callback_(callback) {}
  common::ObIOCallback *callback_;
};

std::unique_ptr<CallbackAdapter> adopt_callback(common::ObIOCallback *callback) noexcept;

} // namespace storage_io
} // namespace oceanbase
