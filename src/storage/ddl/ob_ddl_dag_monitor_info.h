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

#ifndef OB_DDL_DAG_MONITOR_INFO_H_
#define OB_DDL_DAG_MONITOR_INFO_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/list/ob_dlink_node.h"
#include "lib/ob_errno.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace storage
{

class ObDDLDagMonitorInfo : public common::ObDLinkBase<ObDDLDagMonitorInfo>
{
public:
  enum TaskType : int32_t
  {
    TASK_TYPE_INVALID = -1,
    TASK_TYPE_SCAN,
    TASK_TYPE_WRITE_MACRO,
    TASK_TYPE_MERGE_SORT,
    TASK_TYPE_FTS_SAMPLE,
    TASK_TYPE_FTS_WRITE,
    TASK_TYPE_FTS_MERGE,
    TASK_TYPE_MAX
  };

  ObDDLDagMonitorInfo()
    : task_type_(TASK_TYPE_INVALID), create_ts_(0), finish_ts_(0),
      schedule_count_(0), accum_exec_time_us_(0), ret_code_(OB_SUCCESS)
  {
  }

  virtual ~ObDDLDagMonitorInfo() {}

  int init(TaskType task_type, int64_t create_ts)
  {
    int ret = OB_SUCCESS;
    task_type_ = task_type;
    create_ts_ = create_ts;
    return ret;
  }

  void on_schedule()
  {
    schedule_count_++;
  }

  void on_finish(int64_t finish_ts, int64_t exec_time_us, int ret_code)
  {
    finish_ts_ = finish_ts;
    accum_exec_time_us_ += exec_time_us;
    ret_code_ = ret_code;
  }

  OB_INLINE TaskType get_task_type() const { return task_type_; }
  OB_INLINE int64_t get_create_ts() const { return create_ts_; }
  OB_INLINE int64_t get_finish_ts() const { return finish_ts_; }
  OB_INLINE int32_t get_schedule_count() const { return schedule_count_; }
  OB_INLINE int64_t get_accum_exec_time_us() const { return accum_exec_time_us_; }
  OB_INLINE int get_ret_code() const { return ret_code_; }

  VIRTUAL_TO_STRING_KV(K_(task_type), K_(create_ts), K_(finish_ts), K_(schedule_count),
               K_(accum_exec_time_us), K_(ret_code));

private:
  TaskType task_type_;
  int64_t create_ts_;
  int64_t finish_ts_;
  int32_t schedule_count_;
  int64_t accum_exec_time_us_;
  int ret_code_;
};

class ObDDLDagMonitorFtsInfo : public ObDDLDagMonitorInfo
{
public:
  ObDDLDagMonitorFtsInfo()
    : ObDDLDagMonitorInfo(), doc_count_(0), token_count_(0)
  {
  }

  int set_fts_stats(int64_t doc_count, int64_t token_count)
  {
    doc_count_ = doc_count;
    token_count_ = token_count;
    return OB_SUCCESS;
  }

  OB_INLINE int64_t get_doc_count() const { return doc_count_; }
  OB_INLINE int64_t get_token_count() const { return token_count_; }

  INHERIT_TO_STRING_KV("base", ObDDLDagMonitorInfo, K_(doc_count), K_(token_count));

private:
  int64_t doc_count_;
  int64_t token_count_;
};

} // end namespace storage
} // end namespace oceanbase

#endif /* OB_DDL_DAG_MONITOR_INFO_H_ */
