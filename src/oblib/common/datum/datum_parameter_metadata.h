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

#include "common/datum/parameter_flags.h"
#include "common/object/ob_type_metadata.h"
#include "common/ob_accuracy.h"
#include <type_traits>

namespace oceanbase
{
namespace common
{

// Parameter attributes without a value or a payload owner. The physical value
// type and declared parameter type remain independent, including for NULL.
// Member order minimizes padding; serialization keeps its existing explicit
// field order and must never copy this structure's object representation.
struct alignas(8) DatumParameterMetadata
{
  DatumParameterMetadata()
    : accuracy_(), type_meta_(), res_flags_(0), raw_text_pos_(-1),
      raw_text_len_(-1), param_meta_(), flag_()
  {}

  ObAccuracy accuracy_; // Preserve the full word, including PL declaration ids.
  SqlTypeMeta type_meta_;
  uint32_t res_flags_;
  int32_t raw_text_pos_;
  int32_t raw_text_len_;
  SqlTypeMeta param_meta_;
  ParamFlag flag_;

  TO_STRING_KV(K_(accuracy), K_(type_meta), K_(res_flags), K_(raw_text_pos),
               K_(raw_text_len), K_(param_meta), K_(flag));
};

static_assert(std::is_standard_layout<DatumParameterMetadata>::value,
              "Parameter metadata offsets must use a standard-layout type");
static_assert(sizeof(DatumParameterMetadata) == 32,
              "Parameter metadata must not acquire a value or hidden padding");
static_assert(alignof(DatumParameterMetadata) == 8,
              "Parameter accuracy requires eight-byte alignment");

} // namespace common
} // namespace oceanbase
