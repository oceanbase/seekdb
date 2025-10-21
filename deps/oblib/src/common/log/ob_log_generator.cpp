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

#include "common/log/ob_log_generator.h"

namespace oceanbase
{
namespace common
{
int DebugLog::advance()
{
  int ret = OB_SUCCESS;
  last_ctime_ = ctime_;
  ctime_ = ObTimeUtility::current_time();
  return ret;
}


int DebugLog::serialize(char *buf, int64_t limit, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  if (OB_ISNULL(buf) || limit < 0 || pos > limit) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(serialization::encode_i64(buf, limit, new_pos, MAGIC))
             || OB_FAIL(server_.serialize(buf, limit, new_pos))
             || OB_FAIL(serialization::encode_i64(buf, limit, new_pos, ctime_))
             || OB_FAIL(serialization::encode_i64(buf, limit, new_pos, last_ctime_))) {
    OB_LOG(WARN, "serialize error", K(ret), KP(buf), K(limit), K(pos));
    ret = OB_SERIALIZE_ERROR;
  } else {
    pos = new_pos;
  }
  return ret;
}


inline int64_t get_align_padding_size(const int64_t x, const int64_t mask)
{
  return -x & mask;
}

static bool is_aligned(int64_t x, int64_t mask)
{
  return !(x & mask);
}

static int64_t calc_nop_log_len(int64_t pos, int64_t min_log_size)
{
  ObLogEntry entry;
  int64_t header_size = entry.get_serialize_size();
  return get_align_padding_size(pos + header_size + min_log_size,
                                ObLogConstants::LOG_FILE_ALIGN_MASK) + min_log_size;
}

char ObLogGenerator::eof_flag_buf_[ObLogConstants::LOG_FILE_ALIGN_SIZE] __attribute__((aligned(ObLogConstants::LOG_FILE_ALIGN_SIZE)));
static class EOF_FLAG_BUF_CONSTRUCTOR
{
public:
  EOF_FLAG_BUF_CONSTRUCTOR()
  {
    const char *mark_str = "end_of_log_file";
    const int64_t mark_length = static_cast<int64_t>(STRLEN(mark_str));
    const int64_t eof_length = static_cast<int64_t>(sizeof(ObLogGenerator::eof_flag_buf_));
    memset(ObLogGenerator::eof_flag_buf_, 0, sizeof(ObLogGenerator::eof_flag_buf_));
    for (int64_t i = 0; i + mark_length < eof_length; i += mark_length) {
      STRCPY(ObLogGenerator::eof_flag_buf_ + i, mark_str);
    }
  }
  ~EOF_FLAG_BUF_CONSTRUCTOR() {}
} eof_flag_buf_constructor_;

ObLogGenerator::ObLogGenerator(): is_frozen_(false),
                                  log_file_max_size_(1 << 24),
                                  start_cursor_(),
                                  end_cursor_(),
                                  log_buf_(NULL),
                                  log_buf_len_(0),
                                  pos_(0),
                                  debug_log_()
{
  memset(empty_log_, 0, sizeof(empty_log_));
  memset(nop_log_, 0, sizeof(nop_log_));
}

ObLogGenerator:: ~ObLogGenerator()
{
  if (NULL != log_buf_) {
    free(log_buf_);
    log_buf_ = NULL;
  }
}

bool ObLogGenerator::is_inited() const
{
  return NULL != log_buf_ && log_buf_len_ > 0;
}


int ObLogGenerator::check_state() const
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  }
  return ret;
}












static int serialize_log_entry(char *buf, const int64_t len, int64_t &pos, ObLogEntry &entry,
                               const char *log_data, const int64_t data_len)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || 0 >= len || pos > len || OB_ISNULL(log_data) || 0 >= data_len) {
    ret = OB_INVALID_ARGUMENT;
    _OB_LOG(ERROR, "serialize_log_entry(buf=%p, len=%ld, pos=%ld, log_data=%p, data_len=%ld)=>%d",
            buf, len, pos, log_data, data_len, ret);
  } else if (pos + entry.get_serialize_size() + data_len > len) {
    ret = OB_BUF_NOT_ENOUGH;
    _OB_LOG(DEBUG, "pos[%ld] + entry.serialize_size[%ld] + data_len[%ld] > len[%ld]",
            pos, entry.get_serialize_size(), data_len, len);
  } else if (OB_FAIL(entry.serialize(buf, len, pos))) {
    _OB_LOG(ERROR, "entry.serialize(buf=%p, pos=%ld, capacity=%ld)=>%d",
            buf, len, pos, ret);
  } else {
    MEMCPY(buf + pos, log_data, data_len);
    pos += data_len;
  }
  return ret;
}

