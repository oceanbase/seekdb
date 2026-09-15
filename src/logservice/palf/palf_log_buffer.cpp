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

#define USING_LOG_PREFIX PALF

#include "palf_log_buffer.h"
#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{
using namespace common;
using namespace share;
namespace palf
{

static_assert(PalfLogBuffer::DEFAULT_PREFIX_SIZE == sizeof(LogEntryHeader),
              "PalfLogBuffer prefix must match LogEntryHeader");

LogPendingBufferLimiter::LogPendingBufferLimiter()
{}

LogPendingBufferLimiter::~LogPendingBufferLimiter()
{
  destroy();
}

LogPendingBufferLimiter::State *LogPendingBufferLimiter::alloc_state_(const int64_t limit)
{
  State *state = NULL;
  void *buf = NULL;
  if (limit > 0 && OB_NOT_NULL(buf = server_malloc(sizeof(State), "PalfLogLimiter"))) {
    state = new (buf) State(limit);
  }
  return state;
}

void LogPendingBufferLimiter::release_state_ref_(State *&state)
{
  if (NULL != state) {
    if (0 == ATOMIC_SAF(&state->ref_count_, 1)) {
      state->~State();
      server_free(state);
    }
    state = NULL;
  }
}

int LogPendingBufferLimiter::reset(const int64_t limit)
{
  int ret = OB_SUCCESS;
  State *new_state = NULL;
  if (limit <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(new_state = alloc_state_(limit))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    PALF_LOG(ERROR, "allocate pending log limiter state failed", KR(ret), K(limit));
  } else {
    release_state_ref_(state_);
    state_ = new_state;
  }
  return ret;
}

void LogPendingBufferLimiter::destroy()
{
  release_state_ref_(state_);
}

bool LogPendingBufferLimiter::try_acquire(const int64_t bytes,
                                          State *&reservation_state)
{
  bool acquired = false;
  State *state = state_;
  reservation_state = NULL;
  if (bytes > 0 && NULL != state && state->limit_ > 0) {
    int64_t old_val = ATOMIC_LOAD(&state->pending_bytes_);
    while (old_val <= state->limit_ - bytes) {
      if (ATOMIC_BCAS(&state->pending_bytes_, old_val, old_val + bytes)) {
        ATOMIC_INC(&state->ref_count_);
        reservation_state = state;
        acquired = true;
        break;
      }
      old_val = ATOMIC_LOAD(&state->pending_bytes_);
      PAUSE();
    }
  }
  return acquired;
}

void LogPendingBufferLimiter::release(State *&reservation_state,
                                      const int64_t bytes)
{
  if (NULL != reservation_state && bytes > 0) {
    const int64_t remain = ATOMIC_SAF(&reservation_state->pending_bytes_, bytes);
    OB_ASSERT(remain >= 0);
    release_state_ref_(reservation_state);
  }
}

int64_t LogPendingBufferLimiter::get_pending_bytes() const
{
  return NULL == state_ ? 0 : ATOMIC_LOAD(&state_->pending_bytes_);
}

int64_t LogPendingBufferLimiter::get_limit() const
{
  return NULL == state_ ? 0 : state_->limit_;
}

LogBufferSegment::LogBufferSegment()
{}

LogBufferSegment::~LogBufferSegment()
{
  if (NULL != limiter_state_ && reserved_bytes_ > 0) {
    LogPendingBufferLimiter::release(limiter_state_, reserved_bytes_);
  }
  limiter_state_ = NULL;
  reserved_bytes_ = 0;
  next_ = NULL;
}

int LogBufferSegment::alloc_segment_(LogBufferSegment *&segment)
{
  int ret = OB_SUCCESS;
  void *buf = server_malloc(sizeof(LogBufferSegment), "PalfLogSegment");
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    segment = new (buf) LogBufferSegment();
  }
  return ret;
}

void LogBufferSegment::free_segment_(LogBufferSegment *segment)
{
  if (NULL != segment) {
    segment->~LogBufferSegment();
    server_free(segment);
  }
}

int LogBufferSegment::create_normal(const LSN &entry_lsn,
                                    const SCN &scn,
                                    PalfLogBuffer &owner,
                                    LogPendingBufferLimiter *limiter,
                                    const int64_t reserved_bytes,
                                    LogBufferSegment *&segment)
{
  int ret = OB_SUCCESS;
  segment = NULL;
  if (reserved_bytes != owner.get_capacity()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(prepare_normal(owner, limiter, segment))) {
    if (OB_EAGAIN != ret) {
      PALF_LOG(WARN, "prepare normal log segment failed", K(ret));
    }
  } else if (OB_FAIL(segment->bind_normal(entry_lsn, scn))) {
    PALF_LOG(WARN, "init normal log segment failed", K(ret), K(entry_lsn), K(scn));
    int tmp_ret = segment->move_owner_to(owner);
    if (OB_SUCCESS != tmp_ret) {
      PALF_LOG(ERROR, "restore normal log owner failed", K(tmp_ret), KPC(segment));
    }
    free_segment_(segment);
    segment = NULL;
  }
  return ret;
}

