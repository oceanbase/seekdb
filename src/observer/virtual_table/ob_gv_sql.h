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

#ifndef _OB_GV_SQL_H
#define _OB_GV_SQL_H 1

#include "observer/virtual_table/ob_all_plan_cache_stat.h"
#include "sql/plan_cache/ob_cache_object.h"

namespace oceanbase
{
namespace observer
{

class ObGVSql : public ObAllPlanCacheBase
{
public:
  ObGVSql();
  virtual ~ObGVSql();
  void reset();
  virtual int inner_open();
  int inner_get_next_row() { return get_row_from_tenants(); }
protected:
  int get_row_from_tenants();
  int fill_cells(const sql::ObILibCacheObject *cache_obj, const sql::ObPlanCache &plan_cache);
  int get_row_from_specified_tenant(uint64_t tenant_id, bool &is_end);
private:
  common::ObSEArray<uint64_t, 1024> plan_id_array_;
  int64_t plan_id_array_idx_;
  sql::ObPlanCache *plan_cache_;
  DISALLOW_COPY_AND_ASSIGN(ObGVSql);
};

}
}

#endif /* _OB_GV_SQL_H */


