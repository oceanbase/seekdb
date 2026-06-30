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

#define USING_LOG_PREFIX COMMON

#include "lib/alloc/memory_dump.h"
#include "lib/resource/achunk_mgr.h"
#ifdef _WIN32
#include <fcntl.h>
#endif
#ifndef _WIN32
#include <setjmp.h>
#endif
#include <utility>
#include "lib/signal/ob_signal_struct.h"
#include "lib/thread/thread_mgr.h"
#include "lib/container/ob_vector.h"

namespace oceanbase
{
using namespace lib;

namespace common
{


#ifndef _WIN32
RLOCAL(sigjmp_buf, jmp);

static void dump_handler(int sig, siginfo_t *s, void *p)
{
  siglongjmp(jmp, 1);
}

class DumpSignalGuard final
{
public:
  DumpSignalGuard()
  {
    install_dump_signal_handler();
  }

  ~DumpSignalGuard()
  {
    restore_dump_signal_handler();
  }

public:

  template<typename Function>
  void do_with_segv_catch(Function &&func, bool &has_segv, decltype(func()) &ret)
  {
    has_segv = false;
    if (installed_) {
      has_segv = false;
      int js = sigsetjmp(jmp, 1);
      if (0 == js) {
        ret = func();
      } else if (1 == js) {
        has_segv = true;
      } else {
        LOG_ERROR_RET(OB_ERR_UNEXPECTED, "unexpected error!!!", K(js));
        ob_abort();
      }
    } else {
      ret = func();
    }
  }

private:
  void install_dump_signal_handler()
  {
    int ret = OB_SUCCESS;
    struct sigaction sa_new;
    sa_new.sa_flags = SA_SIGINFO;
    sa_new.sa_sigaction = dump_handler;
    sigemptyset(&sa_new.sa_mask);
    installed_ = true;
    int i = 0;
    for (i = 0; OB_SUCC(ret) && i < ARRAYSIZEOF(signals_); i++) {
      if (sigaction(signals_[i], &sa_new, &sa_old_[i]) != 0) {
        ret = OB_ERR_SYS;
        LOG_WARN_RET(ret, "failed to install signal handler", K(errno));
      }
    }
    if (OB_SUCC(ret)) {
      installed_ = true;
    } else {
      installed_ = false;
      for (int j = 0; j < i; j++) {
        if (sigaction(signals_[j], &sa_old_[j], nullptr) != 0) {
          LOG_WARN_RET(OB_ERR_SYS, "failed to restore signal handler", K(errno));
        }
      }
    }
  }

  void restore_dump_signal_handler()
  {
    if (installed_) {
      for (int i = 0; i < ARRAYSIZEOF(signals_); i++) {
        if (sigaction(signals_[i], &sa_old_[i], nullptr) != 0) {
          LOG_WARN_RET(OB_ERR_SYS, "failed to restore signal handler", K(errno));
        }
      }
      installed_ = false;
    }
  }
private:
  static constexpr int signals_[] = {SIGSEGV, SIGABRT};
  struct sigaction sa_old_[ARRAYSIZEOF(signals_)];
  bool installed_ = false;
};

#else // _WIN32

class DumpSignalGuard final
{
public:
  DumpSignalGuard() = default;
  ~DumpSignalGuard() = default;

