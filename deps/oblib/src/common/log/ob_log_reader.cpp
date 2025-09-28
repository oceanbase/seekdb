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

#include "common/log/ob_log_reader.h"

using namespace oceanbase::common;

ObLogReader::ObLogReader()
  : cur_log_file_id_(0),
    cur_log_seq_id_(0),
    max_log_file_id_(0),
    log_file_reader_(NULL),
    is_inited_(false),
    is_wait_(false),
    has_max_(false)
{
}

ObLogReader::~ObLogReader()
{
}


int ObLogReader::read_log(LogCommand &cmd,
                          uint64_t &seq,
                          char *&log_data,
                          int64_t &data_len)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(ERROR, "ObLogReader has not been initialized", K(ret));
  } else if (OB_ISNULL(log_file_reader_)) {
    SHARE_LOG(ERROR, "log_file_reader_ is NULL, this should not be reached");
    ret = OB_ERROR;
  } else {
    if (!log_file_reader_->is_opened()) {
      ret = open_log_(cur_log_file_id_);
    }
    if (OB_SUCC(ret)) {
      ret = read_log_(cmd, seq, log_data, data_len);
      if (OB_SUCC(ret)) {
        cur_log_seq_id_ = seq;
      }
      if (OB_SUCCESS == ret && OB_LOG_SWITCH_LOG == cmd) {
        SHARE_LOG(INFO, "reach the end of log", K(cur_log_file_id_));
        // Regardless of opening success or failure: cur_log_file_id++, log_file_reader_->pos will be set to zero,
        if (OB_FAIL(log_file_reader_->close())) {
          SHARE_LOG(ERROR, "log_file_reader_ close error", K(ret));
        } else if (OB_FAIL(open_log_(++cur_log_file_id_, seq))
            && OB_READ_NOTHING != ret) {
          SHARE_LOG(WARN, "open log failed", K(cur_log_file_id_), K(seq), K(ret));
        }
      }
    }
  }

  return ret;
}




int ObLogReader::open_log_(const uint64_t log_file_id,
                           const uint64_t last_log_seq/* = 0*/)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(ERROR, "ObLogReader has not been initialized", K(ret));
  } else if (OB_ISNULL(log_file_reader_)) {
    SHARE_LOG(ERROR, "log_file_reader_ is NULL, this should not be reached");
    ret = OB_ERROR;
  } else if (is_wait_ && has_max_ && log_file_id > max_log_file_id_) {
    ret = OB_READ_NOTHING;
  } else {
    ret = log_file_reader_->open(log_file_id, last_log_seq);
    if (is_wait_) {
      if (OB_FILE_NOT_EXIST == ret) {
        SHARE_LOG(DEBUG, "log file doesnot exist", K(log_file_id));
        ret = OB_READ_NOTHING;
      } else if (OB_FAIL(ret)) {
        SHARE_LOG(WARN, "log_file_reader_ open error", K(cur_log_file_id_), K(ret));
      }
    } else {
      if (OB_FAIL(ret)) {
        SHARE_LOG(WARN, "log_file_reader_ open error", K(cur_log_file_id_), K(ret));
        if (OB_FILE_NOT_EXIST == ret) {
          ret = OB_READ_NOTHING;
        }
      }
    }
  }

  return ret;
}

int ObLogReader::read_log_(LogCommand &cmd,
                           uint64_t &log_seq,
                           char *&log_data,
                           int64_t &data_len)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(ERROR, "ObLogReader has not been initialized", K(ret));
  } else if (OB_ISNULL(log_file_reader_)) {
    SHARE_LOG(ERROR, "log_file_reader_ is NULL, this should not be reached");
    ret = OB_ERROR;
  } else {
    if (OB_FAIL(log_file_reader_->read_log(cmd, log_seq, log_data, data_len))
        && OB_READ_NOTHING != ret) {
      SHARE_LOG(WARN, "log_file_reader_ read_log error", K(ret));
    }
  }

  return ret;
}
