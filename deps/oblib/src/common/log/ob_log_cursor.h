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

#ifndef OCEANBASE_SHARE_LOG_OB_LOG_CURSOR_
#define OCEANBASE_SHARE_LOG_OB_LOG_CURSOR_
#include "lib/ob_define.h"
#include "lib/utility/serialization.h"
#include "lib/lock/ob_spin_rwlock.h"
#include "common/log/ob_log_entry.h"

namespace oceanbase
{
namespace common
{
struct ObLogCursor
{
  int64_t file_id_;
  int64_t log_id_;
  int64_t offset_;
  ObLogCursor();
  ~ObLogCursor();
  bool is_valid() const;
  void reset();
  int serialize(char *buf, int64_t len, int64_t &pos) const;
  int deserialize(const char *buf, int64_t len, int64_t &pos) const;
  int64_t get_serialize_size() const;
  char *to_str() const;
  int64_t to_string(char *buf, const int64_t len) const;
  int this_entry(ObLogEntry &entry, const LogCommand cmd, const char *log_data,
                 const int64_t data_len) const;
  int next_entry(ObLogEntry &entry, const LogCommand cmd, const char *log_data,
                 const int64_t data_len) const;
  int advance(const ObLogEntry &entry);
  int advance(LogCommand cmd, int64_t seq, const int64_t data_len);
  bool newer_than(const ObLogCursor &that) const;
  bool equal(const ObLogCursor &that) const;
};

ObLogCursor &set_cursor(ObLogCursor &cursor, const int64_t file_id, const int64_t log_id,
                        const int64_t offset);
class ObAtomicLogCursor
{
public:
  ObAtomicLogCursor() : cursor_lock_(ObLatchIds::DEFAULT_SPIN_RWLOCK) {}
  ~ObAtomicLogCursor() {}
private:
  ObLogCursor log_cursor_;
  mutable common::SpinRWLock cursor_lock_;
};
}; // end namespace common
}; // end namespace oceanbase
#endif // OCEANBASE_SHARE_LOG_OB_LOG_CURSOR_
