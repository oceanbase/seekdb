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

#ifndef _OB_CTX_ALLOCATOR_H_
#define _OB_CTX_ALLOCATOR_H_

#include "lib/alloc/ob_iallocator.h"
#include "lib/alloc/object_mgr.h"
#include "lib/alloc/alloc_failed_reason.h"
#include "lib/time/ob_time_utility.h"
#include "lib/resource/ob_resource_mgr.h"
#include "lib/alloc/alloc_func.h"
#include <signal.h>
namespace oceanbase
{

namespace common
{
struct LabelItem;
}
namespace lib
{
extern bool malloc_sample_allowed(const int64_t size, const ObMemAttr &attr);
class ObCtxAllocator;

class ObCtxAllocatorState
{
public:
  friend class ObCtxAllocator;
  friend class ObMallocAllocator;
  using VisitFunc = std::function<int(ObLabel &label, common::LabelItem *l_item)>;
  using InvokeFunc = std::function<int (const ObMemoryMgr*)>;
  ObCtxAllocatorState(uint64_t ctx_id,
      ObCtxAllocator *allocator);
  ~ObCtxAllocatorState();
  uint64_t get_ctx_id()
  {
    return ctx_id_;
  }
  void print_usage() const;
  void update_wash_stat(int64_t related_chunks, int64_t blocks, int64_t size);
  void set_req_chunkmgr_parallel(int32_t parallel);
  void get_chunks(AChunk **chunks, int cap, int &cnt);
  AChunk *alloc_chunk(const int64_t size, const ObMemAttr &attr)
  {
    AChunk *chunk = NULL;
    if (!resource_handle_.is_valid()) {
      LIB_LOG_RET(ERROR, OB_INVALID_ERROR, "resource_handle is invalid", K_(ctx_id));
    } else {
      chunk = resource_handle_.get_memory_mgr()->alloc_chunk(size, attr);
    }
    return chunk;
  }
  void free_chunk(AChunk *chunk, const ObMemAttr &attr)
  {
    if (!resource_handle_.is_valid()) {
      LIB_LOG_RET(ERROR, OB_INVALID_ERROR, "resource_handle is invalid", K_(ctx_id));
    } else {
      resource_handle_.get_memory_mgr()->free_chunk(chunk, attr);
    }
  }
  void dec_hold(const int64_t size);
  // statistic related
  int set_memory_mgr()
  {
    int ret = common::OB_SUCCESS;
    if (resource_handle_.is_valid()) {
      ret = common::OB_INIT_TWICE;
      LIB_LOG(WARN, "resource_handle is already valid", K(ret), K_(ctx_id));
    } else if (OB_FAIL(ObResourceMgr::get_instance().get_handle(
        resource_handle_))) {
      LIB_LOG(ERROR, "get_resource_mgr failed", K(ret));
    }
    return ret;
  }

  int set_hard_limit(int64_t bytes)
  {
    int ret = common::OB_SUCCESS;
    if (!resource_handle_.is_valid()) {
      ret = common::OB_ERR_UNEXPECTED;
      LIB_LOG(ERROR, "resource_handle is invalid", K(ret), K_(ctx_id));
    } else if (OB_FAIL(resource_handle_.get_memory_mgr()->set_ctx_hard_limit(ctx_id_, bytes))) {
      LIB_LOG(WARN, "memory manager set_ctx_limit failed", K(ret), K(ctx_id_), K(bytes));
    }
    return ret;
  }

  int set_limit(int64_t bytes)
  {
    int ret = common::OB_SUCCESS;
    if (!resource_handle_.is_valid()) {
      ret = common::OB_ERR_UNEXPECTED;
      LIB_LOG(ERROR, "resource_handle is invalid", K(ret), K_(ctx_id));
    } else if (OB_FAIL(resource_handle_.get_memory_mgr()->set_ctx_limit(ctx_id_, bytes))) {
      LIB_LOG(WARN, "memory manager set_ctx_limit failed", K(ret), K(ctx_id_), K(bytes));
    }
    return ret;
  }

  int64_t get_limit() const
  {
    int64_t limit = 0;
    uint64_t ctx_id = ctx_id_;
    with_resource_handle_invoke([&ctx_id, &limit](const ObMemoryMgr *mgr) {
      mgr->get_ctx_limit(ctx_id, limit);
      return common::OB_SUCCESS;
    });
    return limit;
  }

  int64_t get_hold() const
  {
    int64_t hold = 0;
    uint64_t ctx_id = ctx_id_;
    with_resource_handle_invoke([&ctx_id, &hold](const ObMemoryMgr *mgr) {
      mgr->get_ctx_hold(ctx_id, hold);
      return common::OB_SUCCESS;
    });
    return hold;
  }

