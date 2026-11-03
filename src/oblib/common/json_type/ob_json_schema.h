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
#include "ob_json_base.h"

namespace oceanbase {
namespace common {

int compile_json_schema(ObIAllocator &allocator, const ObString &input,
                        ObJsonInType input_type, uint32_t max_depth, ObString &output);

// Execution-owned native schema cache. Its destructor releases compiled regexes.
class ObJsonSchemaCache
{
public:
  explicit ObJsonSchemaCache(ObIAllocator *allocator) : allocator_(allocator), state_(nullptr) {}
  ~ObJsonSchemaCache();
  int prepare(const ObString &input, int slot, ObJsonInType input_type, bool normalized = false);
  int validate(const ObString &input, int slot, bool with_report, bool &valid,
               ObString &report, ObIAllocator &allocator) const;
private:
  struct State;
  ObIAllocator *allocator_;
  State *state_;
  DISALLOW_COPY_AND_ASSIGN(ObJsonSchemaCache);
};

} // namespace common
} // namespace oceanbase
