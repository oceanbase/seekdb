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

#include "common/datum/datum.h"

namespace oceanbase
{
namespace common
{
class SqlType;

// A value and its complete, immutable declaration. This descriptor owns
// neither object: a retaining boundary must copy both into its actual owner.
// It is deliberately distinct from the legacy DatumValue/DatumParameter ABI.
struct NativeTypedDatum
{
  NativeTypedDatum() : datum_(), type_(nullptr) {}
  Datum datum_;
  const SqlType *type_;
  TO_STRING_KV(K_(datum), KP_(type));
};

static_assert(sizeof(NativeTypedDatum) == 24, "Native typed values are 24 bytes");
static_assert(std::is_standard_layout<NativeTypedDatum>::value, "Stable typed value layout");
static_assert(std::is_trivially_copyable<NativeTypedDatum>::value, "Copying borrows its owner");

} // namespace common
} // namespace oceanbase
