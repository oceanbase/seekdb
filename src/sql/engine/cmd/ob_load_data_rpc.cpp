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

#define USING_LOG_PREFIX  SQL_ENG


#include "ob_load_data_rpc.h"
#include "sql/engine/cmd/ob_load_data_impl.h"

using namespace oceanbase::sql;
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::storage;

namespace oceanbase
{
namespace sql
{

int ObLoadbuffer::deep_copy_str(const ObString &src, ObString &dest)
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  int64_t len = src.length() + 1;
  if (OB_UNLIKELY(len <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("src string length is invalid", K(ret), K(src), K(len));
  } else if (NULL == (buf = static_cast<char*>(field_data_allocator_.alloc(len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("fail to allocate memory, ", K(ret), K(src), K(len));
  } else {
    MEMCPY(buf, src.ptr(), len - 1);
    buf[len - 1] = '\0';
    dest.assign_ptr(buf, static_cast<ObString::obstr_size_t>(len - 1));
  }
  return ret;
}





int ObParallelTaskController::init(int64_t max_parallelism)
{
  int ret = OB_SUCCESS;
  max_parallelism_ = max_parallelism;
  if (OB_FAIL(vacant_cond_.init(common::ObWaitEventIds::ASYNC_RPC_PROXY_COND_WAIT))) {
    LOG_WARN("init vacant cond failed", K(ret));
  }
  return ret;
}

int ObParallelTaskController::on_next_task()
{
  int ret = OB_SUCCESS;
  ObThreadCondGuard guard(vacant_cond_);

  if (ATOMIC_AAF(&processing_cnt_, 1) > max_parallelism_) {
    ret = vacant_cond_.wait();
  }

  return ret;
}

void ObParallelTaskController::wait_all_task_finish(const char *task_name, int64_t until_ts)
{
  int64_t wait_duration_ms = 0;
  int64_t begin_ts = ObTimeUtil::current_time();
  int64_t processing_count = 0;
  bool is_too_long = false;
  LOG_DEBUG("start wait_all_task_finish", K(task_name));
  while ((processing_count = get_processing_task_cnt()) > 0) {
    ob_usleep(1000 * 10); //wait 10m
    wait_duration_ms += 10;
    if (0 == wait_duration_ms % 1000) {
      int64_t current_ts = ObTimeUtil::current_time();
      if (current_ts > until_ts) {
        LOG_ERROR_RET(OB_ERR_UNEXPECTED, "waiting load data task too long and exceed max waiting timestamp",
                  K(begin_ts), K(until_ts), K(current_ts));
      }
    }
    if (!is_too_long && wait_duration_ms > 10 * 1000) {
      is_too_long = true;
      LOG_WARN_RET(OB_ERR_UNEXPECTED, "LOAD DATA, waiting task finish too long",
               K(task_name), K(processing_count), K(wait_duration_ms), K(until_ts));
    }
  }
  if (is_too_long) {
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "LOAD DATA finish waiting long task", K(wait_duration_ms));
  }
}


int ObParallelTaskController::on_task_finished()
{
  int ret = OB_SUCCESS;
  if(max_parallelism_ == ATOMIC_AAF(&processing_cnt_, -1)) {
    ObThreadCondGuard guard(vacant_cond_);
    ret = vacant_cond_.signal();
  }
  return ret;
}

int ObInsertResult::assign(const ObInsertResult &other)
{
  int ret = OB_SUCCESS;
  flags_ = other.flags_;
  exec_ret_ = other.exec_ret_;
  err_line_no_ = other.err_line_no_;

  if (OB_FAIL(ob_write_string(allocator_, other.err_msg_, err_msg_))) {
    LOG_WARN("fail to write string", K(ret));
  }

  return ret;
}

OB_DEF_SERIALIZE(ObLoadbuffer)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              task_id_,
              tablet_id_,
              table_id_,
              
              stored_pos_,
              stored_row_cnt_,
              insert_mode_,
              insert_column_num_);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(table_name_.serialize(buf, buf_len, pos))) {
    LOG_WARN("serialize error", K(ret));
  } else if (OB_FAIL(insert_column_names_.serialize(buf, buf_len, pos))) {
    LOG_WARN("serialize column names error", K(ret));
  } else if (OB_FAIL(insert_values_.serialize(buf, buf_len, pos))) {
    LOG_WARN("serialize row store error", K(ret));
  } else if (OB_FAIL(expr_bitset_.serialize(buf, buf_len, pos))) {
    LOG_WARN("serialize expr bitset error", K(ret));
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObLoadbuffer)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE,
              task_id_,
              tablet_id_,
              table_id_,
              
              stored_pos_,
              stored_row_cnt_,
              insert_mode_,
              insert_column_num_);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(table_name_.deserialize(buf, data_len, pos))) {
    LOG_WARN("deserialize error", K(ret));
  } else if (OB_FAIL(insert_column_names_.deserialize(buf, data_len, pos))) {
    LOG_WARN("deserialize row store error", K(ret));
  } else if (OB_FAIL(insert_values_.deserialize(buf, data_len, pos))) {
    LOG_WARN("deserialize row store error", K(ret));
  } else if (OB_FAIL(expr_bitset_.deserialize(buf, data_len, pos))) {
    LOG_WARN("deserialize expr bitset error", K(ret));
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObLoadbuffer)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              task_id_,
              tablet_id_,
              table_id_,
              
              stored_pos_,
              stored_row_cnt_,
              insert_mode_,
              insert_column_num_);
  len += table_name_.get_serialize_size();
  len += insert_column_names_.get_serialize_size();
  len += insert_values_.get_serialize_size();
  len += expr_bitset_.get_serialize_size();
  return len;
}

OB_DEF_SERIALIZE(ObLoadResult)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              task_id_,
              tablet_id_,
              affected_rows_,
              failed_rows_,
              task_flags_);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(row_number_.serialize(buf, buf_len, pos))) {
    LOG_WARN("serialize row_number_ error", K(ret));
  } else if (OB_FAIL(row_err_code_.serialize(buf, buf_len, pos))) {
    LOG_WARN("serialize row_err_code_ error", K(ret));
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObLoadResult)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE,
              task_id_,
              tablet_id_,
              affected_rows_,
              failed_rows_,
              task_flags_);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(row_number_.deserialize(buf, data_len, pos))) {
    LOG_WARN("deserialize row_number_ error", K(ret));
  } else if (OB_FAIL(row_err_code_.deserialize(buf, data_len, pos))) {
    LOG_WARN("deserialize row_err_code_ error", K(ret));
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObLoadResult)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              task_id_,
              tablet_id_,
              affected_rows_,
              failed_rows_,
              task_flags_);
  len += row_number_.get_serialize_size();
  len += row_err_code_.get_serialize_size();
  return len;
}

OB_SERIALIZE_MEMBER(ObShuffleTask,
                    task_id_,
                    shuffle_task_handle_,
                    gid_);
OB_SERIALIZE_MEMBER(ObShuffleResult,
                    task_id_,
                    flags_,
                    exec_ret_,
                    row_cnt_);

OB_SERIALIZE_MEMBER(ObInsertTask,
                    
                    task_id_,
                    row_count_,
                    column_count_,
                    insert_stmt_head_,
                    insert_value_data_,
                    timezone_,
                    sql_mode_);
OB_SERIALIZE_MEMBER(ObInsertResult,
                    flags_,
                    exec_ret_,
                    err_line_no_,
                    err_msg_);

}
}
