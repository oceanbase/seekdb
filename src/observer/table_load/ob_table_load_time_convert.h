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

#include "common/object/ob_object.h"
#include "lib/container/ob_se_array.h"
#include "lib/ob_define.h"
#include "lib/timezone/ob_oracle_format_models.h"
#include "lib/timezone/ob_time_convert.h"
#include "share/object/ob_obj_cast.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadTimeConverter
{
public:
  ObTableLoadTimeConverter();
  ~ObTableLoadTimeConverter();
  int init(const ObString &format);
  int str_to_datetime_oracle(const common::ObString &str, const common::ObTimeConvertCtx &cvrt_ctx,
                             common::ObDateTime &value) const;

private:
  int str_to_ob_time(const common::ObString &str, const common::ObTimeConvertCtx &cvrt_ctx,
                     const common::ObObjType target_type, common::ObTime &ob_time,
                     common::ObScale &scale) const;

private:
  common::ObSEArray<common::ObDFMElem, common::ObDFMUtil::COMMON_ELEMENT_NUMBER> dfm_elems_;
  common::ObFixedBitSet<OB_DEFAULT_BITSET_SIZE_FOR_DFM> elem_flags_;
  bool is_inited_;
};

} // namespace observer
} // namespace oceanbase