  template<typename Function>
  void do_with_segv_catch(Function &&func, bool &has_segv, decltype(func()) &ret)
  {
    has_segv = false;
    ret = func();
  }
};
#endif // _WIN32

ObMemoryDump::ObMemoryDump()
  : pending_(0),
    print_buf_(nullptr),
    dump_context_(nullptr),
    iter_lock_(),
    r_stat_(nullptr),
    w_stat_(nullptr),
    huge_segv_cnt_(0),
    is_inited_(false)
{
}

ObMemoryDump::~ObMemoryDump()
{
  if (is_inited_) {
    destroy();
  }
}

ObMemoryDump &ObMemoryDump::get_instance()
{
  static ObMemoryDump the_one;
  if (OB_UNLIKELY(!the_one.is_inited()) && REACH_TIME_INTERVAL(1 * 1000 * 1000)) {
    LOG_WARN_RET(OB_NOT_INIT, "memory dump not init");
  }
  return the_one;
}

int ObMemoryDump::init()
{
  int ret = OB_SUCCESS;
#ifndef OB_USE_ASAN
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_FAIL(cond_.init(ObWaitEventIds::DEFAULT_COND_WAIT))) {
  } else {
    MemoryContext context;// = nullptr;
    int ret = ROOT_CONTEXT->CREATE_CONTEXT(context, ContextParam().set_label("MemDumpContext"));
    PreAllocMemory *pre_mem = nullptr;
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(pre_mem = (PreAllocMemory*)context->allocp(sizeof(PreAllocMemory)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc mem failed", K(ret));
    } else {
      LOG_INFO("pre memory size", K(sizeof(PreAllocMemory)));
      print_buf_ = pre_mem->print_buf_;
      array_ = pre_mem->array_buf_;
      
      print_buf_ = pre_mem->print_buf_;
      if (OB_FAIL(lmap_.create(1000, ObMemAttr("MemDumpMap", ObCtxIds::DEFAULT_CTX_ID, OB_HIGH_ALLOC)))) {
      } else {
        r_stat_ = new (pre_mem->stats_buf_) Stat();
        w_stat_ = new (r_stat_ + 1) Stat();
        dump_context_ = context;
        is_inited_ = true;
        if (OB_FAIL(r_stat_->malloc_sample_map_.create(1000, ObMemAttr("MallocInfoMap", ObCtxIds::DEFAULT_CTX_ID, OB_HIGH_ALLOC)))) {
        } else if (OB_FAIL(w_stat_->malloc_sample_map_.create(1000, ObMemAttr("MallocInfoMap", ObCtxIds::DEFAULT_CTX_ID, OB_HIGH_ALLOC)))) {
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(TG_SET_RUNNABLE_AND_START(TGDefIDs::MEMORY_DUMP, *this))) {
        }
      }
    }
    if (OB_FAIL(ret) && context != nullptr) {
      DESTROY_CONTEXT(context);
      context = nullptr;
    }
  }
  if (OB_FAIL(ret)) {
    destroy();
  }
#else
// do nothing
#endif
  return ret;
}

void ObMemoryDump::stop()
{
  if (is_inited_) {
    {
      ObThreadCondGuard guard(cond_);
      cond_.signal();
    }
    TG_STOP(TGDefIDs::MEMORY_DUMP);
  }
}

void ObMemoryDump::wait()
{
  if (is_inited_) {
    TG_WAIT(TGDefIDs::MEMORY_DUMP);
  }
}

void ObMemoryDump::destroy()
{
  if (is_inited_) {
    stop();
    wait();
    cond_.destroy();
    is_inited_ = false;
  }
}

int ObMemoryDump::generate_mod_stat_task()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    ObThreadCondGuard guard(cond_);
    pending_ |= PENDING_STAT_LABEL;
    cond_.signal();
  }
  return ret;
}

int ObMemoryDump::request_dump(const ObMemoryDumpTask &task)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    ObThreadCondGuard guard(cond_);
    pending_dump_task_ = task;
    pending_ |= PENDING_DUMP;
    cond_.signal();
  }
  return ret;
}

