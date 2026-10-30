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

#include "common/object/ob_type_metadata.h"
#include "common/enumset/enumset_value.h"
#include "common/datum/parameter_flags.h"

namespace oceanbase
{
namespace common
{

OB_SERIALIZE_MEMBER(ObDataType, meta_, accuracy_, is_zero_fill_);
OB_SERIALIZE_MEMBER(ObEnumSetInnerValue, numberic_value_, string_value_);

DEFINE_SERIALIZE(SqlTypeMeta)
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(type_);
  OB_UNIS_ENCODE(cs_level_);
  OB_UNIS_ENCODE(cs_type_);
  OB_UNIS_ENCODE(scale_);
  return ret;
}

DEFINE_DESERIALIZE(SqlTypeMeta)
{
  int ret = OB_SUCCESS;
  OB_UNIS_DECODE(type_);
  OB_UNIS_DECODE(cs_level_);
  OB_UNIS_DECODE(cs_type_);
  OB_UNIS_DECODE(scale_);
  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(SqlTypeMeta)
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN(type_);
  OB_UNIS_ADD_LEN(cs_level_);
  OB_UNIS_ADD_LEN(cs_type_);
  OB_UNIS_ADD_LEN(scale_);
  return len;
}



OB_SERIALIZE_MEMBER(ParamFlag, flag_);

void ParamFlag::reset()
{
  need_to_check_type_ = true;
  need_to_check_bool_value_ = false;
  expected_bool_value_ = false;
  need_to_check_extend_type_ = true;
  reserved_legacy_bit_ = false;
  is_pl_mock_default_param_ = false;
  is_boolean_ = false;
  is_batch_parameter_ = false;
  ignore_scale_check_ = false;
}

} // namespace common
} // namespace oceanbase
