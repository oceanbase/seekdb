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

#define USING_LOG_PREFIX SERVER

#include "share/table/ob_table_load_row.h"

namespace oceanbase
{
namespace table
{
using namespace common;

OB_DEF_SERIALIZE_SIMPLE(ObTableLoadTabletObjRow)
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(tablet_id_);
  OB_UNIS_ENCODE(obj_row_);
  return ret;
}

OB_DEF_DESERIALIZE_SIMPLE(ObTableLoadTabletObjRow)
{
  int ret = OB_SUCCESS;
  OB_UNIS_DECODE(tablet_id_);
  OB_UNIS_DECODE(obj_row_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE_SIMPLE(ObTableLoadTabletObjRow)
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN(tablet_id_);
  OB_UNIS_ADD_LEN(obj_row_);
  return len;
}

} // namespace table
} // namespace oceanbase