int LogBufferSegment::create_padding(const LSN &entry_lsn,
                                     const SCN &scn,
                                     const int64_t log_body_size,
                                     LogPendingBufferLimiter *limiter,
                                     LogBufferSegment *&segment)
{
  int ret = OB_SUCCESS;
  segment = NULL;
  if (OB_FAIL(prepare_padding(limiter, segment))) {
    if (OB_EAGAIN != ret) {
      PALF_LOG(WARN, "prepare padding log segment failed", K(ret));
    }
  } else if (OB_FAIL(segment->bind_padding(entry_lsn, scn, log_body_size))) {
    PALF_LOG(WARN, "init padding log segment failed", K(ret), K(entry_lsn), K(log_body_size));
    free_segment_(segment);
    segment = NULL;
  }
  return ret;
}

int LogBufferSegment::prepare_normal(PalfLogBuffer &owner,
                                     LogPendingBufferLimiter *limiter,
                                     LogBufferSegment *&segment)
{
  int ret = OB_SUCCESS;
  segment = NULL;
  if (OB_FAIL(alloc_segment_(segment))) {
    PALF_LOG(WARN, "alloc normal log segment failed", K(ret));
  } else if (OB_FAIL(segment->prepare_normal_(owner, limiter, owner.get_capacity()))) {
    if (OB_EAGAIN != ret) {
      PALF_LOG(WARN, "prepare normal log segment failed", K(ret), K(owner));
    }
    free_segment_(segment);
    segment = NULL;
  }
  return ret;
}

int LogBufferSegment::prepare_imported_group(PalfLogBuffer &owner,
                                             const LSN &body_lsn,
                                             const int64_t group_checksum,
                                             LogPendingBufferLimiter *limiter,
                                             LogBufferSegment *&segment)
{
  int ret = OB_SUCCESS;
  segment = NULL;
  if (OB_FAIL(alloc_segment_(segment))) {
    PALF_LOG(WARN, "alloc imported group segment failed", K(ret));
  } else if (OB_FAIL(segment->prepare_imported_group_(
                 owner, body_lsn, group_checksum, limiter, owner.get_capacity()))) {
    if (OB_EAGAIN != ret) {
      PALF_LOG(WARN, "prepare imported group segment failed", K(ret), K(body_lsn), K(owner));
    }
    free_segment_(segment);
    segment = NULL;
  }
  return ret;
}

int LogBufferSegment::prepare_padding(LogPendingBufferLimiter *limiter,
                                      LogBufferSegment *&segment)
{
  int ret = OB_SUCCESS;
  segment = NULL;
  if (OB_FAIL(alloc_segment_(segment))) {
    PALF_LOG(WARN, "alloc padding log segment failed", K(ret));
  } else if (OB_FAIL(segment->prepare_padding_(limiter))) {
    if (OB_EAGAIN != ret) {
      PALF_LOG(WARN, "prepare padding log segment failed", K(ret));
    }
    free_segment_(segment);
    segment = NULL;
  }
  return ret;
}

int LogBufferSegment::prepare_normal_(PalfLogBuffer &owner,
                                      LogPendingBufferLimiter *limiter,
                                      const int64_t reserved_bytes)
{
  int ret = OB_SUCCESS;
  LogPendingBufferLimiter::State *limiter_state = NULL;
  if (!owner.is_valid() || !owner.is_sealed()
      || NULL == owner.get_prefix_buf(LogEntryHeader::HEADER_SER_SIZE)
      || NULL == limiter || reserved_bytes <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!limiter->try_acquire(reserved_bytes, limiter_state)) {
    ret = OB_EAGAIN;
  } else if (OB_FAIL(owner_.move_from(owner))) {
    LogPendingBufferLimiter::release(limiter_state, reserved_bytes);
    PALF_LOG(WARN, "move log owner failed", K(ret));
  } else {
    is_padding_ = false;
    limiter_state_ = limiter_state;
    reserved_bytes_ = reserved_bytes;
  }
  return ret;
}

