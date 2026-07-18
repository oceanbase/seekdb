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

#ifndef OCEANBASE_SQL_OB_PHY_OPERATOR_STATS_H
#define OCEANBASE_SQL_OB_PHY_OPERATOR_STATS_H
#include "share/ob_define.h"
#include "lib/atomic/ob_atomic.h"
#include "lib/utility/ob_macro_utils.h"
namespace oceanbase
{
namespace common
{
class ObIAllocator;
}
namespace sql
{
class ObPhyOperatorMonitorInfo;
struct ObOperatorStat;
class ObPhysicalPlan;
namespace StatId
{
enum StatId{
  INPUT_ROWS = 0,
  RESCAN_TIMES,
  OUTPUT_ROWS,
  MAX_STAT
};
}
class ObPhyOperatorStats
{
public:
  friend class TestPhyOperatorStats_init_Test;
  friend class TestPhyOperatorStats_test_add_Test;
  friend class TestPhyOperatorStats_test_accumulation_Test;
  ObPhyOperatorStats() : op_stats_array_(NULL),
    op_count_(0), array_size_(0), execution_times_(0)
  {}
  ~ObPhyOperatorStats() {}
  int init(common::ObIAllocator *alloc, int64_t op_count);
  int add_op_stat(ObPhyOperatorMonitorInfo &info);
  int get_op_stat_accumulation(ObPhysicalPlan *plan, int64_t op_id, ObOperatorStat &stat);
  static const int64_t COPY_COUNT = 10;
  int64_t count() { return op_count_; }
  void inc_execution_times() { ATOMIC_INC(&execution_times_); }
  int64_t get_execution_times() const { return execution_times_; }
private:
  DISALLOW_COPY_AND_ASSIGN(ObPhyOperatorStats);
  int64_t *op_stats_array_;
  int64_t op_count_;
  int64_t array_size_;
  int64_t execution_times_;
};
} //namespace sql
} //namespace oceanbase
#endif
