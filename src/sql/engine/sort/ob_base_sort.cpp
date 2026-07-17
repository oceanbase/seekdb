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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/sort/ob_base_sort.h"

using namespace oceanbase::sql;
using namespace oceanbase::common;

DEFINE_SERIALIZE(ObSortColumn)
{
  int ret = OB_SUCCESS;
  int32_t cs_type_int = static_cast<int32_t>(cs_type_);
  OB_UNIS_ENCODE(index_);
  OB_UNIS_ENCODE(cs_type_int);
  bool is_asc = true;
  if ((extra_info_ & ObSortColumnExtra::SORT_COL_ASC_BIT) > 0) {
    is_asc = true;
  } else {
    is_asc = false;
  }
  if ((extra_info_ & ObSortColumnExtra::SORT_COL_EXTRA_BIT) > 0) {
    OB_UNIS_ENCODE(extra_info_);
    BASE_SER((ObSortColumn, ObSortColumnExtra));
  } else {
    OB_UNIS_ENCODE(is_asc);
  }
  return ret;
}

DEFINE_DESERIALIZE(ObSortColumn)
{
  int ret = OB_SUCCESS;
  int32_t cs_type_int = 0;
  OB_UNIS_DECODE(index_);
  OB_UNIS_DECODE(cs_type_int);
  cs_type_ = static_cast<ObCollationType>(cs_type_int);
  uint8_t extra_char = 0;
  OB_UNIS_DECODE(extra_char);

  if (OB_SUCC(ret)) {
    extra_info_ = extra_char;
    if ((extra_char & ObSortColumnExtra::SORT_COL_EXTRA_BIT) > 0) {
      BASE_DESER((ObSortColumn, ObSortColumnExtra));
    } else {
      // do nothing
    }
  }

  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(ObSortColumn)
{
  int64_t len = 0;
  int32_t cs_type_int = static_cast<int32_t>(cs_type_);
  OB_UNIS_ADD_LEN(index_);
  OB_UNIS_ADD_LEN(cs_type_int);   // for cs_type_.

  OB_UNIS_ADD_LEN(extra_info_);

  if ((extra_info_ & ObSortColumnExtra::SORT_COL_EXTRA_BIT) > 0) {
    BASE_ADD_LEN((ObSortColumn, ObSortColumnExtra));
  }
  return len;
}

OB_SERIALIZE_MEMBER(ObSortColumnExtra, obj_type_, order_type_);
