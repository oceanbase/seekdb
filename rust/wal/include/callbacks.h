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

namespace oceanbase {
namespace wal {
namespace wal_rs {

void publish_flush_ffi(uint8_t *context, const uint64_t end_lsn, const uint64_t end_scn);
void report_fatal_ffi(rust::Str path, rust::Str operation, const int32_t raw_os_error,
    const bool no_space);

} // namespace wal_rs
} // namespace wal
} // namespace oceanbase
