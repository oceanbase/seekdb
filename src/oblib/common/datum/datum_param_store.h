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

#include "common/datum/datum_parameter.h"
#include "lib/container/ob_2d_array.h"
#include "lib/allocator/ob_allocator.h"

namespace oceanbase
{
namespace common
{

// The native store is independent of SQL expression declarations and legacy
// values. Payload bytes remain owned by the arena supplied to each parameter.
using DatumParamStore = Ob2DArray<DatumParameter,
    OB_MALLOC_BIG_BLOCK_SIZE, ObWrapperAllocator, false,
    ObSEArray<DatumParameter *, 1, ObWrapperAllocator, false>>;

// Canonical parameter storage throughout SQL/PL and protocol inputs.
using ParamStore = DatumParamStore;

// These specializations are visible before the generic container serializer is
// instantiated, regardless of header include order. Native payloads are decoded
// with the store's allocator, not through an intermediate legacy value.
template <> int DatumParamStore::serialize(char *buf, int64_t length, int64_t &pos) const;
template <> int DatumParamStore::deserialize(const char *buf, int64_t length, int64_t &pos);
template <> int64_t DatumParamStore::get_serialize_size() const;

} // namespace common
} // namespace oceanbase
