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

#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace common
{

struct ParamFlag
{
  ParamFlag() : need_to_check_type_(true),
                need_to_check_bool_value_(false),
                expected_bool_value_(false),
                need_to_check_extend_type_(true),
                reserved_legacy_bit_(false),
                is_pl_mock_default_param_(false),
                is_boolean_(false),
                is_batch_parameter_(0),
                ignore_scale_check_(false),
                reserved_(0)
  { }
  TO_STRING_KV(K_(need_to_check_type), K_(need_to_check_bool_value),
               K_(expected_bool_value), K_(is_pl_mock_default_param), K_(need_to_check_extend_type), K_(is_batch_parameter),
               K_(ignore_scale_check));
  void reset();

  static uint32_t flag_offset_bits() { return offsetof(ParamFlag, flag_) * 8; }

  union
  {
    uint8_t flag_;
    struct {
      uint8_t need_to_check_type_: 1; //TRUE if the type need to be checked by plan cache, FALSE otherwise
      uint8_t need_to_check_bool_value_ : 1;//TRUE if the bool value need to be checked by plan cache, FALSE otherwise
      uint8_t expected_bool_value_ : 1;//bool value, effective only when need_to_check_bool_value_ is true
      uint8_t need_to_check_extend_type_ : 1; // True if the extended type needs to be checked
      uint8_t reserved_legacy_bit_ : 1;
      uint8_t is_pl_mock_default_param_ : 1; // TRUE if DatumParameter is pl default param
      uint8_t is_boolean_ : 1; // to distinguish T_BOOL and T_TINYINT
      uint8_t is_batch_parameter_ : 1; // indicates it is a batch parameter
    };
  };

  union
  {
    uint8_t extend_flag_;
    struct {
      uint8_t ignore_scale_check_ : 1; //TRUE if plan cache can reuse by different scale numbers, FALSE otherwise
      uint8_t reserved_ : 7;
    };
  };
  OB_UNIS_VERSION_V(1);
};

} // namespace common
} // namespace oceanbase
