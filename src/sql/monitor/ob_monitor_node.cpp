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

#define USING_LOG_PREFIX SHARE
#include "sql/monitor/ob_monitor_node.h"
#include "sql/engine/ob_operator.h"

namespace oceanbase { namespace common { uint64_t lib_get_cpu_khz(); } }

using namespace oceanbase::common;
using namespace oceanbase::sql;

void ObMonitorNode::update_memory(int64_t delta_size)
{
  workarea_mem_ += delta_size;
  workarea_max_mem_ = MAX(workarea_mem_, workarea_max_mem_);
}

void ObMonitorNode::update_tempseg(int64_t delta_size)
{
  workarea_tempseg_ += delta_size;
  workarea_max_tempseg_ = MAX(workarea_tempseg_, workarea_max_tempseg_);
}

uint64_t ObMonitorNode::calc_db_time()
{
  int64_t db_time = 0;
  if (OB_NOT_NULL(op_)) {
    db_time = op_->total_time_;
    int64_t cur_time = rdtsc();
    if (op_->cpu_begin_level_ > 0) {
      db_time += cur_time - op_->cpu_begin_time_;
    }
    for (int32_t i = 0; i < op_->get_child_cnt(); ++i) {
      ObOperator *child_op = op_->get_child(i);
      if (OB_NOT_NULL(child_op)) {
        db_time -= child_op->total_time_;
        if (child_op->cpu_begin_level_ > 0) {
          db_time -= (cur_time - child_op->cpu_begin_time_);
        }
      } else {
        int ret = OB_ERR_UNEXPECTED;
        LOG_WARN("operator child is nullptr", K(ret), KPC(op_), K(i));
      }
    }
    if (db_time < 0) {
      db_time = 0;
    }
  }
  return static_cast<uint64_t>(db_time);
}

void ObMonitorNode::covert_to_static_node()
{
  db_time_ = calc_db_time();
  uint64_t cpu_khz = common::lib_get_cpu_khz();
  db_time_ = db_time_ * 1000 / cpu_khz;
  block_time_ = block_time_ * 1000 / cpu_khz;
  op_ = nullptr;
}

int ObMonitorNode::set_sql_id(const ObString &sql_id)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sql_id.ptr())) {
    sql_id_[0] = '\0';
  } else if (sql_id.length() > common::OB_MAX_SQL_ID_LENGTH) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql id length unexpected", K(ret), K(sql_id.length()));
  } else {
    MEMCPY(sql_id_, sql_id.ptr(), sql_id.length());
    sql_id_[sql_id.length()] = '\0';
  }
  return ret;
}
