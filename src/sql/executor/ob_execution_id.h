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

#ifndef OCEANBASE_SQL_EXECUTOR_OB_EXECUTION_ID_
#define OCEANBASE_SQL_EXECUTOR_OB_EXECUTION_ID_

#include "lib/hash/ob_hashutils.h"

namespace oceanbase
{
namespace sql
{
// execution_type, no more than 255.
static const uint64_t ET_DIST_TASK = 0;   // dist task must be 0 for compatibility.
static const uint64_t ET_MINI_TASK = 1;

class ObExecutionID final
{
  OB_UNIS_VERSION(1);
public:
public:
  explicit ObExecutionID(uint64_t execution_id)
    : execution_id_(execution_id),
      task_type_(ET_DIST_TASK)
  {}
  ObExecutionID()
    : execution_id_(common::OB_INVALID_ID),
      task_type_(ET_DIST_TASK)
  {}

  inline void set_execution_id(const uint64_t execution_id) { execution_id_ = execution_id; }
  inline void set_task_type(uint64_t task_type) { task_type_ = task_type; }
  inline void set_dist_task_type() { task_type_ = ET_DIST_TASK; }
  inline void set_mini_task_type() { task_type_ = ET_MINI_TASK; }
  inline uint64_t get_execution_id() const { return execution_id_; }
  inline uint64_t get_task_type() const { return task_type_; }
  inline bool is_dist_task_type() const { return task_type_ == ET_DIST_TASK; }
  inline bool is_mini_task_type() const { return task_type_ == ET_MINI_TASK; }

  inline bool equal(const ObExecutionID &id) const
  {
    return id.execution_id_ == execution_id_
        && id.task_type_ == task_type_;
  }
  inline int64_t hash() const
  {
    return common::murmurhash(&execution_id_, sizeof(execution_id_), 0);
  }
  inline bool operator==(const ObExecutionID &id) const
  {
    return equal(id);
  }
  inline bool is_valid() const
  {
    return common::OB_INVALID_ID != execution_id_;
  }
  inline void reset()
  {
    execution_id_ = common::OB_INVALID_ID;
  }
  // Log the hash value for easy association of execution logic at various stages when troubleshooting
  TO_STRING_KV(N_EXECUTION_ID, execution_id_,
               N_TASK_TYPE, task_type_,
               "hash", static_cast<uint64_t>(hash()));
  DECLARE_TO_YSON_KV;

private:
  uint64_t execution_id_;
  union
  {
    uint64_t execution_flag_;
    struct {
      uint64_t task_type_:8;
      uint64_t reserved_:56;
    };
  };
};
}
}
#endif /* OCEANBASE_SQL_EXECUTOR_OB_EXECUTION_ID_ */
