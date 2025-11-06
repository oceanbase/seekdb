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

#ifndef OB_LOG_TRACING_H_
#define OB_LOG_TRACING_H_

#include "sql/optimizer/ob_logical_operator.h"

namespace oceanbase
{
namespace sql
{

class ObLogMonitoringDump : public ObLogicalOperator
{
public:
  ObLogMonitoringDump(ObLogPlan &plan) :
  ObLogicalOperator(plan), flags_(0), dst_op_line_id_(0)
  { }
  virtual ~ObLogMonitoringDump() = default;
  const char *get_name() const;
  inline void set_flags(uint64_t flags) { flags_ = flags; }
  uint64_t get_flags() { return flags_; }
  void set_dst_op_id(uint64_t dst_op_id) { dst_op_line_id_ = dst_op_id; }
  uint64_t get_dst_op_id() { return dst_op_line_id_; }
  virtual int est_cost() override;
private:
  uint64_t flags_;
  // Here the id is just the operator's line id, which is the line number seen in explain
  uint64_t dst_op_line_id_;
  DISALLOW_COPY_AND_ASSIGN(ObLogMonitoringDump);
};

}
}

#endif
