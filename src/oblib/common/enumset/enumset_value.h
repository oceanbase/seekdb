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

#include "lib/string/ob_string.h"
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace common
{

struct ObEnumSetInnerValue
{
  OB_UNIS_VERSION_V(1);
public:
  ObEnumSetInnerValue() : numberic_value_(0), string_value_() {}
  ObEnumSetInnerValue(uint64_t numberic_value, common::ObString &string_value)
      : numberic_value_(numberic_value), string_value_(string_value) {}
  virtual ~ObEnumSetInnerValue() {}

  TO_STRING_KV(K_(numberic_value), K_(string_value));
  uint64_t numberic_value_;
  common::ObString string_value_;
};

} // namespace common
} // namespace oceanbase
