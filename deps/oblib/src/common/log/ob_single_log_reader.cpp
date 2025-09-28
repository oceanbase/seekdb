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

#include "common/log/ob_single_log_reader.h"
#include "common/log/ob_log_dir_scanner.h"
#include "common/log/ob_log_generator.h"

using namespace oceanbase::common;

const int64_t ObSingleLogReader::LOG_BUFFER_MAX_LENGTH = 1 << 21;

ObSingleLogReader::ObSingleLogReader()
{
  is_inited_ = false;
  file_id_ = 0;
  last_log_seq_ = 0;
  log_buffer_.reset();
  file_name_[0] = '\0';
  pos_ = 0;
  pread_pos_ = 0;
  dio_ = true;
}

ObSingleLogReader::~ObSingleLogReader()
{
  if (NULL != log_buffer_.get_data()) {
    ob_free(log_buffer_.get_data());
    log_buffer_.reset();
  }
}



// 0 is valid file id, so as last_log_seq
int ObSingleLogReader::open(const uint64_t file_id, const uint64_t last_log_seq/* = 0*/)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(ERROR, "ObSingleLogReader not init", K(ret));
  } else {
    int err = snprintf(file_name_, OB_MAX_FILE_NAME_LENGTH, "%s/%lu", log_dir_, file_id);
    if (OB_UNLIKELY(err < 0)) {
      ret = OB_ERROR;
      SHARE_LOG(ERROR, "snprintf file name error", K(ret), K(err), KCSTRING(log_dir_), K(file_id),
                KERRNOMSG(errno));
    } else if (err >= OB_MAX_FILE_NAME_LENGTH) {
      ret = OB_ERROR;
      SHARE_LOG(ERROR, "snprintf file_name error", K(ret), K(file_id), KERRNOMSG(errno));
    } else {
      int32_t fn_len = static_cast<int32_t>(strlen(file_name_));
      file_id_ = file_id;
      last_log_seq_ = last_log_seq;
      pos_ = 0;
      pread_pos_ = 0;
      log_buffer_.get_position() = 0;
      log_buffer_.get_limit() = 0;
      if (OB_SUCC(file_.open(ObString(fn_len, fn_len, file_name_), dio_))) {
        SHARE_LOG(INFO, "open log file success", KCSTRING(file_name_), K(file_id));
      } else if (OB_FILE_NOT_EXIST == ret) {
        SHARE_LOG(DEBUG, "log file not found", K(ret), KCSTRING(file_name_), K(file_id));
      } else {
        SHARE_LOG(WARN, "open file error", K(ret), KCSTRING(file_name_), KERRNOMSG(errno));
      }
    }
  }
  return ret;
}


int ObSingleLogReader::close()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    SHARE_LOG(ERROR, "single log reader not init");
    ret = OB_NOT_INIT;
  } else {
    file_.close();
    if (last_log_seq_ == 0) {
      SHARE_LOG(INFO, "close file, read no data from this log", KCSTRING(file_name_));
    } else {
      SHARE_LOG(INFO, "close file successfully", KCSTRING(file_name_), K(last_log_seq_));
    }
  }
  return ret;
}





int ObSingleLogReader::read_log_()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(ERROR, "single log reader not init", K(ret));
  } else {
    if (log_buffer_.get_remain_data_len() > 0) {
      MEMMOVE(log_buffer_.get_data(), log_buffer_.get_data() + log_buffer_.get_position(),
              log_buffer_.get_remain_data_len());
      log_buffer_.get_limit() = log_buffer_.get_remain_data_len();
      log_buffer_.get_position() = 0;
    } else {
      log_buffer_.get_limit() = log_buffer_.get_position() = 0;
    }

    int64_t read_size = 0;
    ret = file_.pread(log_buffer_.get_data() + log_buffer_.get_limit(),
                      log_buffer_.get_capacity() - log_buffer_.get_limit(),
                      pread_pos_, read_size);
    SHARE_LOG(DEBUG, "pread", K(ret), K(pread_pos_), K(read_size), "buf_pos", log_buffer_.get_position(),
              "buf_limit", log_buffer_.get_limit());
    if (OB_FAIL(ret)) {
      SHARE_LOG(ERROR, "read log file error", K(ret), K(file_id_));
    } else {
      // comment this log due to too frequent invoke by replay thread
      // SHARE_LOG(DEBUG, "read data from log file", K(ret), K(file_id_), K(log_fd_));
      if (0 == read_size) {
        // comment this log due to too frequent invoke by replay thread
        // SHARE_LOG(DEBUG, "reach end of log file", K(file_id_));
        ret = OB_READ_NOTHING;
      } else {
        log_buffer_.get_limit() += read_size;
        pread_pos_ += read_size;
      }
    }
  }
  return ret;
}

