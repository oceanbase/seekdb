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

#ifndef OCEANBASE_LOGSERVICE_PALF_LOG_BUFFER_
#define OCEANBASE_LOGSERVICE_PALF_LOG_BUFFER_

#include "lib/ob_define.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "log_entry_header.h"
#include "log_group_entry_header.h"
#include "log_writer_utils.h"
#include "lsn.h"

namespace oceanbase
{
namespace palf
{

// An explicitly transferable owner for one immutable PALF log payload.  The
// prefix is reserved for LogEntryHeader, so PALF can prepend its header without
// copying the payload into another buffer.
class PalfLogBuffer
{
public:
  PalfLogBuffer();
  ~PalfLogBuffer();

  int init(const int64_t capacity, const int64_t prefix_size);
  int copy_from(const char *buf, const int64_t size, const int64_t prefix_size);
  int extend_and_copy(const int64_t new_capacity, const int64_t valid_size);
  int seal(const int64_t size);
  int reuse_for_write();
  int move_from(PalfLogBuffer &other);
  void reset();

  bool is_valid() const;
  bool is_sealed() const { return is_sealed_; }
  char *get_buf() { return data_; }
  const char *get_buf() const { return data_; }
  char *get_prefix_buf(const int64_t size);
  const char *get_prefix_buf(const int64_t size) const;
  int64_t get_size() const { return size_; }
  int64_t get_capacity() const { return capacity_; }
  int64_t get_prefix_size() const { return prefix_size_; }

  TO_STRING_KV(KP_(allocation), KP_(data), K_(size), K_(capacity),
      K_(prefix_size), K_(is_sealed));

private:
  char *allocation_;
  char *data_;
  int64_t size_;
  int64_t capacity_;
  int64_t prefix_size_;
  bool is_sealed_;
  DISALLOW_COPY_AND_ASSIGN(PalfLogBuffer);
};

class LogPendingBufferLimiter
{
public:
  struct State
  {
    explicit State(const int64_t limit)
      : pending_bytes_(0), limit_(limit), ref_count_(1)
    {}
    int64_t pending_bytes_;
    int64_t limit_;
    int64_t ref_count_;
  };

  explicit LogPendingBufferLimiter(const int64_t limit = 0);
  ~LogPendingBufferLimiter();
  void reset(const int64_t limit);
  bool try_acquire(const int64_t bytes, State *&reservation_state);
  static void release(State *&reservation_state, const int64_t bytes);
  int64_t get_pending_bytes() const;
  int64_t get_limit() const;

  TO_STRING_KV("pending_bytes", get_pending_bytes(), "limit", get_limit());

private:
  static State *alloc_state_(const int64_t limit);
  static void release_state_ref_(State *&state);

private:
  State *state_;
  DISALLOW_COPY_AND_ASSIGN(LogPendingBufferLimiter);
};

// One complete entry in a LogTask.  For normal entries entry_buf points to the
// reserved LogEntryHeader prefix followed immediately by the caller payload.
class LogBufferSegment
{
public:
  LogBufferSegment();
  ~LogBufferSegment();

  // Prepare every resource which may fail before assigning an LSN.  bind_*()
  // only materializes headers and immutable metadata after the LSN allocator
  // succeeds.
  static int prepare_normal(PalfLogBuffer &owner,
                            LogPendingBufferLimiter *limiter,
                            LogBufferSegment *&segment);
  static int prepare_padding(LogPendingBufferLimiter *limiter,
                             LogBufferSegment *&segment);
  int bind_normal(const LSN &entry_lsn, const share::SCN &scn);
  int bind_padding(const LSN &entry_lsn,
                   const share::SCN &scn,
                   const int64_t log_body_size);
  static int create_normal(const LSN &entry_lsn,
                           const share::SCN &scn,
                           PalfLogBuffer &owner,
                           LogPendingBufferLimiter *limiter,
                           const int64_t reserved_bytes,
                           LogBufferSegment *&segment);
  static int create_padding(const LSN &entry_lsn,
                            const share::SCN &scn,
                            const int64_t log_body_size,
                            LogPendingBufferLimiter *limiter,
                            LogBufferSegment *&segment);
  static void destroy_list(LogBufferSegment *head);

  const LSN &get_begin_lsn() const { return begin_lsn_; }
  LSN get_end_lsn() const { return begin_lsn_ + entry_size_; }
  const char *get_entry_buf() const;
  int64_t get_entry_size() const { return entry_size_; }
  int64_t get_valid_size() const { return valid_size_; }
  bool is_padding() const { return is_padding_; }
  int64_t get_data_checksum() const { return data_checksum_; }
  LogBufferSegment *get_next() const { return next_; }
  void set_next(LogBufferSegment *next) { next_ = next; }
  int move_owner_to(PalfLogBuffer &owner);

  TO_STRING_KV(K_(begin_lsn), K_(entry_size), K_(valid_size), K_(is_padding), K_(data_checksum),
      K_(reserved_bytes), KP_(limiter_state), KP_(next), K_(owner));

private:
  int prepare_normal_(PalfLogBuffer &owner,
                      LogPendingBufferLimiter *limiter,
                      const int64_t reserved_bytes);
  int prepare_padding_(LogPendingBufferLimiter *limiter);
  static int alloc_segment_(LogBufferSegment *&segment);
  static void free_segment_(LogBufferSegment *segment);

private:
  LSN begin_lsn_;
  PalfLogBuffer owner_;
  int64_t entry_size_;
  int64_t valid_size_;
  bool is_padding_;
  int64_t data_checksum_;
  LogPendingBufferLimiter::State *limiter_state_;
  int64_t reserved_bytes_;
  LogBufferSegment *next_;
  DISALLOW_COPY_AND_ASSIGN(LogBufferSegment);
};

// Owns all segments of one frozen group while it is queued or being written.
// LogWriteBuf is rebuilt after each ownership transfer so it never contains a
// pointer to another object's inline group-header storage.
class LogGroupWriteBuf
{
public:
  LogGroupWriteBuf();
  ~LogGroupWriteBuf();

  int init(const LogGroupEntryHeader &header,
           LogBufferSegment *head,
           LogBufferSegment *tail,
           const int64_t data_len);
  int move_from(LogGroupWriteBuf &other);
  int build_write_buf(LogWriteBuf &write_buf) const;
  void take_segments(LogBufferSegment *&head,
                     LogBufferSegment *&tail,
                     int64_t &data_len);
  void reset();

  bool is_valid() const;
  int64_t get_total_size() const
  {
    return LogGroupEntryHeader::HEADER_SER_SIZE + data_len_;
  }
  LogBufferSegment *get_head() const { return head_; }

  TO_STRING_KV(K_(data_len), KP_(head), KP_(tail));

private:
  char header_buf_[sizeof(LogGroupEntryHeader)];
  LogBufferSegment *head_;
  LogBufferSegment *tail_;
  int64_t data_len_;
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(LogGroupWriteBuf);
};

} // namespace palf
} // namespace oceanbase

#endif // OCEANBASE_LOGSERVICE_PALF_LOG_BUFFER_
