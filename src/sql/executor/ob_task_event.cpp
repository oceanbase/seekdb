/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX SQL_EXE

#include "sql/executor/ob_task_event.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace sql
{

ObTaskSmallResult::ObTaskSmallResult()
  : has_data_(false),
    data_len_(0),
    data_buf_(),
    affected_rows_(0),
    found_rows_(0),
    last_insert_id_(0),
    matched_rows_(0),
    duplicated_rows_(0)
{
}

ObTaskSmallResult::~ObTaskSmallResult()
{
}

void ObTaskSmallResult::reset()
{
  has_data_ = false;
  data_len_ = 0;
  affected_rows_ = 0;
  found_rows_ = 0;
  last_insert_id_ = 0;
  matched_rows_ = 0;
  duplicated_rows_ = 0;
}

bool ObTaskSmallResult::equal(const ObTaskSmallResult &other) const
{
  bool is_eq = (has_data_ == other.has_data_ && data_len_ == other.data_len_);
  for (int64_t i = 0; is_eq && i < data_len_ && i < MAX_DATA_BUF_LEN; ++i) {
    if (data_buf_[i] != other.data_buf_[i]) {
      is_eq = false;
    }
  }
  return is_eq;
}

int ObTaskSmallResult::assign(const ObTaskSmallResult &other)
{
  int ret = OB_SUCCESS;
  has_data_ = other.has_data_;
  if (OB_UNLIKELY(other.data_len_ > MAX_DATA_BUF_LEN)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("data len is too long", K(ret), K(other.data_len_), LITERAL_K(MAX_DATA_BUF_LEN));
  } else {
    data_len_ = other.data_len_;
    MEMCPY(data_buf_, other.data_buf_, other.data_len_);
    affected_rows_ = other.affected_rows_;
    found_rows_ = other.found_rows_;
    last_insert_id_ = other.last_insert_id_;
    matched_rows_ = other.matched_rows_;
    duplicated_rows_ = other.duplicated_rows_;
  }
  return ret;
}



int ObSliceEvent::assign(const ObSliceEvent &other)
{
  int ret = OB_SUCCESS;
  ob_slice_id_ = other.ob_slice_id_;
  if (OB_FAIL(small_result_.assign(other.small_result_))) {
    LOG_WARN("fail to assign slice small result", K(ret), K(other));
  }
  return ret;
}


ObTaskEvent::ObTaskEvent()
  : task_loc_(),
    err_code_(static_cast<const int64_t>(OB_ERR_UNEXPECTED)),
    inited_(false),
    ts_task_recv_done_(0),
    ts_result_send_begin_(0)
{
}

ObTaskEvent::~ObTaskEvent()
{
}



void ObTaskEvent::reset()
{
  task_loc_.reset();
  err_code_ = static_cast<const int64_t>(OB_ERR_UNEXPECTED);
  inited_ = false;
}


OB_DEF_SERIALIZE(ObTaskSmallResult)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              has_data_,
              data_len_);
  if (OB_SUCC(ret)) {
    if (has_data_ && data_len_ > 0) {
      MEMCPY(buf + pos, data_buf_, data_len_);
      pos += data_len_;
    }
  }
  LST_DO_CODE(OB_UNIS_ENCODE,
              affected_rows_,
              found_rows_,
              last_insert_id_,
              matched_rows_,
              duplicated_rows_);
  return ret;
}

OB_DEF_DESERIALIZE(ObTaskSmallResult)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE,
              has_data_,
              data_len_);
  if (OB_SUCC(ret)) {
    if (has_data_ && data_len_ > 0) {
      MEMCPY(data_buf_, buf + pos, data_len_);
      pos += data_len_;
    }
  }
  LST_DO_CODE(OB_UNIS_DECODE,
              affected_rows_,
              found_rows_,
              last_insert_id_,
              matched_rows_,
              duplicated_rows_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObTaskSmallResult)
{
  int64_t len = 0;
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              has_data_,
              data_len_);
  if (OB_SUCC(ret)) {
    if (has_data_ && data_len_ > 0) {
      len += data_len_;
    }
  }
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              affected_rows_,
              found_rows_,
              last_insert_id_,
              matched_rows_,
              duplicated_rows_);
  return len;
}

OB_SERIALIZE_MEMBER(ObSliceEvent, ob_slice_id_, small_result_);
OB_SERIALIZE_MEMBER(ObTaskEvent, task_loc_, err_code_, inited_, ts_task_recv_done_, ts_result_send_begin_);

int ObMiniTaskResult::assign(const ObMiniTaskResult &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(task_result_.assign(other.task_result_))) {
    LOG_WARN("assign task result failed", K(ret), K(other));
  } else if (OB_FAIL(extend_result_.assign(other.extend_result_))) {
    LOG_WARN("assign extend result failed", K(ret), K(other));
  }
  return ret;
}



OB_SERIALIZE_MEMBER(ObMiniTaskResult, task_result_, extend_result_);
OB_SERIALIZE_MEMBER(ObRemoteResult, task_id_, result_, has_more_);
}/* ns sql*/
}/* ns oceanbase */