static int generate_log(char *buf, const int64_t len, int64_t &pos, ObLogCursor &cursor,
                        const LogCommand cmd,
                        const char *log_data, const int64_t data_len)
{
  int ret = OB_SUCCESS;
  ObLogEntry entry;
  if (OB_ISNULL(buf) || 0 >= len || pos > len || OB_ISNULL(log_data) || 0 >= data_len ||
      !cursor.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    ObCStringHelper helper;
    _OB_LOG(ERROR, "generate_log(buf=%p, len=%ld, pos=%ld, log_data=%p, data_len=%ld, cursor=%s)=>%d",
            buf, len, pos, log_data, data_len, helper.convert(cursor), ret);
  } else if (entry.get_serialize_size() + data_len > len) {
    ret = OB_LOG_TOO_LARGE;
    _OB_LOG(WARN, "header[%ld] + data_len[%ld] > len[%ld]", entry.get_serialize_size(), data_len,
            len);
  } else if (OB_FAIL(cursor.next_entry(entry, cmd, log_data, data_len))) {
    ObCStringHelper helper;
    _OB_LOG(ERROR, "cursor[%s].next_entry()=>%d", helper.convert(cursor), ret);
  } else if (OB_FAIL(serialize_log_entry(buf, len, pos, entry, log_data, data_len))) {
    _OB_LOG(DEBUG, "serialize_log_entry(buf=%p, len=%ld, entry[id=%ld], data_len=%ld)=>%d",
            buf, len, entry.seq_, data_len, ret);
  } else if (OB_FAIL(cursor.advance(entry))) {
    _OB_LOG(ERROR, "cursor[id=%ld].advance(entry.id=%ld)=>%d", cursor.log_id_, entry.seq_, ret);
  }
  return ret;
}

int ObLogGenerator:: do_write_log(const LogCommand cmd, const char *log_data,
                                  const int64_t data_len,
                                  const int64_t reserved_len)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_state())) {
    _OB_LOG(ERROR, "check_state()=>%d", ret);
  } else if (OB_ISNULL(log_data) || data_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (is_frozen_) {
    ret = OB_STATE_NOT_MATCH;
    ObCStringHelper helper;
    _OB_LOG(ERROR, "log_generator is frozen, cursor=[%s,%s]", helper.convert(start_cursor_),
            helper.convert(end_cursor_));
  } else if (OB_FAIL(generate_log(log_buf_, log_buf_len_ - reserved_len, pos_,
                                  end_cursor_, cmd, log_data, data_len))
             && OB_BUF_NOT_ENOUGH != ret) {
    _OB_LOG(WARN, "generate_log(pos=%ld)=>%d", pos_, ret);
  }
  return ret;
}




int ObLogGenerator::switch_log()
{
  int ret = OB_SUCCESS;
  ObLogEntry entry;
  int64_t header_size = entry.get_serialize_size();
  const int64_t buf_len = ObLogConstants::LOG_FILE_ALIGN_SIZE - header_size;
  char *buf = empty_log_;
  int64_t buf_pos = 0;
  if (OB_FAIL(check_state())) {
    _OB_LOG(ERROR, "check_state()=>%d", ret);
  } else if (OB_FAIL(serialization::encode_i64(buf, buf_len, buf_pos,
                                               end_cursor_.file_id_ + 1))) {
    _OB_LOG(ERROR, "encode_i64(file_id_=%ld)=>%d", end_cursor_.file_id_, ret);
  } else if (OB_FAIL(do_write_log(OB_LOG_SWITCH_LOG, buf, buf_len, 0))) {
    _OB_LOG(ERROR, "write(OB_LOG_SWITCH_LOG, len=%ld)=>%d", end_cursor_.file_id_, ret);
  } else {
    _OB_LOG(INFO, "switch_log(file_id=%ld, log_id=%ld)", end_cursor_.file_id_, end_cursor_.log_id_);
  }
  return ret;
}




bool ObLogGenerator::is_eof(const char *buf, int64_t len)
{
  return NULL != buf && len >= ObLogConstants::LOG_FILE_ALIGN_SIZE &&
         0 == MEMCMP(buf, eof_flag_buf_, ObLogConstants::LOG_FILE_ALIGN_SIZE);
}






} // end namespace common
} // end namespace oceanbase
