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

#ifndef OB_RINGBUF_LOG_WRITER_H_
#define OB_RINGBUF_LOG_WRITER_H_

#include <stdint.h>
#include <stdlib.h>

#include "lib/ob_define.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/atomic/ob_atomic.h"

namespace oceanbase
{
namespace common
{

struct SimpleCond;

// TTAS spinlock: test-then-test-and-set. Waiters do plain reads (shared
// cache line), CAS only when lock appears free. Avoids cache-line bouncing
// of repeated lock cmpxchg.
class RingSpinLock {
public:
  RingSpinLock() : lock_(0) {}
  void lock() {
    while (true) {
      if (!ATOMIC_LOAD(&lock_)) {
        uint8_t expected = 0;
        if (ATOMIC_BCAS(&lock_, expected, 1)) {
          return;
        }
      }
      PAUSE();
    }
  }
  void unlock() { ATOMIC_STORE(&lock_, 0); }
private:
  uint8_t lock_;
};

// 8-byte entry header.
//   type_:      2 bits  (COMMIT/ROLLBACK)
//   total_len_: 30 bits (entry length including header, max ~1GB)
//   busy_:      1 bit   (1 = producer writing, 0 = committed/rolled back)
//   reserved_:  31 bits
struct RingBufEntry {
  uint64_t type_      : 2;
  uint64_t total_len_ : 30;
  uint64_t busy_      : 1;
  uint64_t reserved_  : 31;
  char data_[0];

  static const uint64_t TYPE_COMMIT  = 0;
  static const uint64_t TYPE_ROLLBACK = 1;

};

static_assert(sizeof(RingBufEntry) == 8, "RingBufEntry must be exactly 8 bytes");

// MPSC ring buffer: spinlock-protected alloc + BUSY_BIT commit.
// External buf + buf_len. No threads, no condition variables.
class ObRingBuf {
public:
  ObRingBuf();
  int init(char *buf, int64_t buf_len);
  void destroy();

  // Producer API (multi-thread safe)
  // Returns pos (>= 0) for commit/rollback, or -1 if full.
  int64_t alloc(int64_t total_len);
  void commit(int64_t pos);
  void rollback(int64_t pos);
  char *data_of(int64_t pos) const {
    return entry_at(pos % buf_len_)->data_;
  }

  // Consumer API (single-thread)
  int64_t get_pop() const;
  int64_t get_push() const;
  void advance_pop(int64_t new_pop);
  RingBufEntry *entry_at(int64_t ring_off) const {
    return reinterpret_cast<RingBufEntry *>(buf_ + ring_off);
  }

  // State queries
  bool is_queue_full() const;
  int64_t get_queue_depth() const;

private:
  char   *buf_;
  int64_t buf_len_;
  int64_t push_ CACHE_ALIGNED;
  int64_t pop_ CACHE_ALIGNED;
  RingSpinLock alloc_lock_ CACHE_ALIGNED;
};

// Async wrapper: ObRingBuf + flush thread + SimpleCond.
// Subclass implements process_batch().
class ObRingBufLogWriter {
public:
  static const uint64_t RINGBUF_SIZE = 2 * 1024 * 1024;
  static const uint64_t ENTRY_HEADER_SIZE = sizeof(RingBufEntry);
  static const uint64_t MAX_ENTRY_SIZE = ENTRY_HEADER_SIZE + 65536 + 64;

  ObRingBufLogWriter();
  virtual ~ObRingBufLogWriter();

  int init(int64_t group_commit_max_wait_us, const char *thread_name);
  void stop();
  void wait();
  void destroy();

  // Producer API (multi-thread safe, delegates to ObRingBuf)
  int64_t alloc(int64_t total_len);
  void rollback(int64_t pos);
  void commit(int64_t pos);
  char *data_of(int64_t pos) { return ringbuf_.data_of(pos); }

  // State queries
  bool is_inited() const { return is_inited_; }
  bool has_stopped() const { return has_stopped_; }
  const char *get_thread_name() const { return thread_name_; }
  bool is_queue_full();
  int64_t get_queue_depth() const { return ringbuf_.get_queue_depth(); }

protected:
  virtual void process_batch(char **entries, int64_t *lens, int64_t count) = 0;

private:
  static void *flush_thread(void *arg);
  void flush_loop();
  bool do_flush();

  bool       has_stopped_;
  bool       is_inited_;
  int64_t    group_commit_max_wait_us_;
  static const uint64_t MAX_THREAD_NAME_LEN = 9;
  char       thread_name_[MAX_THREAD_NAME_LEN + 1];

  ObRingBuf  ringbuf_;
  char      *ringbuf_buf_;
  SimpleCond *flush_cond_ CACHE_ALIGNED;
  void *flush_tid_;
};

}
}

#endif /* OB_RINGBUF_LOG_WRITER_H_ */
