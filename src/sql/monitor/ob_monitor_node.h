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

#ifndef __OB_SQL_MONITOR_NODE_H__
#define __OB_SQL_MONITOR_NODE_H__

#include "lib/profile/ob_trace_id.h"
#include "lib/string/ob_string.h"
#include "lib/time/ob_time_utility.h"
#include "share/diagnosis/ob_sql_monitor_statname.h"
#include "sql/engine/ob_phy_operator_type.h"

namespace oceanbase
{
namespace sql
{
class ObOperator;

class TimingGuard
{
public:
  explicit TimingGuard(int64_t &v) :
      v_(v),
      begin_(common::ObTimeUtility::fast_current_time())
  {
  }
  ~TimingGuard()
  {
    v_ =  v_ + common::ObTimeUtility::fast_current_time() - begin_;
  }
private:
  int64_t &v_;
  int64_t begin_;
};

class ObMonitorNode
{
  typedef common::ObCurTraceId::TraceId TraceId;
public:
  ObMonitorNode() :
      op_id_(0),
      plan_depth_(0),
      output_batches_(0),
      skipped_rows_count_(0),
      op_type_(PHY_INVALID),
      op_(nullptr),
      open_time_(0),
      first_row_time_(0),
      last_row_time_(0),
      close_time_(0),
      rescan_times_(0),
      output_row_count_(0),
      db_time_(0),
      block_time_(0),
      disk_read_count_(0),
      otherstat_1_value_(0),
      otherstat_2_value_(0),
      otherstat_3_value_(0),
      otherstat_4_value_(0),
      otherstat_5_value_(0),
      otherstat_6_value_(0),
      otherstat_7_value_(0),
      otherstat_8_value_(0),
      otherstat_9_value_(0),
      otherstat_10_value_(0),
      otherstat_1_id_(0),
      otherstat_2_id_(0),
      otherstat_3_id_(0),
      otherstat_4_id_(0),
      otherstat_5_id_(0),
      otherstat_6_id_(0),
      otherstat_7_id_(0),
      otherstat_8_id_(0),
      otherstat_9_id_(0),
      otherstat_10_id_(0),
      workarea_mem_(0),
      workarea_max_mem_(0),
      workarea_tempseg_(0),
      workarea_max_tempseg_(0),
      plan_hash_value_(common::OB_INVALID_ID)
  {
    TraceId* trace_id = common::ObCurTraceId::get_trace_id();
    if (NULL != trace_id) {
      trace_id_ = *trace_id;
    }
    thread_id_ = GETTID();
    sql_id_[0] = '\0';
  }
  explicit ObMonitorNode(const ObMonitorNode &that) = default;
  ~ObMonitorNode() = default;
  int assign(const ObMonitorNode &that)
  {
    *this = that;
    return common::OB_SUCCESS;
  }
  void set_op(ObOperator *op) { op_ = op; }
  void set_operator_type(ObPhyOperatorType type) { op_type_ = type; }
  void set_operator_id(int64_t op_id) { op_id_ = op_id; }
  void set_plan_depth(int64_t plan_depth) { plan_depth_ = plan_depth; }
  const char *get_operator_name() const { return get_phy_op_name(op_type_); }
  ObPhyOperatorType get_operator_type() const { return op_type_; }
  int64_t get_op_id() const { return op_id_; }
  const TraceId& get_trace_id() const { return trace_id_; }
  int64_t get_thread_id() { return thread_id_; }
  void update_memory(int64_t delta_size);
  void update_tempseg(int64_t delta_size);
  uint64_t calc_db_time();
  void covert_to_static_node();
  int set_sql_id(const ObString &sql_id);
  void set_plan_hash_value(uint64_t plan_hash_value) { plan_hash_value_ = plan_hash_value; }
  TO_STRING_KV(K_(op_id), "op_name", get_operator_name(), K_(thread_id));
public:
  int64_t op_id_;
  int64_t plan_depth_;
  int64_t output_batches_;
  int64_t skipped_rows_count_;
  ObPhyOperatorType op_type_;
  ObOperator *op_;
private:
  int64_t thread_id_;
  TraceId trace_id_;
public:
  int64_t open_time_;
  int64_t first_row_time_;
  int64_t last_row_time_;
  int64_t close_time_;
  int64_t rescan_times_;
  int64_t output_row_count_;
  uint64_t db_time_;
  uint64_t block_time_;
  int64_t disk_read_count_;
  int64_t otherstat_1_value_;
  int64_t otherstat_2_value_;
  int64_t otherstat_3_value_;
  int64_t otherstat_4_value_;
  int64_t otherstat_5_value_;
  int64_t otherstat_6_value_;
  int64_t otherstat_7_value_;
  int64_t otherstat_8_value_;
  int64_t otherstat_9_value_;
  int64_t otherstat_10_value_;
  int16_t otherstat_1_id_;
  int16_t otherstat_2_id_;
  int16_t otherstat_3_id_;
  int16_t otherstat_4_id_;
  int16_t otherstat_5_id_;
  int16_t otherstat_6_id_;
  int16_t otherstat_7_id_;
  int16_t otherstat_8_id_;
  int16_t otherstat_9_id_;
  int16_t otherstat_10_id_;
  int64_t workarea_mem_;
  int64_t workarea_max_mem_;
  int64_t workarea_tempseg_;
  int64_t workarea_max_tempseg_;
  char sql_id_[common::OB_MAX_SQL_ID_LENGTH + 1];
  uint64_t plan_hash_value_;
};

}
}
#endif
