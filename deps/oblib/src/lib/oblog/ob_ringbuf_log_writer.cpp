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

#include "ob_ringbuf_log_writer.h"
#include "lib/lock/ob_scond.h"
#include "lib/thread/ob_thread_name.h"

using namespace oceanbase::lib;
extern "C" {
int ob_pthread_create(void **ptr, void *(*start_routine) (void *), void *arg);
void ob_pthread_join(void *ptr);
}

namespace oceanbase
{
namespace common
{

// ==================== ObRingBuf ====================

ObRingBuf::ObRingBuf()
  : buf_(nullptr),
    buf_len_(0),
    push_(0),
    pop_(0)
{}

int ObRingBuf::init(char *buf, int64_t buf_len)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_STDERR("ObRingBuf init: invalid buf=%p buf_len=%ld\n", buf, buf_len);
  } else {
    buf_ = buf;
    buf_len_ = buf_len;
    push_ = 0;
    pop_ = 0;
    alloc_lock_ = RingSpinLock();
    memset(buf_, 0, buf_len_);
  }
  return ret;
}

void ObRingBuf::destroy()
{
  buf_ = nullptr;
  buf_len_ = 0;
  push_ = 0;
  pop_ = 0;
  alloc_lock_ = RingSpinLock();
}

int64_t ObRingBuf::alloc(int64_t total_len)
{
  total_len = (total_len + 7) & ~7;  // 8-byte align

  alloc_lock_.lock();

  int64_t cur_push = push_;
  int64_t cur_pop  = ATOMIC_LOAD(&pop_);
  int64_t ring_off = cur_push % buf_len_;

  int64_t needed  = total_len;
  int64_t pad_len = 0;
  if (ring_off + total_len > buf_len_) {
    pad_len = buf_len_ - ring_off;
    needed += pad_len;
  }

  if (cur_push + needed - cur_pop > buf_len_) {
    alloc_lock_.unlock();
    return -1;
  }

  int64_t ret_pos;
  if (pad_len > 0) {
    RingBufEntry *pad = entry_at(ring_off);
    pad->total_len_ = pad_len;
    pad->type_ = RingBufEntry::TYPE_ROLLBACK;
    pad->busy_ = 0;

    RingBufEntry *real = entry_at(0);
    real->total_len_ = total_len;
    real->busy_ = 1;

    // Publish header (busy_/total_len_) before advancing push_, so a consumer
    // that observes the new push_ (acquire) is guaranteed to see the header.
    WEAK_BARRIER();
    push_ = cur_push + needed;
    ret_pos = cur_push + pad_len;
  } else {
    RingBufEntry *entry = entry_at(ring_off);
    entry->total_len_ = total_len;
    entry->busy_ = 1;

    // Publish header (busy_/total_len_) before advancing push_, so a consumer
    // that observes the new push_ (acquire) is guaranteed to see the header.
    WEAK_BARRIER();
    push_ = cur_push + needed;
    ret_pos = cur_push;
  }

  alloc_lock_.unlock();
  return ret_pos;
}

void ObRingBuf::commit(int64_t pos)
{
  RingBufEntry *entry = entry_at(pos % buf_len_);
  entry->type_ = RingBufEntry::TYPE_COMMIT;
  WEAK_BARRIER();
  entry->busy_ = 0;
}

void ObRingBuf::rollback(int64_t pos)
{
  RingBufEntry *entry = entry_at(pos % buf_len_);
  entry->type_ = RingBufEntry::TYPE_ROLLBACK;
  WEAK_BARRIER();
  entry->busy_ = 0;
}

bool ObRingBuf::is_queue_full() const
{
  return ATOMIC_LOAD(&push_) - ATOMIC_LOAD(&pop_) >= buf_len_;
}

int64_t ObRingBuf::get_queue_depth() const
{
  return ATOMIC_LOAD(&push_) - ATOMIC_LOAD(&pop_);
}

void ObRingBuf::advance_pop(int64_t new_pop)
{
  if (new_pop > ATOMIC_LOAD(&pop_)) {
    ATOMIC_STORE(&pop_, new_pop);
  }
}

int64_t ObRingBuf::get_pop() const
{
  return ATOMIC_LOAD(&pop_);
}

int64_t ObRingBuf::get_push() const
{
  return ATOMIC_LOAD_ACQ(&push_);
}

// ==================== ObRingBufLogWriter ====================

ObRingBufLogWriter::ObRingBufLogWriter()
  : has_stopped_(true),
    is_inited_(false),
    group_commit_max_wait_us_(0),
    ringbuf_buf_(nullptr),
    flush_cond_(nullptr),
    flush_tid_(NULL)
{
  memset(thread_name_, 0, sizeof(thread_name_));
}

ObRingBufLogWriter::~ObRingBufLogWriter()
{}

