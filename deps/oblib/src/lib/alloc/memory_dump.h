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

#ifndef OCEANBASE_DUMP_MEMORY_H_
#define OCEANBASE_DUMP_MEMORY_H_

#include "lib/alloc/ob_malloc_sample_struct.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/lock/ob_thread_cond.h"
#include "lib/rc/context.h"
#include "lib/thread/thread_pool.h"

// This file will be placed under lib for a short period of time to facilitate unit testing. After the function is stable, move to ob
// The corresponding MySimpleThreadPool will also be deleted

namespace oceanbase
{
namespace observer
{
class ObAllVirtualMemoryInfo;
class ObMallocSampleInfo;
}
namespace lib
{
struct AChunk;
struct ABlock;
struct AObject;
class ObCtxAllocator;
class ObCtxAllocatorState;
}
namespace common
{
enum DumpType
{
  DUMP_CONTEXT,
  DUMP_CHUNK,
  STAT_LABEL
};

struct ObMemoryCheckContext
{
  enum CheckType
  {
    SQL_MEMORY_LEAK,
  };
  ObMemoryCheckContext(CheckType type = SQL_MEMORY_LEAK)
    : ret_(OB_SUCCESS), type_(type), cond_()
  {}
  bool is_sql_memory_leak() { return SQL_MEMORY_LEAK == type_; }
  int ret_;
  CheckType type_;
  ObThreadCond cond_;
};

class ObMemoryDumpTask
{
public:
  TO_STRING_KV(K(type_), K(dump_all_), KP(p_context_), K(slot_idx_),
               K(dump_ctx_), K(ctx_id_), KP(p_chunk_));
  DumpType type_;
  bool dump_all_;
  union
  {
    struct {
      void *p_context_;
      int slot_idx_;
    };
    struct {
      bool dump_ctx_;
      union {
        struct {
          
          int64_t ctx_id_;
        };
        void *p_chunk_;
      };
    };
    struct {
      ObMemoryCheckContext *memory_check_ctx_;
    };
  };
};

struct LabelItem
{
  LabelItem()
  {
    MEMSET(this, 0 , sizeof(*this));
  }
  char str_[lib::AOBJECT_LABEL_SIZE + 1];
  int32_t str_len_;

  int32_t count_;
  int32_t block_cnt_;
  int32_t chunk_cnt_;
  int64_t hold_;
  int64_t used_;
  LabelItem &operator +=(const LabelItem &item)
  {
    hold_ += item.hold_;
    used_ += item.used_;
    count_ += item.count_;
    return *this;
  }
};
struct LabelInfoItem
{
  LabelInfoItem()
  {}
  LabelInfoItem(LabelItem* litem, void *chunk, void *block)
    : litem_(litem), chunk_(chunk), block_(block)
  {}
  LabelItem* litem_;
  void *chunk_;
  void *block_;
};

typedef common::hash::ObHashMap<std::pair<uint64_t, uint64_t>, LabelInfoItem, hash::NoPthreadDefendMode> LabelMap;

using lib::AChunk;
using lib::ABlock;
using lib::AObject;
class ObMemoryDump : public lib::ThreadPool
{
public:
  static constexpr const char *LOG_FILE = "log/memory_meta";
  typedef void (*SharedWorkerNotifyFunc)(void *);
private:
friend class observer::ObAllVirtualMemoryInfo;
friend class lib::ObCtxAllocator;
friend class lib::ObCtxAllocatorState;
friend class lib::ObMallocAllocator;

static const int PENDING_STAT_LABEL = 1;
static const int PENDING_DUMP = 2;
static const int PRINT_BUF_LEN = 64L << 10;
static const int64_t MAX_MEMORY = 128L << 30; // 1T
static const int MAX_CHUNK_CNT = MAX_MEMORY / (2L << 20);
static const int MAX_LABEL_ITEM_CNT = 4L << 10;
static const int64_t STAT_LABEL_INTERVAL = INT64_MAX;

struct CtxRange
{
  static bool compare(const CtxRange &tcr,
                      const uint64_t cmp_ctx_id)
  {
    return tcr.ctx_id_ < cmp_ctx_id;
  }
  
  uint64_t ctx_id_;
  // [start_, end_)
  int start_;
  int end_;
};

struct Stat {
  LabelItem up2date_items_[MAX_LABEL_ITEM_CNT];
  CtxRange tcrs_[ObCtxIds::MAX_CTX_ID];
  lib::ObMallocSampleMap  malloc_sample_map_;
  int tcr_cnt_ = 0;
};

struct PreAllocMemory
{
  char print_buf_[PRINT_BUF_LEN];
  char array_buf_[MAX_CHUNK_CNT * sizeof(void*)];
  char stats_buf_[sizeof(Stat) * 2];
};

public:
  ObMemoryDump();
  ~ObMemoryDump();
  static ObMemoryDump &get_instance();
  int init();
  void stop();
  void wait();
  void destroy();
  bool is_inited() const { return is_inited_; }
  bool is_using_shared_worker() const { return use_shared_worker_; }
  int request_dump(const ObMemoryDumpTask &task);
  int generate_mod_stat_task();
  int set_shared_worker_notifier(
      SharedWorkerNotifyFunc notify_func,
      void *notify_arg,
      bool &has_pending);
  void clear_shared_worker_notifier();
  int process_one_pending_batch(
      int64_t &processed_count,
      bool &has_more);
  int load_malloc_sample_map(lib::ObMallocSampleMap &malloc_sample_map)
  {
    int ret = OB_SUCCESS;
    if (is_inited_) {
      ObLatchRGuard guard(iter_lock_, ObLatchIds::MEM_DUMP_ITER_LOCK);
      lib::ObMallocSampleMap &map = r_stat_->malloc_sample_map_;
      for (lib::ObMallocSampleMap::iterator it = map.begin(); OB_SUCC(ret) && it != map.end();
           ++it) {
        ret = malloc_sample_map.set_refactored(it->first, it->second);
      }
    }
    return ret;
  }

private:
  void run1() override;
  void signal_stop();
  void handle(void *task);

  void print_malloc_sample_info();
private:
  AChunk *find_chunk(void *ptr);
private:
  ObThreadCond cond_;
  int pending_;
  ObMemoryDumpTask pending_dump_task_;
  char *print_buf_;
  union {
    void *array_;
    AChunk **chunks_;
  };
  lib::MemoryContext dump_context_;
  LabelMap lmap_;
  common::ObLatch iter_lock_;
  Stat *r_stat_;
  Stat *w_stat_;
  int huge_segv_cnt_;
  bool use_shared_worker_;
  SharedWorkerNotifyFunc shared_worker_notify_func_;
  void *shared_worker_notify_arg_;
  bool is_inited_;
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_DUMP_MEMORY_H_
