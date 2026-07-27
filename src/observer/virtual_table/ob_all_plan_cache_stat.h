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

#ifndef _OB_ALL_PLAN_CACHE_STAT_H_
#define _OB_ALL_PLAN_CACHE_STAT_H_

#include "share/ob_define.h"
#include "lib/net/ob_addr.h"
#include "lib/container/ob_se_array.h"

#include "observer/virtual_table/ob_virtual_table_iterator.h"
#include "sql/ob_scanner.h"
#include "common/row/ob_row.h"

#include "sql/plan_cache/ob_plan_cache_util.h"
namespace oceanbase
{
namespace sql
{
class ObPlanCacheValue;
class ObPlanCacheRlockAndRef;
class ObPlanCache;
class ObPlanStat;
} // end of namespace sql

namespace observer
{

class ObAllPlanCacheBase : public common::ObVirtualTableIterator
{
public:
  ObAllPlanCacheBase();
  virtual ~ObAllPlanCacheBase();
  int inner_get_next_row(common::ObNewRow *&row);
  void reset();
  // deriative class specific
  virtual int inner_get_next_row() = 0;
protected:
  // Single-shot iteration guard
  bool iter_end_;
  DISALLOW_COPY_AND_ASSIGN(ObAllPlanCacheBase);
};

class ObAllPlanCacheStat : public ObAllPlanCacheBase
{
public:
  ObAllPlanCacheStat() {}
  virtual ~ObAllPlanCacheStat() {}
  int inner_get_next_row() { return get_row(); }
protected:
  int get_row();
  int fill_cells(sql::ObPlanCache &plan_cache);
private:
  enum
  {
        SQL_NUM = common::OB_APP_MIN_COLUMN_ID,
    MEM_USED,
    MEM_HOLD,
    ACCESS_COUNT,
    HIT_COUNT,
    HIT_RATE,
    PLAN_NUM,
    MEM_LIMIT,
    HASH_BUCKET
  };
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllPlanCacheStat);
}; // end of class ObAllPlanCacheStat

} // end of namespace observer
} // end of namespace oceanbase
#endif /* _OB_ALL_PLAN_CACHE_STAT_H_ */
