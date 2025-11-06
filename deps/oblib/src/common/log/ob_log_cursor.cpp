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

#include "common/log/ob_log_cursor.h"

namespace oceanbase
{
namespace common
{
ObLogCursor::ObLogCursor(): file_id_(0), log_id_(0), offset_(0)
{}

ObLogCursor::~ObLogCursor()
{}

bool ObLogCursor::is_valid() const
{
  return file_id_ > 0 && log_id_ >= 0 && offset_ >= 0;
}


void ObLogCursor::reset()
{
  file_id_ = 0;
  log_id_ = 0;
  offset_ = 0;
}

int ObLogCursor::serialize(char *buf, int64_t len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (!(OB_SUCCESS == serialization::encode_i64(buf, len, pos, file_id_)
        && OB_SUCCESS == serialization::encode_i64(buf, len, pos, log_id_)
        && OB_SUCCESS == serialization::encode_i64(buf, len, pos, offset_))) {
    ret = OB_SERIALIZE_ERROR;
    SHARE_LOG(WARN, "ObLogCursor.serialize", KP(buf), K(len), K(pos), K(ret));
  }
  return ret;
}

int ObLogCursor::deserialize(const char *buf, int64_t len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (!(OB_SUCCESS == serialization::decode_i64(buf, len, pos, (int64_t *)&file_id_)
        && OB_SUCCESS == serialization::decode_i64(buf, len, pos, (int64_t *)&log_id_)
        && OB_SUCCESS == serialization::decode_i64(buf, len, pos, (int64_t *)&offset_))) {
    ret = OB_DESERIALIZE_ERROR;
    SHARE_LOG(WARN, "ObLogCursor.deserialize", KP(buf), K(len), K(pos), K(ret));
  }
  return ret;
}

int64_t ObLogCursor::get_serialize_size() const
{
  int64_t len = 0;
  len += serialization::encoded_length_i64(file_id_);
  len += serialization::encoded_length_i64(log_id_);
  len += serialization::encoded_length_i64(offset_);
  return len;
}

char *ObLogCursor::to_str() const
{
  static char buf[512];
  snprintf(buf, sizeof(buf), "ObLogCursor{file_id=%ld, log_id=%ld, offset=%ld}", file_id_, log_id_,
           offset_);
  buf[sizeof(buf) - 1] = 0;
  return buf;
}

int64_t ObLogCursor::to_string(char *buf, const int64_t limit) const
{
  return snprintf(buf, limit, "ObLogCursor{file_id=%ld, log_id=%ld, offset=%ld}", file_id_, log_id_, offset_);
}

int ObLogCursor::next_entry(ObLogEntry &entry, const LogCommand cmd, const char *log_data,
                            const int64_t data_len) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(entry.set_log_seq(log_id_))) {
    SHARE_LOG(WARN, "set_log_seq error", K(log_id_), K(ret));
  } else if (OB_FAIL(entry.set_log_command(cmd))) {
    SHARE_LOG(WARN, "set_log_command error", K(cmd), K(ret));
  } else if (OB_FAIL(entry.fill_header(log_data, data_len, 0))) {
    SHARE_LOG(WARN, "fill_header error", KP(log_data), K(data_len), K(ret));
  }
  return ret;
}

int ObLogCursor:: advance(LogCommand cmd, int64_t seq, const int64_t data_len)
{
  int ret = OB_SUCCESS;
  ObLogEntry entry;
  if (OB_UNLIKELY(cmd < 0) || OB_UNLIKELY(seq < 0) || OB_UNLIKELY(data_len < 0)) {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "invalid arguments", K(cmd), K(seq), K(data_len));
  } else if (OB_LIKELY(log_id_ > 0) && OB_UNLIKELY(seq != log_id_)) {
    ret = OB_DISCONTINUOUS_LOG;
    SHARE_LOG(ERROR, "entry.advance", K_(log_id), K(seq), K(ret));
  } else {
    log_id_ = seq + 1;
    offset_ += entry.get_header_size() + data_len;
    if (OB_LOG_SWITCH_LOG == cmd) {
      file_id_++;
      offset_ = 0;
    }
  }
  return ret;
}

int ObLogCursor::advance(const ObLogEntry &entry)
{
  return advance((LogCommand)entry.cmd_, entry.seq_, entry.get_log_data_len());
}


bool ObLogCursor::newer_than(const ObLogCursor &that) const
{
  return file_id_ > that.file_id_  || (file_id_ == that.file_id_ && log_id_ > that.log_id_);
}

bool ObLogCursor::equal(const ObLogCursor &that) const
{
  return file_id_ == that.file_id_  && log_id_ == that.log_id_ && offset_ == that.offset_;
}

ObLogCursor &set_cursor(ObLogCursor &cursor, const int64_t file_id, const int64_t log_id,
                        const int64_t offset)
{
  cursor.file_id_ = file_id;
  cursor.log_id_ = log_id;
  cursor.offset_ = offset;
  return cursor;
}


}; // end namespace common
}; // end namespace oceanbase
