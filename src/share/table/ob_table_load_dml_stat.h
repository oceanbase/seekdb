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

#include "share/stat/ob_stat_define.h"

namespace oceanbase
{
namespace common
{
class ObOptDmlStat;
} // namespace common
namespace table
{
struct ObTableLoadDmlStat
{
  OB_UNIS_VERSION(1);
public:
  ObTableLoadDmlStat() : allocator_("TLD_Dmlstat")
  {
    dml_stat_array_.set_tenant_id(MTL_ID());
    allocator_.set_tenant_id(MTL_ID());
  }
  ~ObTableLoadDmlStat() { reset(); }
  void reset()
  {
    for (int64_t i = 0; i < dml_stat_array_.count(); ++i) {
      ObOptDmlStat *col_stat = dml_stat_array_.at(i);
      if (col_stat != nullptr) {
        col_stat->~ObOptDmlStat();
      }
    }
    dml_stat_array_.reset();
    allocator_.reset();
  }
  bool is_empty() const { return dml_stat_array_.empty(); }
  int allocate_dml_stat(ObOptDmlStat *&dml_stat)
  {
    int ret = OB_SUCCESS;
    ObOptDmlStat *new_dml_stat = OB_NEWx(ObOptDmlStat, (&allocator_));
    if (OB_ISNULL(new_dml_stat)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      OB_LOG(WARN, "fail to allocate buffer", KR(ret));
    } else if (OB_FAIL(dml_stat_array_.push_back(new_dml_stat))) {
      OB_LOG(WARN, "fail to push back", KR(ret));
    } else {
      dml_stat = new_dml_stat;
    }
    if (OB_FAIL(ret)) {
      if (new_dml_stat != nullptr) {
        new_dml_stat->~ObOptDmlStat();
        allocator_.free(new_dml_stat);
        new_dml_stat = nullptr;
      }
    }
    return ret;
  }
  int merge(const ObTableLoadDmlStat &other)
  {
    int ret = OB_SUCCESS;
    for (int64_t i = 0; OB_SUCC(ret) && i < other.dml_stat_array_.count(); i++) {
      ObOptDmlStat *dml_stat = other.dml_stat_array_.at(i);
      ObOptDmlStat *new_dml_stat = nullptr;
      if (OB_ISNULL(dml_stat)) {
        ret = OB_ERR_UNEXPECTED;
        OB_LOG(WARN, "unexpected dml stat is null", KR(ret));
      } else if (OB_FAIL(allocate_dml_stat(new_dml_stat))) {
        OB_LOG(WARN, "fail to allocate dml stat", KR(ret));
      } else {
        *new_dml_stat = *dml_stat;
      }
    }    
    return ret;
  }
  TO_STRING_KV(K_(dml_stat_array));
public:
  common::ObArray<ObOptDmlStat *> dml_stat_array_;
  common::ObArenaAllocator allocator_;
};

} // namespace table
} // namespace oceanbase