  int64_t get_used() const;

  int64_t get_total_limit() const
  {
    int64_t limit = 0;
    with_resource_handle_invoke([&limit](const ObMemoryMgr *mgr) {
      limit = mgr->get_limit();
      return common::OB_SUCCESS;
    });
    return limit;
  }

  int64_t get_total_hold() const
  {
    int64_t hold = 0;
    with_resource_handle_invoke([&hold](const ObMemoryMgr *mgr) {
      hold = mgr->get_sum_hold();
      return common::OB_SUCCESS;
    });
    return hold;
  }

  common::ObLabelItem get_label_usage(ObLabel &label) const;
private:
  int iter_label(VisitFunc func) const;
  int with_resource_handle_invoke(InvokeFunc func) const
  {
    int ret = common::OB_SUCCESS;
    if (!resource_handle_.is_valid()) {
      ret = common::OB_ERR_UNEXPECTED;
      LIB_LOG(ERROR, "resource_handle is invalid");
    } else {
      ret = func(resource_handle_.get_memory_mgr());
    }
    return ret;
  }
  ObCtxAllocator *get_allocator()
  {
    return allocator_;
  }
  ObResourceMgrHandle &get_resource_handle() { return resource_handle_; }
private:
  ObResourceMgrHandle resource_handle_;
  uint64_t ctx_id_;
  ObCtxAllocator *allocator_;
  int64_t wash_related_chunks_;
  int64_t washed_blocks_;
  int64_t washed_size_;
} __attribute__((__aligned__(16)));

class ObCtxAllocator
    : public common::ObIAllocator
{
friend class ObMallocAllocator;
friend class ObCtxAllocatorState;

class ChunkMgr : public IChunkMgr
{
public:
  explicit ChunkMgr(ObCtxAllocator &allocator) : allocator_(allocator) {}
  AChunk *alloc_chunk(const uint64_t size, const ObMemAttr &attr) override
  {
    AChunk *chunk = allocator_.alloc_chunk(size, attr);
    if (OB_ISNULL(chunk)) {
      allocator_.req_chunk_mgr_.reclaim_chunks();
      chunk = allocator_.alloc_chunk(size, attr);
    }
    return chunk;
  }
  void free_chunk(AChunk *chunk, const ObMemAttr &attr) override
  {
    allocator_.free_chunk(chunk, attr);
  }
private:
  ObCtxAllocator &allocator_;
};

class ReqChunkMgr : public IChunkMgr
{
public:
  static constexpr int32_t MAX_PARALLEL = 64;
  explicit ReqChunkMgr(ObCtxAllocator &allocator)
    : allocator_(allocator), parallel_(CTX_ATTR(allocator_.get_ctx_id()).parallel_)
  {
    abort_unless(parallel_ <= ARRAYSIZEOF(chunks_));
    MEMSET(chunks_, 0, sizeof(chunks_));
  }
  AChunk *alloc_chunk(const uint64_t size, const ObMemAttr &attr) override
  {
    AChunk *chunk = NULL;
    if (INTACT_ACHUNK_SIZE == AChunk::calc_hold(size)) {
      const uint64_t idx = common::get_itid() % parallel_;
      chunk = ATOMIC_TAS(&chunks_[idx], NULL);
    }
    if (OB_ISNULL(chunk)) {
      chunk = allocator_.alloc_chunk(size, attr);
    }
    return chunk;
  }
  void free_chunk(AChunk *chunk, const ObMemAttr &attr) override
  {
    bool freed = false;
    if (INTACT_ACHUNK_SIZE == chunk->hold()) {
      const uint64_t idx = common::get_itid() % parallel_;
      freed = ATOMIC_BCAS(&chunks_[idx], NULL, chunk);
    }
    if (!freed) {
      allocator_.free_chunk(chunk, attr);
    }
  }
  void reclaim_chunks()
  {
    for (int i = 0; i < MAX_PARALLEL; i++) {
      AChunk *chunk = ATOMIC_TAS(&chunks_[i], NULL);
      if (chunk != NULL) {
        allocator_.free_chunk(chunk,
                              ObMemAttr("unused", allocator_.get_ctx_id()));
      }
    }
  }
  int64_t n_chunks() const
  {
    int64_t n = 0;
    for (int i = 0; i < MAX_PARALLEL; i++) {
      AChunk *chunk = ATOMIC_LOAD(&chunks_[i]);
      if (chunk != NULL) {
        n++;
      }
    }
    return n;
  }
  void set_parallel(int32_t parallel)
  {
    int32_t min_parallel = CTX_ATTR(allocator_.get_ctx_id()).parallel_;
    if (parallel < min_parallel) {
      parallel_ = min_parallel;
    } else if (parallel > MAX_PARALLEL) {
      parallel_ = MAX_PARALLEL;
    } else {
      parallel_ = parallel;
    }
  }
private:
  ObCtxAllocator &allocator_;
  int32_t parallel_;
  AChunk *chunks_[MAX_PARALLEL];
};

class AChunkUsingList
{
public:
  static const uint64_t NWAY = 64;
  uint64_t get_index(AChunk *chunk)
  {
    return (((uint64_t)chunk>>21) * 0xdeece66d + 0xb) % NWAY;
  }
  void insert(AChunk *chunk)
  {
    uint64_t index = get_index(chunk);
    lib::ObMutexGuard guard(slots_[index].mutex_);
    AChunk &head = slots_[index].head_;
    chunk->prev2_ = &head;
    chunk->next2_ = head.next2_;
    head.next2_->prev2_ = chunk;
    head.next2_ = chunk;
  }
  void remove(AChunk *chunk)
  {
    uint64_t index = get_index(chunk);
    lib::ObMutexGuard guard(slots_[index].mutex_);
    chunk->prev2_->next2_ = chunk->next2_;
    chunk->next2_->prev2_ = chunk->prev2_;
  }
  void get_chunks(AChunk **chunks, int cap, int &cnt)
  {
    for (int i = 0; i < NWAY; ++i) {
      lib::ObMutexGuard guard(slots_[i].mutex_);
      AChunk &head = slots_[i].head_;
      AChunk *cur = head.next2_;
      while (cur != &head && cnt < cap) {
        chunks[cnt++] = cur;
        cur = cur->next2_;
      }
    }
  }
private:
  struct Slot {
    Slot()
      : mutex_(common::ObLatchIds::CHUNK_USING_LIST_LOCK),
        head_()
    {
      mutex_.enable_record_stat(false);
      head_.prev2_ = &head_;
      head_.next2_ = &head_;
    }
    ObMutex mutex_;
    AChunk head_;
  } slots_[NWAY];
};

public:
  explicit ObCtxAllocator(ObCtxAllocatorState &ctx_allocator, uint64_t ctx_id)
    : ctx_allocator_(ctx_allocator), ctx_id_(ctx_id),
      obj_mgr_(*this,
               CTX_ATTR(ctx_id).enable_no_log_,
               INTACT_NORMAL_AOBJECT_SIZE,
               CTX_ATTR(ctx_id).parallel_,
               CTX_ATTR(ctx_id).enable_dirty_list_,
               NULL),
      idle_size_(0), head_chunk_(), chunk_cnt_(0),
      chunk_freelist_mutex_(common::ObLatchIds::CHUNK_FREE_LIST_LOCK),
      chunk_mgr_(*this), req_chunk_mgr_(*this)
  {
    MEMSET(&head_chunk_, 0, sizeof(AChunk));
    ObMemAttr attr;

    attr.ctx_id_ = ctx_id;
    chunk_freelist_mutex_.enable_record_stat(false);
  }
  virtual ~ObCtxAllocator()
  {}
  uint64_t get_ctx_id()
  {
    return ctx_id_;
  }
private:
  // will delete it
  virtual void *alloc(const int64_t size)
  {
    return alloc(size, ObMemAttr());
  }

  virtual void *alloc(const int64_t size, const ObMemAttr &attr);
  virtual void* realloc(const void *ptr, const int64_t size, const ObMemAttr &attr);
  virtual void free(void *ptr);
public:
  static int64_t get_obj_hold(void *ptr);

  // statistic related
  int set_hard_limit(int64_t bytes) { return ctx_allocator_.set_hard_limit(bytes); }
  int set_limit(int64_t bytes) { return ctx_allocator_.set_limit(bytes); }

  int64_t get_limit() const { return ctx_allocator_.get_limit(); }

  int64_t get_hold() const { return ctx_allocator_.get_hold(); }

  int64_t get_used() const { return ctx_allocator_.get_used(); }

  int64_t get_total_limit() const { return ctx_allocator_.get_total_limit(); }

  int64_t get_total_hold() const { return ctx_allocator_.get_total_hold(); }
  common::ObLabelItem get_label_usage(ObLabel &label) const { return ctx_allocator_.get_label_usage(label); }

  void print_memory_usage() const { ctx_allocator_.print_usage(); }
  AChunk *alloc_chunk(const int64_t size, const ObMemAttr &attr);
  void free_chunk(AChunk *chunk, const ObMemAttr &attr);
  void dec_hold(const int64_t size);
  int set_idle(const int64_t size, const bool reserve = false);
  IBlockMgr &get_block_mgr() { return obj_mgr_; }
  IChunkMgr &get_chunk_mgr() { return chunk_mgr_; }
  IChunkMgr &get_req_chunk_mgr() { return req_chunk_mgr_; }
  void get_chunks(AChunk **chunks, int cap, int &cnt) { ctx_allocator_.get_chunks(chunks, cap, cnt); }
  using VisitFunc = std::function<int(ObLabel &label,
                                      common::LabelItem *l_item)>;
  int iter_label(VisitFunc func) const { return ctx_allocator_.iter_label(func); }
  bool check_has_unfree(char *first_label, char *first_bt)
  {
    bool has_unfree = obj_mgr_.check_has_unfree();
    if (has_unfree) {
      bool tmp_has_unfree = obj_mgr_.check_has_unfree(first_label, first_bt);
    }
    return has_unfree;
  }
  void update_wash_stat(int64_t related_chunks, int64_t blocks, int64_t size)
  {
    ctx_allocator_.update_wash_stat(related_chunks, blocks, size);
  }
  void reset_req_chunk_mgr() { req_chunk_mgr_.reclaim_chunks(); }
  void set_req_chunkmgr_parallel(int32_t parallel) { ctx_allocator_.set_req_chunkmgr_parallel(parallel); }
private:
  void get_chunks_(AChunk **chunks, int cap, int &cnt) { using_list_.get_chunks(chunks, cap, cnt); }
  void set_req_chunkmgr_parallel_(int32_t parallel) { req_chunk_mgr_.set_parallel(parallel); }
  AChunk *pop_chunk();
  void push_chunk(AChunk *chunk);
public:
  template <typename T>
  static void* common_realloc(const void *ptr, const int64_t size, const ObMemAttr &attr,
      ObCtxAllocator& ctx_allocator, T &allocator)
  {
    ObDisableDiagnoseGuard disable_diagnose_guard;
    if (!attr.label_.is_valid()) {
      LIB_LOG_RET(ERROR, OB_INVALID_ARGUMENT, "OB_MOD_DO_NOT_USE_ME REALLOC", K(size));
    }
    void *nptr = NULL;
    if (errsim_alloc(attr)) {
      // do-nothing
    } else {
      AObject *obj = NULL; // original object
      AObject *nobj = NULL; // newly allocated object
      ObMemAttr inner_attr = attr;
      if (NULL != ptr) {
        obj = reinterpret_cast<AObject*>((char*)ptr - AOBJECT_HEADER_SIZE);
        abort_unless(obj->is_valid());
        abort_unless(obj->in_use_);
        ABlock *block = obj->block();
        abort_unless(block->is_valid());
        abort_unless(block->in_use_);
        on_free(*obj, *block);
      }
      const bool light_backtrace_allowed = is_memleak_light_backtrace_enabled() && ObLightBacktraceGuard::is_enabled() && ObCtxIds::GLIBC != attr.ctx_id_;
      bool sample_allowed = light_backtrace_allowed || malloc_sample_allowed(size, inner_attr);
      inner_attr.alloc_extra_info_ = sample_allowed;
      nobj = allocator.realloc_object(obj, size, inner_attr);
      if (OB_ISNULL(nobj)) {
        int64_t total_size = 0;
        if (g_alloc_failed_ctx().need_wash_chunk()) {
          total_size += CHUNK_MGR.sync_wash();
        }
        if (total_size > 0) {
          nobj = allocator.realloc_object(obj, size, inner_attr);
        }
      }
      if (OB_NOT_NULL(nobj)) {
        on_alloc(*nobj, inner_attr, light_backtrace_allowed);
        nptr = nobj->data_;
      }
    }
    if (NULL == nptr) {
      print_alloc_failed_msg(ctx_allocator.get_ctx_id(),
                             ctx_allocator.get_hold(), ctx_allocator.get_limit(),
                             ctx_allocator.get_total_hold(), ctx_allocator.get_total_limit());
    }
    return nptr;
  }
  static void common_free(void *ptr);
private:
  static void on_alloc(AObject& obj, const ObMemAttr& attr, const bool light_backtrace_allowed);
  static void on_free(AObject& obj, ABlock& block);

private:
  ObCtxAllocatorState &ctx_allocator_;
  uint64_t ctx_id_;
  ObjectMgr obj_mgr_;
  int64_t idle_size_;
  AChunk head_chunk_;
  // Temporarily useless, leave debug
  int64_t chunk_cnt_;
  ObMutex chunk_freelist_mutex_;
  AChunkUsingList using_list_;
  ChunkMgr chunk_mgr_;
  ReqChunkMgr req_chunk_mgr_;
}; // end of class ObCtxAllocator

} // end of namespace lib
} // end of namespace oceanbase

#endif /* _OB_CTX_ALLOCATOR_H_ */