void ObMemoryDump::print_malloc_sample_info()
{
  int ret = OB_SUCCESS;
  typedef ObSortedVector<ObMallocSamplePair*> MallocSamplePairVector;
  ObLatchRGuard guard(iter_lock_, ObLatchIds::MEM_DUMP_ITER_LOCK);
  ObMallocSampleMap &map = r_stat_->malloc_sample_map_;
  ObMemAttr attr("MallocSampleInf", ObCtxIds::DEFAULT_CTX_ID, lib::OB_HIGH_ALLOC);
  MallocSamplePairVector vector(map.size(), nullptr, attr);
  for (ObMallocSampleIter it = map.begin(); OB_SUCC(ret) && it != map.end(); ++it) {
    MallocSamplePairVector::iterator pos;
    ret = vector.insert(&(*it), pos, ObMallocSamplePairCmp());
  }
  int64_t log_pos = 0;
  
  int64_t ctx_id = ObCtxIds::DEFAULT_CTX_ID;
  const char *label = "";
  int64_t bt_cnt = 0;
  const int64_t MAX_LABEL_BT_CNT = 5;
  for (MallocSamplePairVector::iterator it = vector.begin(); OB_SUCC(ret) && it != vector.end(); ++it) {
    if ((*it)->first.ctx_id_ != ctx_id) {
      if (log_pos > 0) {
        _LOG_INFO("\n[MEMORY][BT] ctx_id=%25s\n%.*s",
              get_global_ctx_info().get_ctx_name(ctx_id), static_cast<int>(log_pos), print_buf_);
        log_pos = 0;
      }
      (void)(1UL);
      ctx_id = (*it)->first.ctx_id_;
      label = (*it)->first.label_;
      bt_cnt = 0;
    } else if (0 != STRCMP(label, (*it)->first.label_)) {
      label = (*it)->first.label_;
      bt_cnt = 0;
    }
    if (bt_cnt++ < MAX_LABEL_BT_CNT) {
      char bt[MAX_BACKTRACE_LENGTH];
      parray(bt, sizeof(bt), (int64_t*)(*it)->first.bt_, AOBJECT_BACKTRACE_COUNT);
      ret = databuff_printf(print_buf_, PRINT_BUF_LEN, log_pos, "[MEMORY][BT] mod=%15s, alloc_bytes=% '15ld, alloc_count=% '15ld, bt=%s\n",
            label, (*it)->second.alloc_bytes_, (*it)->second.alloc_count_, bt);
      if (OB_SUCC(ret) && log_pos > PRINT_BUF_LEN / 2) {
        _LOG_INFO("\n[MEMORY][BT] ctx_id=%25s\n%.*s",
            get_global_ctx_info().get_ctx_name(ctx_id), static_cast<int>(log_pos), print_buf_);
        log_pos = 0;
      }
    }
  }
  if (OB_SUCC(ret) && log_pos > 0) {
    _LOG_INFO("\n[MEMORY][BT] ctx_id=%25s\n%.*s",
        get_global_ctx_info().get_ctx_name(ctx_id), static_cast<int>(log_pos), print_buf_);
  }
}

void ObMemoryDump::signal_stop()
{
  ObThreadCondGuard guard(cond_);
  cond_.signal();
}

void ObMemoryDump::run1()
{
  SANITY_DISABLE_CHECK_RANGE();
  lib::set_thread_name("MemoryDump");
  int64_t last_stat_ts = 0;
  while (!has_set_stop()) {
    int pending = 0;
    ObMemoryDumpTask local_dump_task;

    {
      ObThreadCondGuard guard(cond_);
      if (0 == pending_) {
        cond_.wait(10 * 1000);  // 10s timeout, woke by signal or timeout
      }
      pending = pending_;
      local_dump_task = pending_dump_task_;
      pending_ = 0;
    }

    // event-driven: DUMP always
    if (pending & PENDING_DUMP) {
      handle(&local_dump_task);
    }

    // event-driven or timer-based STAT_LABEL
    ObMemoryDumpTask stat_task;
    stat_task.type_ = STAT_LABEL;
    if (pending & PENDING_STAT_LABEL) {
      handle(&stat_task);
      last_stat_ts = ObTimeUtility::current_time();
    } else if (ObTimeUtility::current_time() - last_stat_ts > STAT_LABEL_INTERVAL) {
      handle(&stat_task);
      last_stat_ts = ObTimeUtility::current_time();
    }
  }
}

AChunk *ObMemoryDump::find_chunk(void *ptr)
{
  AChunk *ret = nullptr;
  auto func = [ptr] () {
      AChunk *ret = nullptr;
      const static uint32_t magic = ABLOCK_MAGIC_CODE + 1;
      int offset = 16;
      char *start = (char*)ptr - offset;
      char *end = (char*)ptr + offset;
      while (true) {
        void *loc = std::search(start,
                                end,
                                (char*)&magic,
                                (char*)&magic + sizeof(magic));
        if (loc != nullptr) {
          ABlock *block = (ABlock*)loc;
          AChunk *chunk = block->chunk();
          if (chunk != nullptr && chunk->is_valid()) {
            ret = chunk;
            break;
          }
        }
        start -= offset;
        end -= offset;
      }
      return ret;
  };
  bool has_segv = false;
  DumpSignalGuard guard;
  guard.do_with_segv_catch(func, has_segv, ret);
  if (has_segv) {
    LOG_INFO("restore from sigsegv, let's goon~");
  }
  return ret;
}

