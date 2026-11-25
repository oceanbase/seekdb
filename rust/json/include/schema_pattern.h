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
#include <memory>

namespace oceanbase { namespace common { namespace json {
class SchemaPattern
{
public:
  explicit SchemaPattern(rust::Slice<const uint8_t> pattern);
  ~SchemaPattern();
  bool matches(rust::Slice<const uint8_t> text) const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
std::unique_ptr<SchemaPattern> compile_schema_pattern(rust::Slice<const uint8_t> pattern);
} } } // namespace oceanbase::common::json