int ObRingBufLogWriter::init(int64_t group_commit_max_wait_us, const char *thread_name)
{
  int ret = OB_SUCCESS;
  ObMemAttr attr(OB_SERVER_TENANT_ID, "RingBufLogWr");

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_STDERR("ObRingBufLogWriter has been inited.\n");
  } else if (OB_UNLIKELY(group_commit_max_wait_us <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_STDERR("Invalid argument, group_commit_max_wait_us=%ld.\n", group_commit_max_wait_us);
  } else {
    char *ringbuf_buf = static_cast<char *>(ob_malloc(RINGBUF_SIZE, attr));
    if (NULL == ringbuf_buf) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_STDERR("Fail to allocate ring buffer, size=%lu.\n", RINGBUF_SIZE);
    } else if (OB_FAIL(ringbuf_.init(ringbuf_buf, RINGBUF_SIZE))) {
      LOG_STDERR("Fail to init ObRingBuf, ret=%d.\n", ret);
      ob_free(ringbuf_buf);
    } else if (OB_ISNULL(flush_cond_ = OB_NEW(SimpleCond, attr))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_STDERR("Fail to allocate flush_cond_.\n");
      ob_free(ringbuf_buf);
      ringbuf_.destroy();
    } else {
      ringbuf_buf_ = ringbuf_buf;
      group_commit_max_wait_us_ = group_commit_max_wait_us;
      strncpy(thread_name_, thread_name, MAX_THREAD_NAME_LEN);
      thread_name_[MAX_THREAD_NAME_LEN] = '\0';
      has_stopped_ = false;
      is_inited_ = true;
      if (0 != ob_pthread_create(&flush_tid_,
                                 ObRingBufLogWriter::flush_thread, this)) {
        ret = OB_ERR_SYS;
        LOG_STDERR("Fail to create flush thread.\n");
        has_stopped_ = true;
      }
    }
  }
  return ret;
}

void ObRingBufLogWriter::stop()
{
  has_stopped_ = true;
  if (OB_NOT_NULL(flush_cond_)) {
    flush_cond_->signal(UINT32_MAX);
  }
}

void ObRingBufLogWriter::wait()
{
  if (NULL != flush_tid_) {
    ob_pthread_join(flush_tid_);
    flush_tid_ = NULL;
  }
}

void ObRingBufLogWriter::destroy()
{
  if (OB_NOT_NULL(flush_cond_)) {
    OB_DELETE(SimpleCond, "RingBufLogWr", flush_cond_);
    flush_cond_ = NULL;
  }
  if (ringbuf_buf_ != nullptr) {
    ob_free(ringbuf_buf_);
    ringbuf_buf_ = nullptr;
  }
  ringbuf_.destroy();
  is_inited_ = false;
  has_stopped_ = true;
}

int64_t ObRingBufLogWriter::alloc(int64_t total_len)
{
  return ringbuf_.alloc(total_len);
}

void ObRingBufLogWriter::commit(int64_t pos)
{
  ringbuf_.commit(pos);
  if (OB_NOT_NULL(flush_cond_)) {
    flush_cond_->signal(1);
  }
}

void ObRingBufLogWriter::rollback(int64_t pos)
{
  ringbuf_.rollback(pos);
  if (OB_NOT_NULL(flush_cond_)) {
    flush_cond_->signal(1);
  }
}

bool ObRingBufLogWriter::is_queue_full()
{
  return ringbuf_.is_queue_full();
}

void *ObRingBufLogWriter::flush_thread(void *arg)
{
  if (OB_ISNULL(arg)) {
    LOG_STDERR("invalid argument, arg = %p\n", arg);
  } else {
    ObRingBufLogWriter *writer = reinterpret_cast<ObRingBufLogWriter *>(arg);
    lib::set_thread_name(writer->thread_name_);
    writer->flush_loop();
  }
  return NULL;
}

void ObRingBufLogWriter::flush_loop()
{
  while (!has_stopped_) {
    // Snapshot key BEFORE do_flush: if a commit() fires between do_flush
    // returning false and wait(), the key bump ensures we don't miss it.
    const uint32_t key = flush_cond_->get_key();
    if (!do_flush()) {
      if (!has_stopped_) {
        flush_cond_->wait(key, group_commit_max_wait_us_);
      }
    }
  }
}

bool ObRingBufLogWriter::do_flush()
{
  static const int64_t BATCH_SIZE = 64;
  char *entries[BATCH_SIZE];
  int64_t lens[BATCH_SIZE];
  bool did_work = false;

  while (!has_stopped_) {
    int64_t item_cnt = 0;
    int64_t cur_pop = ringbuf_.get_pop();
    int64_t cur_push = ringbuf_.get_push();

    while (item_cnt < BATCH_SIZE && cur_pop < cur_push) {
      int64_t ring_off = cur_pop % RINGBUF_SIZE;
      uint64_t hdr_raw = ATOMIC_LOAD_ACQ(reinterpret_cast<uint64_t *>(ringbuf_.entry_at(ring_off)));
      const RingBufEntry *hdr = reinterpret_cast<const RingBufEntry *>(&hdr_raw);
      if (hdr->busy_) break;
      int64_t total_len = hdr->total_len_;
      if (hdr->type_ == RingBufEntry::TYPE_COMMIT) {
        entries[item_cnt] = ringbuf_.data_of(cur_pop);
        lens[item_cnt] = total_len;
        ++item_cnt;
      }
      cur_pop += total_len;
    }

    if (item_cnt > 0) {
      process_batch(entries, lens, item_cnt);
      ringbuf_.advance_pop(cur_pop);
      did_work = true;
    } else {
      ringbuf_.advance_pop(cur_pop);
      break;
    }
  }

  return did_work;
}

}
}