template<typename BlockFunc, typename ObjectFunc>
int parse_block_meta(AChunk *chunk, ABlock *block, BlockFunc b_func, ObjectFunc o_func)
{
  int ret = OB_SUCCESS;
  ret = b_func(chunk, block);
  if (block->in_use_) {
    char *block_end = !block->is_large_ ?
      (char*)block->data() + block->hold() :
      (char*)block->chunk() + block->chunk()->hold();
    int loop_cnt = 0;
    AObject *object = (AObject*)(block->data());
    while (OB_SUCC(ret)) {
      if ((char*)object + AOBJECT_META_SIZE > block_end ||
          !object->is_valid() || loop_cnt++ >= AllocHelper::cells_per_block(block->ablock_size_)) {
        break;
      }
      ret = o_func(chunk, block, object);
      // is_large shows that there is only one ojbect in block, so we can break directly.
      if (object->is_large_ || object->is_last(AllocHelper::cells_per_block(block->ablock_size_))) {
        break;
      }
      object = object->phy_next(object->nobjs_);
      if (nullptr == object) {
        break;
      }
    }
  }
  return ret;
}

template<typename ChunkFunc, typename BlockFunc, typename ObjectFunc>
int parse_chunk_meta(AChunk *chunk, ChunkFunc c_func, BlockFunc b_func,
                     ObjectFunc o_func)
{
  int ret = OB_SUCCESS;
  ret = c_func(chunk);
  char *block_meta_end = (char*)chunk + ACHUNK_HEADER_SIZE;
  int loop_cnt = 0;
  int offset = 0;
  do {
    ABlock *block = chunk->offset2blk(offset);
    if ((char*)block + ABLOCK_HEADER_SIZE > block_meta_end ||
        !block->is_valid() ||
        offset >= BLOCKS_PER_CHUNK ||
        loop_cnt++ >= BLOCKS_PER_CHUNK) {
      break;
    }
    ret = parse_block_meta(chunk, block, b_func, o_func);
    int next_offset = -1;
    bool is_last = chunk->is_last_blk_offset(offset, &next_offset);
    if (is_last) break;
    offset = next_offset;
  } while (OB_SUCC(ret));
  return ret;
}

int print_chunk_meta(AChunk *chunk, char *buf, int64_t buf_len, int64_t &pos)
{
  return databuff_printf(buf, buf_len, pos,
                         "chunk: %p, alloc_bytes: %ld, all_size: %ld, hold_size: %ld, washed_size: %ld, washed_blks: %ld, "
                         "ablock_size: %d, block_set: %p\n",
                         chunk, chunk->alloc_bytes_, chunk->aligned(), chunk->hold(),
                         chunk->washed_size_, chunk->washed_blks_, ABLOCK_SIZE, chunk->block_set_);
}

int print_block_meta(AChunk *chunk, ABlock *block, char *buf, int64_t buf_len, int64_t &pos,
                     int fd)
{
  int ret = OB_SUCCESS;
  ret = databuff_printf(buf, buf_len, pos,
                        "    block: %p, offset: %03d, in_use: %d, is_large: %d, is_washed: %d, nblocks: %03d," \
                        " alloc_bytes: %lu, aobject_size: %d, obj_set: %p\n",
                        chunk->blk_data(block), chunk->blk_offset(block), block->in_use_, block->is_large_,
                        block->is_washed_, chunk->blk_nblocks(block),
                        block->alloc_bytes_, AOBJECT_CELL_BYTES, block->obj_set_);
  if (OB_SUCC(ret)) {
    if (pos > buf_len / 2) {
      ::write(fd, buf, pos);
      pos = 0;
    }
  }
  return ret;
}

int print_object_meta(AChunk *chunk, ABlock *block, AObject *object, char *buf,
                      int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int offset = ((char*)object - chunk->blk_data(block))/AOBJECT_CELL_BYTES;
  void *label = &object->label_[0];
  void *end = memchr(label, '\0', sizeof(object->label_));
  int len = end ? (char*)end - (char*)label : sizeof(object->label_);
  ret = databuff_printf(buf, buf_len, pos,
                        "        object: %p, offset: %04d, in_use: %d, is_large: %d, nobjs: %04d," \
                        " label: \'%.*s\', alloc_bytes: %u\n",
                        object, offset, object->in_use_, object->is_large_, object->nobjs_,
                        len, (char*)label, object->alloc_bytes_);
  return ret;
}

