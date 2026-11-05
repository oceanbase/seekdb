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

#define USING_LOG_PREFIX CLIENT

#include "ob_table_load_dml_stat.h"

namespace oceanbase
{
namespace table
{

OB_DEF_SERIALIZE(ObTableLoadDmlStat)
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(dml_stat_array_.count());
  for (int64_t i = 0; OB_SUCC(ret) && i < dml_stat_array_.count(); i++) {
    ObOptDmlStat *dml_stat = dml_stat_array_.at(i);
    if (OB_ISNULL(dml_stat)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected dml stat is null", KR(ret));
    } else {
      OB_UNIS_ENCODE(*dml_stat);
    }
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObTableLoadDmlStat)
{
  int ret = OB_SUCCESS;
  int64_t size = 0;
  reset();
  OB_UNIS_DECODE(size);
  for (int64_t i = 0; OB_SUCC(ret) && i < size; ++i) {
    ObOptDmlStat *dml_stat = nullptr;
    if (OB_FAIL(allocate_dml_stat(dml_stat))) {
      LOG_WARN("fail to allocate dml stat", KR(ret));
    } else {
      OB_UNIS_DECODE(*dml_stat);
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObTableLoadDmlStat)
{
  int ret = OB_SUCCESS;
  int64_t len = 0;
  OB_UNIS_ADD_LEN(dml_stat_array_.count());
  for (int64_t i = 0; OB_SUCC(ret) && i < dml_stat_array_.count(); i++) {
    ObOptDmlStat *dml_stat = dml_stat_array_.at(i);
    if (OB_ISNULL(dml_stat)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected dml stat is null", KR(ret));
    } else {
      OB_UNIS_ADD_LEN(*dml_stat);
    }
  }
  return len;
}

} // namespace table
} // namespace oceanbase
