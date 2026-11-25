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

#include "rust/json/include/schema_pattern.h"
#include <regex>
#include <string>

namespace oceanbase { namespace common { namespace json {
struct SchemaPattern::Impl
{
  explicit Impl(rust::Slice<const uint8_t> pattern)
    : pattern_(reinterpret_cast<const char *>(pattern.data()), pattern.size()) {}
  std::regex pattern_;
};
SchemaPattern::SchemaPattern(rust::Slice<const uint8_t> pattern)
  : impl_(std::make_unique<Impl>(pattern)) {}
SchemaPattern::~SchemaPattern() = default;

bool SchemaPattern::matches(rust::Slice<const uint8_t> text) const
{
  // The existing schema matcher uses the NUL-terminated regex_search overload.
  const std::string value(reinterpret_cast<const char *>(text.data()), text.size());
  return std::regex_search(value.c_str(), impl_->pattern_);
}

std::unique_ptr<SchemaPattern> compile_schema_pattern(rust::Slice<const uint8_t> pattern)
{
  try {
    return std::make_unique<SchemaPattern>(pattern);
  } catch (const std::regex_error &) {
    return nullptr;
  }
}
} } } // namespace oceanbase::common::json