int label_stat(AChunk *chunk, ABlock *block, AObject *object,
               LabelMap &lmap, LabelItem *items, int64_t item_cap,
               int64_t &item_used)
{
  int ret = OB_SUCCESS;
  if (object->in_use_) {
    int64_t hold = 0;
    if (!object->is_large_) {
      hold = object->nobjs_ * AOBJECT_CELL_BYTES;
    } else if (!block->is_large_) {
      hold = chunk->blk_nblocks(block) * ABLOCK_SIZE;
    } else {
      hold = align_up2(chunk->alloc_bytes_ + ACHUNK_HEADER_SIZE, get_page_size());
    }
    LabelItem *litem = nullptr;
    auto key = std::make_pair(*(uint64_t*)object->label_, *((uint64_t*)object->label_ + 1));
    LabelInfoItem *linfoitem = lmap.get(key);
    if (NULL != linfoitem) {
      // exist
      litem = linfoitem->litem_;
      litem->hold_ += hold;
      litem->used_ += object->alloc_bytes_;
      litem->count_++;
      if (chunk != linfoitem->chunk_) {
        litem->chunk_cnt_ += 1;
        linfoitem->chunk_ = chunk;
      }
      if (block != linfoitem->block_) {
        litem->block_cnt_ += 1;
        linfoitem->block_ = block;
      }
    } else {
      if (item_used >= item_cap) {
        ret = OB_SIZE_OVERFLOW;
        LOG_WARN("label cnt too large", K(ret), K(item_cap), K(item_used));
      } else {
        litem = &items[item_used++];
        STRNCPY(litem->str_, object->label_, sizeof(litem->str_));
        litem->str_[sizeof(litem->str_) - 1] = '\0';
        litem->str_len_ = strlen(litem->str_);
        litem->hold_ = hold;
        litem->used_ = object->alloc_bytes_;
        litem->count_ = 1;
        litem->block_cnt_ = 1;
        litem->chunk_cnt_ = 1;
        ObSignalHandlerGuard guard(ob_signal_handler);
        ret = lmap.set_refactored(key, LabelInfoItem(litem, chunk, block));
      }
    }
  }
  return ret;
}

int malloc_sample_stat(uint64_t ctx_id,
                       AObject *object, ObMallocSampleMap &malloc_sample_map)
{
  int ret = OB_SUCCESS;
  if (object->in_use_ && object->on_malloc_sample_) {
    ObMallocSampleKey key;
    
    key.ctx_id_ = ctx_id;
    MEMCPY((char*)key.bt_, object->bt(), AOBJECT_BACKTRACE_SIZE);
    STRNCPY(key.label_, object->label_, sizeof(key.label_));
    key.label_[sizeof(key.label_) - 1] = '\0';
    ObMallocSampleValue *item = malloc_sample_map.get(key);
    if (NULL != item) {
      item->alloc_count_ += 1;
      item->alloc_bytes_ += object->alloc_bytes_;
    } else {
      ObMallocSampleValue value(1, object->alloc_bytes_);
      ObSignalHandlerGuard guard(ob_signal_handler);
      ret = malloc_sample_map.set_refactored(key, value);
    }
  }
  return ret;
}

