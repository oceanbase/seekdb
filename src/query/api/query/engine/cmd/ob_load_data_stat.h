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

#ifndef OCEANBASE_QUERY_API_ENGINE_CMD_OB_LOAD_DATA_STAT_H_
#define OCEANBASE_QUERY_API_ENGINE_CMD_OB_LOAD_DATA_STAT_H_

#include "share/ob_define.h"
#include "lib/allocator/page_arena.h"
#include "lib/atomic/ob_atomic.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace sql
{
struct ObLoadDataStat
{
  ObLoadDataStat() : allocator_(ObModIds::OB_SQL_LOAD_DATA),
                     ref_cnt_(0),
                     job_id_(0),
                     job_type_("normal"),
                     table_name_(),
                     file_path_(),
                     table_column_(0),
                     file_column_(0),
                     batch_size_(0),
                     parallel_(1),
                     load_mode_(0),
                     start_time_(0),
                     estimated_remaining_time_(0),
                     total_bytes_(0),
                     read_bytes_(0),
                     parsed_bytes_(0),
                     parsed_rows_(0),
                     total_shuffle_task_(0),
                     total_insert_task_(0),
                     shuffle_rt_sum_(0),
                     insert_rt_sum_(0),
                     total_wait_secs_(0),
                     max_allowed_error_rows_(0),
                     detected_error_rows_(0),
                     coordinator_(),
                     store_(),
                     message_() {}
  int64_t aquire() {
    return ATOMIC_AAF(&ref_cnt_, 1);
  }
  int64_t release() {
    return ATOMIC_AAF(&ref_cnt_, -1);
  }
  int64_t get_ref_cnt() { return ATOMIC_LOAD(&ref_cnt_); }

  common::ObArenaAllocator allocator_;
  volatile int64_t ref_cnt_;

  int64_t job_id_;
  common::ObString job_type_; // normal / direct
  common::ObString table_name_;
  common::ObString file_path_;
  int64_t table_column_;
  int64_t file_column_;
  int64_t batch_size_;
  int64_t parallel_;
  int64_t load_mode_;
  int64_t start_time_;
  int64_t estimated_remaining_time_;
  int64_t total_bytes_;
  volatile int64_t read_bytes_;  //bytes read to memory
  volatile int64_t parsed_bytes_;
  volatile int64_t parsed_rows_;
  int64_t total_shuffle_task_;
  int64_t total_insert_task_;
  int64_t shuffle_rt_sum_;
  int64_t insert_rt_sum_;
  int64_t total_wait_secs_;
  int64_t max_allowed_error_rows_;
  int64_t detected_error_rows_;
  struct coordinator {
    coordinator()
      : received_rows_(0),
        last_commit_segment_id_(0),
        status_("none"),
        trans_status_("none")
    {}
    volatile int64_t received_rows_; // received from client
    int64_t last_commit_segment_id_;
    common::ObString status_; // none / inited / loading / frozen / merging / commit / error / abort
    common::ObString trans_status_; // none / inited / running / frozen / commit / error / abort
    TO_STRING_KV(K(received_rows_), K(last_commit_segment_id_), K(status_), K(trans_status_));
  } coordinator_;
  struct store {
    store()
      : processed_rows_(0),
        last_commit_segment_id_(0),
        status_("none"),
        trans_status_("none"),
        compact_stage_load_rows_(0),
        compact_stage_dump_rows_(0),
        compact_stage_product_tmp_files_(0),
        compact_stage_consume_tmp_files_(0),
        compact_stage_merge_write_rows_(0),
        merge_stage_write_rows_(0)
    {}
    volatile int64_t processed_rows_;
    int64_t last_commit_segment_id_;
    common::ObString status_;
    common::ObString trans_status_;
    int64_t compact_stage_load_rows_ CACHE_ALIGNED;
    int64_t compact_stage_dump_rows_ CACHE_ALIGNED;
    int64_t compact_stage_product_tmp_files_ CACHE_ALIGNED;
    int64_t compact_stage_consume_tmp_files_ CACHE_ALIGNED;
    int64_t compact_stage_merge_write_rows_ CACHE_ALIGNED;
    int64_t merge_stage_write_rows_ CACHE_ALIGNED;
    TO_STRING_KV(K(processed_rows_), K(last_commit_segment_id_), K(status_), K(trans_status_),
                 K(compact_stage_load_rows_), K(compact_stage_dump_rows_),
                 K(compact_stage_product_tmp_files_), K(compact_stage_consume_tmp_files_),
                 K(compact_stage_merge_write_rows_), K(merge_stage_write_rows_));
  } store_;
  char message_[common::MAX_LOAD_DATA_MESSAGE_LENGTH];

  TO_STRING_KV(K(job_id_), K(job_type_),
      K(table_name_), K(file_path_), K(table_column_), K(file_column_),
      K(batch_size_), K(parallel_), K(load_mode_),
      K(start_time_), K(estimated_remaining_time_),
      K(total_bytes_), K(read_bytes_), K(parsed_bytes_),
      K(parsed_rows_), K(total_shuffle_task_), K(total_insert_task_),
      K(shuffle_rt_sum_), K(insert_rt_sum_), K(total_wait_secs_),
      K(max_allowed_error_rows_), K(detected_error_rows_),
      K(coordinator_), K(store_), K(message_));
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_ENGINE_CMD_OB_LOAD_DATA_STAT_H_