int LogBufferSegment::prepare_imported_group_(PalfLogBuffer &owner,
                                              const LSN &body_lsn,
                                              const int64_t group_checksum,
                                              LogPendingBufferLimiter *limiter,
                                              const int64_t reserved_bytes)
{
  int ret = OB_SUCCESS;
  LogPendingBufferLimiter::State *limiter_state = NULL;
  if (!body_lsn.is_valid() || !owner.is_valid() || !owner.is_sealed()
      || 0 != owner.get_prefix_size() || owner.get_size() <= 0
      || owner.get_size() != reserved_bytes || NULL == limiter || reserved_bytes <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!limiter->try_acquire(reserved_bytes, limiter_state)) {
    ret = OB_EAGAIN;
  } else if (OB_FAIL(owner_.move_from(owner))) {
    LogPendingBufferLimiter::release(limiter_state, reserved_bytes);
    PALF_LOG(WARN, "move imported group owner failed", K(ret));
  } else {
    begin_lsn_ = body_lsn;
    entry_size_ = owner_.get_size();
    valid_size_ = entry_size_;
    is_padding_ = false;
    is_imported_group_ = true;
    data_checksum_ = group_checksum;
    limiter_state_ = limiter_state;
    reserved_bytes_ = reserved_bytes;
  }
  return ret;
}

int LogBufferSegment::prepare_padding_(LogPendingBufferLimiter *limiter)
{
  int ret = OB_SUCCESS;
  const int64_t reserved_bytes = LogEntryHeader::PADDING_LOG_ENTRY_SIZE;
  LogPendingBufferLimiter::State *limiter_state = NULL;
  if (NULL == limiter) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!limiter->try_acquire(reserved_bytes, limiter_state)) {
    ret = OB_EAGAIN;
  } else if (OB_FAIL(owner_.init(reserved_bytes, 0))) {
    LogPendingBufferLimiter::release(limiter_state, reserved_bytes);
    PALF_LOG(WARN, "alloc padding body failed", K(ret));
  } else {
    is_padding_ = true;
    limiter_state_ = limiter_state;
    reserved_bytes_ = reserved_bytes;
  }
  return ret;
}

int LogBufferSegment::bind_normal(const LSN &entry_lsn, const SCN &scn)
{
  int ret = OB_SUCCESS;
  LogEntryHeader entry_header;
  char *header_buf = owner_.get_prefix_buf(LogEntryHeader::HEADER_SER_SIZE);
  int64_t pos = 0;
  if (!entry_lsn.is_valid() || !scn.is_valid() || begin_lsn_.is_valid()
      || !owner_.is_valid() || !owner_.is_sealed() || NULL == header_buf
      || is_padding_ || NULL == limiter_state_ || reserved_bytes_ <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(entry_header.generate_header(owner_.get_buf(), owner_.get_size(), scn))) {
    PALF_LOG(WARN, "generate entry header failed", K(ret), K(entry_lsn), K(scn));
  } else if (OB_FAIL(entry_header.serialize(header_buf,
                                            LogEntryHeader::HEADER_SER_SIZE,
                                            pos))) {
    PALF_LOG(WARN, "serialize entry header failed", K(ret), K(entry_header));
  } else {
    begin_lsn_ = entry_lsn;
    entry_size_ = LogEntryHeader::HEADER_SER_SIZE + owner_.get_size();
    valid_size_ = entry_size_;
    data_checksum_ = entry_header.get_data_checksum();
  }
  return ret;
}

int LogBufferSegment::bind_padding(const LSN &entry_lsn,
                                   const SCN &scn,
                                   const int64_t log_body_size)
{
  int ret = OB_SUCCESS;
  if (!entry_lsn.is_valid() || !scn.is_valid() || begin_lsn_.is_valid()
      || log_body_size < LogEntryHeader::PADDING_LOG_ENTRY_SIZE
      || !owner_.is_valid() || owner_.is_sealed() || !is_padding_
      || NULL == limiter_state_
      || reserved_bytes_ != LogEntryHeader::PADDING_LOG_ENTRY_SIZE) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    MEMSET(owner_.get_buf(), PADDING_LOG_CONTENT_CHAR, LogEntryHeader::PADDING_LOG_ENTRY_SIZE);
    if (OB_FAIL(LogEntryHeader::generate_padding_log_buf(
            log_body_size - LogEntryHeader::HEADER_SER_SIZE,
            scn,
            owner_.get_buf(),
            LogEntryHeader::PADDING_LOG_ENTRY_SIZE))) {
      PALF_LOG(WARN, "generate padding body failed", K(ret), K(log_body_size));
    } else if (OB_FAIL(owner_.seal(LogEntryHeader::PADDING_LOG_ENTRY_SIZE))) {
      PALF_LOG(WARN, "seal padding body failed", K(ret), K(log_body_size));
    } else {
      begin_lsn_ = entry_lsn;
      entry_size_ = log_body_size;
      valid_size_ = LogEntryHeader::PADDING_LOG_ENTRY_SIZE;
      data_checksum_ = 0;
    }
  }
  return ret;
}