void ObMemoryDump::handle(void *task)
{
  int ret = OB_SUCCESS;
  bool segv_cnt_over = false;
  ObMemoryDumpTask *m_task = static_cast<ObMemoryDumpTask*>(task);
  LOG_INFO("handle dump task", "task", *m_task);

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (STAT_LABEL == m_task->type_) {
    int tenant_cnt = 1;
    w_stat_->tcr_cnt_ = 0;
    w_stat_->malloc_sample_map_.clear();
    int64_t item_used = 0;
    int64_t log_pos = 0;
    IGNORE_RETURN databuff_printf(print_buf_, PRINT_BUF_LEN, log_pos,
                                  "\ntenant_cnt: %d, max_chunk_cnt: %d\n" \
                                  "%-15s%-15s%-15s%-15s\n",
                                  tenant_cnt, MAX_CHUNK_CNT,
                                  "ctx_id", "chunk_cnt", "label_cnt",
                                  "segv_cnt");
    const int64_t start_ts = ObTimeUtility::current_time();
    ObMallocAllocator *ma = ObMallocAllocator::get_instance();
    for (int tdx = 0; tdx < tenant_cnt; tdx++) {
      
      for (int ctx_id = 0; ctx_id < ObCtxIds::MAX_CTX_ID; ctx_id++) {
        ObTenantCtxAllocatorGuard ta = ma->get_tenant_ctx_allocator(ctx_id);
        if (nullptr == ta) {
          ta = ma->get_tenant_ctx_allocator_unrecycled(ctx_id);
        }
        if (nullptr == ta) {
          continue;
        }
        int segv_cnt = 0;
        const int64_t orig_item_used = item_used;
        int chunk_cnt = 0;
        ret = OB_SUCCESS;
        ta->get_chunks(chunks_, MAX_CHUNK_CNT, chunk_cnt);
        auto &w_stat = w_stat_;
        auto &lmap = lmap_;
        lmap.clear();
        DumpSignalGuard guard;
        for (int i = 0; OB_SUCC(ret) && i < chunk_cnt; i++) {
          AChunk *chunk = chunks_[i];
          auto func = [&, chunk] {
              int ret = parse_chunk_meta(chunk,
                  [] (AChunk *chunk) {
                    UNUSEDx(chunk);
                    return OB_SUCCESS;
                  },
                  [] (AChunk *chunk, ABlock *block) {
                    UNUSEDx(chunk, block);
                    return OB_SUCCESS;
                  },
                  [ctx_id, &lmap, w_stat, &item_used]
                  (AChunk *chunk, ABlock *block, AObject *object) {
                    int ret = OB_SUCCESS;
                    if (object->in_use_) {
                     if (OB_FAIL(label_stat(chunk, block, object, lmap, w_stat->up2date_items_,
                                           ARRAYSIZEOF(w_stat->up2date_items_), item_used))) {
                        // do-nothing
                      } else if (OB_FAIL(malloc_sample_stat(ctx_id, object,
                                                            w_stat->malloc_sample_map_))) {
                        // do-nothing
                      }
                    }
                    return ret;
                  });
              if (OB_FAIL(ret)) {
              }
              return ret;
          };
          bool has_segv = false;
          guard.do_with_segv_catch(func, has_segv, ret);
          if (has_segv) {
            LOG_INFO("restore from sigsegv, let's goon~");
            segv_cnt++;
            continue;
          }
        } // iter chunk end
        if (OB_SUCC(ret)) {
          auto &tcr = w_stat_->tcrs_[w_stat_->tcr_cnt_++];
          
          tcr.ctx_id_ = ctx_id;
          tcr.start_ = orig_item_used;
          tcr.end_ = item_used;
        }
        if (segv_cnt > 128) {
          LOG_WARN("too many sigsegv, maybe there is a low-level bug", K(segv_cnt));
          segv_cnt_over = true;
        }
        if (OB_SUCC(ret) && (chunk_cnt != 0 || segv_cnt != 0)) {
          IGNORE_RETURN databuff_printf(print_buf_, PRINT_BUF_LEN, log_pos,
                                        "%-15d%-15d%-15ld%-15d\n",
                                        ctx_id, chunk_cnt,
                                        item_used - orig_item_used, segv_cnt);
        }
      } // iter ctx end
    } // iter tenant end
    if (segv_cnt_over) {
      ++huge_segv_cnt_;
    } else {
      huge_segv_cnt_ = 0;
    }
    if (huge_segv_cnt_ > 8) {
      LOG_ERROR("too many sigsegv has happened many times continuously, maybe there is a low-level bug");
    }
    if (OB_SUCC(ret)) {
      IGNORE_RETURN databuff_printf(print_buf_, PRINT_BUF_LEN, log_pos, "cost_time: %ld",
                                    ObTimeUtility::current_time() - start_ts);
    }
    if (log_pos > 0) {
      _OB_LOG(INFO, "statistics: %.*s", static_cast<int32_t>(log_pos), print_buf_);
    }
    // switch stat as long as one tenant-ctx is generated, ignore the error code.
    if (w_stat_->tcr_cnt_ > 0) {
      ObLatchWGuard guard(iter_lock_, common::ObLatchIds::MEM_DUMP_ITER_LOCK);
      std::swap(r_stat_, w_stat_);
    }

    for (int tdx = 0; tdx < tenant_cnt; tdx++) {
      
      ma->print_tenant_memory_usage();
      ma->print_tenant_ctx_memory_usage();
    }

    print_malloc_sample_info();

    // print global chunk freelist
    {
      static const int64_t CHUNK_BUF_LEN = 4LL << 10;
      char chunk_buf[CHUNK_BUF_LEN] = "";
      int64_t chunk_pos = CHUNK_MGR.to_string(chunk_buf, CHUNK_BUF_LEN);
      _OB_LOG(INFO, "%.*s", static_cast<int>(chunk_pos), chunk_buf);
    }
  } else {
    int fd = -1;
    if (-1 == (fd = ::open(LOG_FILE,
                           O_CREAT | O_WRONLY | O_APPEND
#ifdef _WIN32
                           | _O_BINARY
#endif
                           , S_IRUSR | S_IWUSR))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("create new file failed", KCSTRING(strerror(errno)));
    }
    if (OB_SUCC(ret)) {
      int64_t print_pos = 0;
      struct timeval tv;
      gettimeofday(&tv, NULL);
      struct tm tm;
#ifndef _WIN32
      ::localtime_r((const time_t *) &tv.tv_sec, &tm);
#else
      {
        time_t sec = tv.tv_sec;
        if (sec < 0) sec = 0;
        errno_t err = ::localtime_s(&tm, &sec);
        if (err != 0) {
          memset(&tm, 0, sizeof(tm));
          tm.tm_year = 70;
          tm.tm_mday = 1;
        }
      }
#endif
      ret = databuff_printf(print_buf_, PRINT_BUF_LEN, print_pos,
          "\n###################%04d-%02d-%02d %02d:%02d:%02d.%06ld###################\n",
          tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
          tm.tm_sec, tv.tv_usec);
      print_pos += m_task->to_string(print_buf_ + print_pos, PRINT_BUF_LEN - print_pos);
      ret = databuff_printf(print_buf_, PRINT_BUF_LEN, print_pos, "\n");
      // chunk
      int cnt = 0;
      if (m_task->dump_all_) {
        int tenant_cnt = 1;
        for (int tdx = 0; tdx < tenant_cnt; tdx++) {
          
          for (int ctx_id = 0; ctx_id < ObCtxIds::MAX_CTX_ID; ctx_id++) {
            auto ta =
              ObMallocAllocator::get_instance()->get_tenant_ctx_allocator(ctx_id);
            if (nullptr == ta) {
              ta = ObMallocAllocator::get_instance()->get_tenant_ctx_allocator_unrecycled(ctx_id);
            }
            if (nullptr != ta) {
              ta->get_chunks(chunks_, MAX_CHUNK_CNT, cnt);
            }
          }
        }
      } else if (m_task->dump_tenant_ctx_) {
        auto ta = ObMallocAllocator::get_instance()->get_tenant_ctx_allocator(m_task->ctx_id_);
        if (nullptr == ta) {
          ta = ObMallocAllocator::get_instance()->get_tenant_ctx_allocator_unrecycled(m_task->ctx_id_);
        }
        if (nullptr != ta) {
          ta->get_chunks(chunks_, MAX_CHUNK_CNT, cnt);
        }
      } else {
        AChunk *chunk = find_chunk(m_task->p_chunk_);
        if (chunk != nullptr) {
          chunks_[cnt++] = chunk;
        }
      }
      LOG_INFO("chunk cnt", K(cnt));
      // sort chunk
      lib::ob_sort(chunks_, chunks_ + cnt);
      // iter chunk
      DumpSignalGuard guard;
      for (int i = 0; OB_SUCC(ret) && i < cnt; i++) {
        AChunk *chunk = chunks_[i];
        char *print_buf = print_buf_; // for lambda capture
        auto func = [&, chunk] {
            int ret = parse_chunk_meta(chunk,
                [print_buf, &print_pos] (AChunk *chunk) {
                  return print_chunk_meta(chunk, print_buf, PRINT_BUF_LEN, print_pos);
                },
                [print_buf, &print_pos, fd] (AChunk *chunk, ABlock *block) {
                  UNUSEDx(chunk);
                  return print_block_meta(chunk, block, print_buf, PRINT_BUF_LEN, print_pos, fd);
                },
                [print_buf, &print_pos] (AChunk *chunk, ABlock *block, AObject *object) {
                  UNUSEDx(chunk, block);
                  return print_object_meta(chunk, block, object, print_buf, PRINT_BUF_LEN, print_pos);
                });
            if (OB_FAIL(ret)) {
            }
            return OB_SUCCESS;
        };
        bool has_segv = false;
        guard.do_with_segv_catch(func, has_segv, ret);
        if (has_segv) {
          LOG_INFO("restore from sigsegv, let's goon~");
          continue;
        }
      } // iter chunk end
      if (OB_SUCC(ret) && print_pos > 0) {
        ::write(fd, print_buf_, print_pos);
      }
    }
    if (fd > 0) {
      ::close(fd);
    }
  }
}

} // namespace common
} // namespace oceanbase
