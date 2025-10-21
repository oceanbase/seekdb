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

#ifndef _OB_ALL_VIRTUAL_PS_STAT_H
#define _OB_ALL_VIRTUAL_PS_STAT_H 1

#include "observer/virtual_table/ob_all_plan_cache_stat.h"

namespace oceanbase
{
namespace sql
{
class ObPsCache;
}
namespace observer
{

class ObAllVirtualPsStat : public ObAllPlanCacheBase
{
public:
  ObAllVirtualPsStat() : ObAllPlanCacheBase() {}
  virtual ~ObAllVirtualPsStat() {}

  virtual int inner_get_next_row() override;
  virtual int inner_open() override;
private:
  int fill_cells(sql::ObPsCache &ps_cache, uint64_t tenant_id);
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualPsStat);
};

}
}

#endif /* _OB_ALL_VIRTUAL_PS_STAT_H */