const char *LogBufferSegment::get_entry_buf() const
{
  const char *buf = NULL;
  if (owner_.is_valid()) {
    buf = (0 == owner_.get_prefix_size())
        ? owner_.get_buf()
        : owner_.get_prefix_buf(LogEntryHeader::HEADER_SER_SIZE);
  }
  return buf;
}

int LogBufferSegment::move_owner_to(PalfLogBuffer &owner)
{
  int ret = OB_SUCCESS;
  if (!owner_.is_valid() || owner.is_valid()) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_FAIL(owner.move_from(owner_))) {
    PALF_LOG(WARN, "move segment owner back failed", K(ret));
  } else {
    LogPendingBufferLimiter::release(limiter_state_, reserved_bytes_);
    limiter_state_ = NULL;
    reserved_bytes_ = 0;
  }
  return ret;
}

void LogBufferSegment::destroy_list(LogBufferSegment *head)
{
  while (NULL != head) {
    LogBufferSegment *next = head->next_;
    free_segment_(head);
    head = next;
  }
}

LogGroupWriteBuf::LogGroupWriteBuf()
{}

LogGroupWriteBuf::~LogGroupWriteBuf()
{
  reset();
}

int LogGroupWriteBuf::init(const LogGroupEntryHeader &header,
                           LogBufferSegment *head,
                           LogBufferSegment *tail,
                           const int64_t data_len)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (!header.is_valid() || NULL == head || NULL == tail || data_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(header.serialize(header_buf_, sizeof(header_buf_), pos))) {
    PALF_LOG(WARN, "serialize group header failed", K(ret), K(header));
  } else if (pos != LogGroupEntryHeader::HEADER_SER_SIZE) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    head_ = head;
    tail_ = tail;
    data_len_ = data_len;
    is_inited_ = true;
  }
  return ret;
}

int LogGroupWriteBuf::move_from(LogGroupWriteBuf &other)
{
  int ret = OB_SUCCESS;
  if (this == &other) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    reset();
    MEMCPY(header_buf_, other.header_buf_, sizeof(header_buf_));
    head_ = other.head_;
    tail_ = other.tail_;
    data_len_ = other.data_len_;
    is_inited_ = other.is_inited_;
    other.head_ = NULL;
    other.tail_ = NULL;
    other.data_len_ = 0;
    other.is_inited_ = false;
    MEMSET(other.header_buf_, 0, sizeof(other.header_buf_));
  }
  return ret;
}

int LogGroupWriteBuf::build_write_buf(LogWriteBuf &write_buf) const
{
  int ret = OB_SUCCESS;
  int64_t actual_data_len = 0;
  write_buf.reset();
  if (!is_valid()) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(write_buf.push_back(header_buf_, LogGroupEntryHeader::HEADER_SER_SIZE))) {
    PALF_LOG(WARN, "push group header failed", K(ret));
  } else {
    for (LogBufferSegment *segment = head_; OB_SUCC(ret) && NULL != segment;
         segment = segment->get_next()) {
      if (OB_FAIL(write_buf.push_back(segment->get_entry_buf(), segment->get_valid_size()))) {
        PALF_LOG(WARN, "push log segment failed", K(ret), KPC(segment));
      } else {
        actual_data_len += segment->get_valid_size();
        if (segment->is_padding()
            && segment->get_entry_size() > segment->get_valid_size()) {
          const int64_t fill_len = segment->get_entry_size() - segment->get_valid_size();
          if (OB_FAIL(write_buf.push_fill(PADDING_LOG_CONTENT_CHAR, fill_len))) {
            PALF_LOG(WARN, "push padding fill fragment failed", K(ret), K(fill_len));
          } else {
            actual_data_len += fill_len;
          }
        }
      }
    }
    if (OB_SUCC(ret) && actual_data_len != data_len_) {
      ret = OB_ERR_UNEXPECTED;
      PALF_LOG(ERROR, "group segment length mismatch", K(ret), K(actual_data_len), K_(data_len));
    }
  }
  return ret;
}

void LogGroupWriteBuf::take_segments(LogBufferSegment *&head,
                                     LogBufferSegment *&tail,
                                     int64_t &data_len)
{
  head = head_;
  tail = tail_;
  data_len = data_len_;
  head_ = NULL;
  tail_ = NULL;
  data_len_ = 0;
  is_inited_ = false;
  MEMSET(header_buf_, 0, sizeof(header_buf_));
}

void LogGroupWriteBuf::reset()
{
  LogBufferSegment::destroy_list(head_);
  head_ = NULL;
  tail_ = NULL;
  data_len_ = 0;
  is_inited_ = false;
  MEMSET(header_buf_, 0, sizeof(header_buf_));
}

bool LogGroupWriteBuf::is_valid() const
{
  return is_inited_ && NULL != head_ && NULL != tail_ && data_len_ > 0;
}

} // namespace palf
} // namespace oceanbase
