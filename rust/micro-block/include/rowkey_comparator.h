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

#include <cstdint>
#include "rust/cxx.h"

namespace oceanbase::blocksstable::micro_block {

struct CellDesc;

class RowkeyComparator
{
public:
  using Compare = int32_t (*)(void *, rust::Slice<const CellDesc>,
                              rust::Slice<const uint8_t>, int32_t &) noexcept;

  RowkeyComparator(void *context, Compare compare) noexcept
    : context_(context), compare_(compare) {}

  // The caller owns the context. Rust calls synchronously on the calling
  // thread and the comparator must consume the scratch slices before returning.
  int32_t run(rust::Slice<const CellDesc> cells,
              rust::Slice<const uint8_t> decoded,
              int32_t &ordering) const noexcept
  {
    return compare_(context_, cells, decoded, ordering);
  }

private:
  void *context_;
  Compare compare_;
};

} // namespace oceanbase::blocksstable::micro_block
