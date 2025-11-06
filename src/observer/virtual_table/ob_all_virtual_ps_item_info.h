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

#ifndef _OB_ALL_VIRTUAL_PS_ITEM_INFO_H
#define _OB_ALL_VIRTUAL_PS_ITEM_INFO_H 1

#include "observer/virtual_table/ob_all_plan_cache_stat.h"
#include "sql/plan_cache/ob_prepare_stmt_struct.h"

namespace oceanbase
{
using common::ObPsStmtId;
namespace observer
{

class ObAllVirtualPsItemInfo: public ObAllPlanCacheBase
{
public:
  ObAllVirtualPsItemInfo()
    : ObAllPlanCacheBase(),
      stmt_id_array_idx_(OB_INVALID_ID),
      stmt_id_array_(),
      ps_cache_(NULL) {}
  virtual ~ObAllVirtualPsItemInfo() {}

  virtual int inner_get_next_row() override;
  virtual int inner_open() override;
  virtual void reset() override;
private:
  int fill_cells(uint64_t tenant_id,
                 ObPsStmtId stmt_id,
                 sql::ObPsStmtItem *stmt_item,
                 sql::ObPsStmtInfo *stmt_info);
  int get_next_row_from_specified_tenant(uint64_t tenant_id, bool &is_end);
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualPsItemInfo);

private:
  int64_t stmt_id_array_idx_;
  common::ObSEArray<ObPsStmtId, 1024> stmt_id_array_;
  sql::ObPsCache *ps_cache_;
};

}
}

#endif /* _OB_ALL_VIRTUAL_PS_ITEM_INFO_H */
