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

#include <cstddef>
#include <cstdint>

namespace oceanbase {
namespace common {

struct XmlAttributeView;

int32_t xml_on_start_document(uint8_t* context);
int32_t xml_on_end_document(uint8_t* context);
int32_t xml_on_declaration(uint8_t* context, const uint8_t* value, size_t value_len);
int32_t xml_on_start_element(uint8_t* context,
                             const uint8_t* name,
                             size_t name_len,
                             const XmlAttributeView* attributes,
                             size_t attribute_count,
                             bool empty);
int32_t xml_on_end_element(uint8_t* context, bool empty);
int32_t xml_on_text(uint8_t* context, const uint8_t* value, size_t value_len);
int32_t xml_on_cdata(uint8_t* context, const uint8_t* value, size_t value_len);
int32_t xml_on_comment(uint8_t* context, const uint8_t* value, size_t value_len);
int32_t xml_on_processing_instruction(uint8_t* context,
                                      const uint8_t* target,
                                      size_t target_len,
                                      const uint8_t* value,
                                      size_t value_len);

} // namespace common
} // namespace oceanbase
