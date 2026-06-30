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


#include "block_set.h"
#include "lib/alloc/ob_tenant_ctx_allocator.h"
#include "lib/time/ob_time_utility.h"
#ifdef _WIN32
#include <windows.h>
#ifndef MADV_DONTNEED
#define MADV_DONTNEED 4
#endif
#endif

// macOS sys/param.h defines isset macro which conflicts with method calls
#ifdef isset
#undef isset
#endif

using namespace oceanbase;
using namespace oceanbase::lib;

namespace
{
static const int64_t ORDINARY_PURGE_BUDGET = 4L << 20;
static const int64_t ORDINARY_PURGE_MIN_INTERVAL_US = 1000L * 1000L;
static const int64_t ORDINARY_PURGE_DELAY_US = 1000L * 1000L;
static const int64_t ORDINARY_PURGE_MAX_BLOCKS = 64;
}

BlockSet::BlockSet()
    : tallocator_(NULL),
      locker_(NULL),
      chunk_mgr_(NULL),
      clist_(NULL),
      avail_bm_(BLOCKS_PER_CHUNK+1, avail_bm_buf_),
      purged_avail_bm_(BLOCKS_PER_CHUNK+1, purged_avail_bm_buf_),
      total_hold_(0), total_payload_(0), total_used_(0),
      last_ordinary_purge_ts_(0)
{
}

BlockSet::~BlockSet()
{
  reset();
}

bool BlockSet::check_has_unfree()
{
  return clist_ != NULL;
}

void BlockSet::reset()
{
  while (NULL != clist_) {
    free_chunk(clist_);
  }
  //MEMSET(block_list_, 0, sizeof(block_list_));
  clist_ = nullptr;
  avail_bm_.clear();
  purged_avail_bm_.clear();
  last_ordinary_purge_ts_ = 0;
}

void BlockSet::set_tenant_ctx_allocator(ObTenantCtxAllocator &allocator)
{
  if (&allocator != tallocator_) {
    reset();
    tallocator_ = &allocator;
    attr_ = ObMemAttr(nullptr, allocator.get_ctx_id());
  }
}

ABlock *BlockSet::alloc_block(const uint64_t size, const ObMemAttr &attr)
{
  const uint64_t alloc_size = size;
  const uint64_t all_size   = alloc_size;
  const uint32_t cls        = (uint32_t)(1 + (all_size - 1) / ABLOCK_SIZE);
  ABlock *block             = NULL;

  if (size >= UINT32_MAX) {
    // not support
    auto &afc = g_alloc_failed_ctx();
    afc.reason_ = SINGLE_ALLOC_SIZE_OVERFLOW;
    afc.alloc_size_ = size;
  } else if (0 == size) {
    // size is zero
  } else if (cls <= BLOCKS_PER_CHUNK) {
    // can fit in a chunk
    block = get_free_block(cls, attr);
  } else {
    AChunk *chunk = alloc_chunk(all_size, attr);
    if (chunk) {
      block = new (chunk->data_) ABlock();
      block->in_use_ = true;
      block->is_large_ = true;
      chunk->mark_blk_offset_bit(chunk->blk_offset(block));
    }
  }

  if (OB_NOT_NULL(block)) {
    AChunk *chunk = block->chunk();
    chunk->using_cnt_++;
    block->alloc_bytes_ = size;
    uint64_t payload = 0;
    block->hold(&payload);
    UNUSED(ATOMIC_FAA(&total_used_, payload));
    if (!block->is_large_) {
      maybe_ordinary_purge();
    }
  }

  return block;
}

void BlockSet::free_block(ABlock *const block)
{
  if (NULL == block) {
    // nothing
  } else {
    abort_unless(block->is_valid());
    uint64_t payload = 0;
    block->hold(&payload);
    UNUSED(ATOMIC_FAA(&total_used_, -payload));
    AChunk *chunk = block->chunk();
    abort_unless(chunk->is_valid());
    chunk->using_cnt_--;
    if (!!block->is_large_) {
      free_chunk(chunk);
    } else {
      ABlock *prev_block = NULL;
      ABlock *next_block = NULL;

      int offset = chunk->blk_offset(block);
      int prev_offset = -1;
      if (!chunk->is_first_blk_offset(offset, &prev_offset)) {
        prev_block = chunk->offset2blk(prev_offset);
        if (!prev_block->in_use_ && !prev_block->is_washed_) {
          take_off_free_block(prev_block, offset - prev_offset, chunk);
          block->clear_magic_code();
          chunk->unmark_blk_offset_bit(offset);
        }
      }

      int next_offset = -1;
      if (!chunk->is_last_blk_offset(offset, &next_offset)) {
        next_block = chunk->offset2blk(next_offset);
        if (!next_block->in_use_ && !next_block->is_washed_) {
          take_off_free_block(next_block, chunk->blk_nblocks(next_block), chunk);
          next_block->clear_magic_code();
          chunk->unmark_blk_offset_bit(next_offset);
        }
      }

      ABlock *head = NULL != prev_block && !prev_block->in_use_ && !prev_block->is_washed_ ? prev_block : block;

      // head won't been NULL,
      if (head != NULL) {
        head->in_use_ = false;
        // copy a temp
        if (0 == chunk->using_cnt_) {
          int offset = 0;
          do {
            ABlock *unused_block = chunk->offset2blk(offset);
            int next_offset = -1;
            bool is_last = chunk->is_last_blk_offset(offset, &next_offset);
            abort_unless(!unused_block->in_use_);
            // don't allow to take off head twice
            if (unused_block->is_washed_) {
              take_off_purged_block(unused_block, chunk->blk_nblocks(unused_block), chunk);
            } else if (head != unused_block) {
              take_off_free_block(unused_block, chunk->blk_nblocks(unused_block), chunk);
            }
            if (is_last) break;
            offset = next_offset;
          } while (true);
          free_chunk(chunk);
        } else {
          int head_nblocks = chunk->blk_nblocks(head);
          add_free_block(head, head_nblocks, chunk);
          maybe_ordinary_purge();
        }
      }
    }
  }
}

void BlockSet::add_free_block(ABlock *block, int nblocks, AChunk *chunk)
{
  abort_unless(NULL != block && !block->in_use_ && !block->is_washed_);
  int offset = chunk->blk_offset(block);
  chunk->mark_blk_offset_bit(offset);
  block->alloc_bytes_ = common::ObTimeUtility::fast_current_time();

#if MEMCHK_LEVEL >= 1
  int expect_nblocks = chunk->blk_nblocks(block);
  abort_unless(nblocks == expect_nblocks);
#endif
  ABlock *&blist = block_list_[nblocks];
  if (avail_bm_.isset(nblocks)) {
    block->prev_ = blist->prev_;
    block->next_ = blist;
    block->prev_->next_ = block;
    block->next_->prev_ = block;
  } else {
    block->prev_ = block->next_ = block;
    blist = block;
    avail_bm_.set(nblocks);
  }
}

ABlock* BlockSet::get_free_block(const int cls, const ObMemAttr &attr)
{
  ABlock *block = NULL;

  const int ffs = avail_bm_.find_first_significant(cls);
  if (ffs >= 0) {
    if (NULL != block_list_[ffs]) {  // exist
      block = block_list_[ffs];
      take_off_free_block(block, ffs, block->chunk());
      block->in_use_ = true;
    }
  }

  // put back into another block list if need be.
  if (NULL != block && ffs > cls) {
    AChunk *chunk = block->chunk();
    ABlock *next_block = new (block + cls) ABlock();
    add_free_block(next_block, ffs - cls, chunk);
  }

  if (block == NULL && ffs < 0) {
    block = get_purged_block(cls, attr);
  }

  if (block == NULL && ffs < 0) {
    if (add_chunk(attr)) {
      block = get_free_block(cls, attr);
    }
  }

  return block;
}

void BlockSet::take_off_free_block(ABlock *block, int nblocks, AChunk *chunk)
{
  abort_unless(NULL != block && !block->in_use_ && !block->is_washed_);

#if MEMCHK_LEVEL >= 1
  int expect_nblocks = chunk->blk_nblocks(block);
  abort_unless(nblocks == expect_nblocks);
#endif
  if (block->next_ != block) {
    block->next_->prev_ = block->prev_;
    block->prev_->next_ = block->next_;
    if (block == block_list_[nblocks]) {
      block_list_[nblocks] = block->next_;
    }
  } else {
    avail_bm_.unset(nblocks);
    block_list_[nblocks] = NULL;
  }
}

void BlockSet::add_purged_block(ABlock *block, int nblocks, AChunk *chunk)
{
  abort_unless(NULL != block && !block->in_use_ && block->is_washed_);
  int offset = chunk->blk_offset(block);
  chunk->mark_blk_offset_bit(offset);

#if MEMCHK_LEVEL >= 1
  int expect_nblocks = chunk->blk_nblocks(block);
  abort_unless(nblocks == expect_nblocks);
#endif
  ABlock *&blist = purged_block_list_[nblocks];
  if (purged_avail_bm_.isset(nblocks)) {
    block->prev_ = blist->prev_;
    block->next_ = blist;
    block->prev_->next_ = block;
    block->next_->prev_ = block;
  } else {
    block->prev_ = block->next_ = block;
    blist = block;
    purged_avail_bm_.set(nblocks);
  }
}

ABlock* BlockSet::get_purged_block(const int cls, const ObMemAttr &attr)
{
  ABlock *block = NULL;
  const int ffs = purged_avail_bm_.find_first_significant(cls);
  if (ffs >= 0 && OB_NOT_NULL(purged_block_list_[ffs]) && OB_NOT_NULL(tallocator_)) {
    const int64_t restore_size = cls * ABLOCK_SIZE;
    if (tallocator_->restore_purged_hold(restore_size, attr)) {
      block = purged_block_list_[ffs];
      AChunk *chunk = block->chunk();
      take_off_purged_block(block, ffs, chunk);

      UNUSED(ATOMIC_FAA(&total_hold_, restore_size));
      UNUSED(ATOMIC_FAA(&total_payload_, restore_size));
      abort_unless(chunk->washed_size_ >= static_cast<uint64_t>(restore_size));
      chunk->washed_size_ -= restore_size;

      int64_t related_chunks = 0;
      int64_t washed_blks = 0;
      if (ffs == cls) {
        abort_unless(chunk->washed_blks_ > 0);
        chunk->washed_blks_--;
        washed_blks = -1;
        if (0 == chunk->washed_blks_) {
          related_chunks = -1;
        }
      } else {
        ABlock *next_block = new (block + cls) ABlock();
        next_block->is_washed_ = true;
        add_purged_block(next_block, ffs - cls, chunk);
      }
      tallocator_->update_wash_stat(related_chunks, washed_blks, -restore_size);
      block->is_washed_ = false;
      block->in_use_ = true;
    }
  }
  return block;
}

void BlockSet::take_off_purged_block(ABlock *block, int nblocks, AChunk *chunk)
{
  abort_unless(NULL != block && !block->in_use_ && block->is_washed_);

#if MEMCHK_LEVEL >= 1
  int expect_nblocks = chunk->blk_nblocks(block);
  abort_unless(nblocks == expect_nblocks);
#endif
  if (block->next_ != block) {
    block->next_->prev_ = block->prev_;
    block->prev_->next_ = block->next_;
    if (block == purged_block_list_[nblocks]) {
      purged_block_list_[nblocks] = block->next_;
    }
  } else {
    purged_avail_bm_.unset(nblocks);
    purged_block_list_[nblocks] = NULL;
  }
}

AChunk *BlockSet::alloc_chunk(const uint64_t size, const ObMemAttr &attr)
{
  AChunk *chunk = NULL;
  if (OB_NOT_NULL(tallocator_)) {
    const uint64_t all_size = AChunkMgr::aligned(size);
    chunk = chunk_mgr_->alloc_chunk(static_cast<int64_t>(size), attr);
    if (chunk != nullptr) {
      uint64_t payload = 0;
      UNUSED(ATOMIC_FAA(&total_hold_, chunk->hold(&payload)));
      UNUSED(ATOMIC_FAA(&total_payload_, payload));
    }
    if (NULL != chunk) {
      if (NULL != clist_) {
        chunk->prev_ = clist_->prev_;
        chunk->next_ = clist_;
        clist_->prev_->next_ = chunk;
        clist_->prev_ = chunk;
      } else {
        chunk->prev_ = chunk->next_ = chunk;
        clist_ = chunk;
      }
      chunk->block_set_ = this;
    }
  }
  return chunk;
}

bool BlockSet::add_chunk(const ObMemAttr &attr)
{
  AChunk *chunk = alloc_chunk(ACHUNK_SIZE, attr);
  if (NULL != chunk) {
    ABlock *block = new (chunk->data_) ABlock();
    add_free_block(block, BLOCKS_PER_CHUNK, chunk);
  }
  return NULL != chunk;
}

void BlockSet::free_chunk(AChunk *const chunk)
{
  abort_unless(NULL != chunk);
  abort_unless(chunk->is_valid());
  abort_unless(NULL != chunk->next_);
  abort_unless(NULL != chunk->prev_);
  abort_unless(NULL != clist_);
  abort_unless(0 == chunk->using_cnt_);
  if (chunk == clist_) {
    clist_ = clist_->next_;
  }

  if (chunk == clist_) {
    clist_ = NULL;
  } else {
    chunk->next_->prev_ = chunk->prev_;
    chunk->prev_->next_ = chunk->next_;
  }
  uint64_t payload = 0;
  const uint64_t hold = chunk->hold(&payload);
  if (OB_NOT_NULL(tallocator_)) {
    UNUSED(ATOMIC_FAA(&total_hold_, -hold));
    UNUSED(ATOMIC_FAA(&total_payload_, -payload));
    if (chunk->washed_size_ != 0) {
      tallocator_->update_wash_stat(-1, -chunk->washed_blks_, -chunk->washed_size_);
    }
    chunk_mgr_->free_chunk(chunk, attr_);
  }
}

#ifdef _WIN32
inline int ob_madvise(void* addr, size_t length, int advice) {
  // Use MEM_RESET instead of MEM_DECOMMIT: MEM_DECOMMIT truly decommits pages,
  // causing ACCESS_VIOLATION on subsequent access. MEM_RESET keeps pages committed
  // but tells the OS their contents are no longer needed — matching Linux
  // madvise(MADV_DONTNEED) semantics where pages remain accessible.
  if (advice == MADV_DONTNEED) {
      return ::VirtualAlloc(addr, length, MEM_RESET, PAGE_READWRITE) != NULL ? 0 : -1;
  }
  return 0;
}
#endif

int64_t BlockSet::sync_wash(int64_t wash_size)
{
  bool has_ignore = false;
  return purge_free_blocks(wash_size, 0, INT64_MAX, &has_ignore);
}

int64_t BlockSet::purge_free_blocks(const int64_t wash_size,
    const int64_t delay_us,
    const int64_t max_blocks_per_round,
    bool *has_ignore,
    int64_t *scanned_blocks)
{
#if !defined(MADV_DONTNEED)
  UNUSED(wash_size);
  UNUSED(delay_us);
  UNUSED(max_blocks_per_round);
  UNUSED(has_ignore);
  if (OB_NOT_NULL(scanned_blocks)) {
    *scanned_blocks = 0;
  }
  return 0;
#else
  const ssize_t ps = get_page_size();
  bool local_has_ignore = false;
  int64_t washed_size = 0;
  int64_t washed_blks = 0;
  int64_t scanned_blks = 0;
  int64_t related_chunks = 0;
  const int64_t now = delay_us > 0 ? common::ObTimeUtility::fast_current_time() : 0;
  int cls = avail_bm_.nbits() - 1;
  while (washed_size < wash_size && scanned_blks < max_blocks_per_round &&
	       cls >= 1 && (cls = avail_bm_.find_first_most_significant(cls)) != -1) {
    const int64_t len = cls * ABLOCK_SIZE;
    if (len < ps) {
      break;
    } else if (washed_size + len > wash_size) {
      cls--;
      continue;
    }
    ABlock *head = block_list_[cls];
    if (nullptr == head) {
    } else {
      ABlock *block = head;
      bool need_scan = true;
      int64_t scan_cnt = 0;
      while (need_scan && OB_NOT_NULL(block) &&
             washed_size < wash_size && scanned_blks < max_blocks_per_round &&
             scan_cnt++ < BLOCKS_PER_CHUNK) {
        ABlock *next = block->next_ != block ? block->next_ : nullptr;
        need_scan = OB_NOT_NULL(next) && next != head;
        scanned_blks++;
        AChunk *chunk = block->chunk();
        if (chunk->is_hugetlb_) {
          _OB_LOG(DEBUG, "cannot be applied to Huge TLB pages");
          local_has_ignore = true;
        } else {
        #if MEMCHK_LEVEL >= 1
          abort_unless(!block->in_use_ && !block->is_washed_);
          int nblocks = chunk->blk_nblocks(block);
          abort_unless(nblocks == cls);
        #endif
          char *data = chunk->blk_data(block);
          if ((reinterpret_cast<uint64_t>(data) & (ps - 1)) != 0 ||
              (len & (ps - 1)) != 0) {
            _OB_LOG(DEBUG, "cannot be applied to non-multiple of page-size, page_size: %zd", ps);
            local_has_ignore = true;
          } else if (delay_us > 0 && now - static_cast<int64_t>(block->alloc_bytes_) < delay_us) {
          } else {
            int result = 0;
            do {
#ifndef _WIN32
              result = ::madvise(data, len, MADV_DONTNEED);
#else
              result = ob_madvise(data, len, MADV_DONTNEED);
#endif
            } while (result == -1 && errno == EAGAIN);
            if (-1 == result) {
              _OB_LOG_RET(WARN, OB_ERR_SYS, "madvise failed, errno: %d", errno);
              local_has_ignore = true;
            } else {
              if (head == block) {
                head = next;
              }
              take_off_free_block(block, cls, chunk);
              block->is_washed_ = true;
              add_purged_block(block, cls, chunk);
              if (0 == chunk->washed_blks_) {
                abort_unless(0 == chunk->washed_size_);
                related_chunks++;
              }
              chunk->washed_size_ += len;
              chunk->washed_blks_++;
              washed_blks += 1;
              washed_size += len;
            }
          }
        }
        block = next;
        if (OB_ISNULL(head)) {
          need_scan = false;
        }
      }
    }
    cls--;
  }
#if MEMCHK_LEVEL >= 1
  if (wash_size == INT64_MAX && !local_has_ignore) {
    abort_unless(-1 == avail_bm_.find_first_significant(1));
  }
#endif
  if (washed_size > 0) {
    UNUSED(ATOMIC_FAA(&total_hold_, -washed_size));
    UNUSED(ATOMIC_FAA(&total_payload_, -washed_size));
    tallocator_->dec_hold(washed_size);
    tallocator_->update_wash_stat(related_chunks, washed_blks, washed_size);
  }
  if (OB_NOT_NULL(has_ignore)) {
    *has_ignore = local_has_ignore;
  }
  if (OB_NOT_NULL(scanned_blocks)) {
    *scanned_blocks = scanned_blks;
  }
#if MEMCHK_LEVEL >= 1
  if (0 == washed_size && ABLOCK_SIZE & (ps - 1)) {
    abort_unless(total_payload_ == total_used_);
  }
#endif
  return washed_size;
#endif
}

void BlockSet::maybe_ordinary_purge()
{
#if defined(MADV_DONTNEED)
  const int64_t now = common::ObTimeUtility::fast_current_time();
  const int64_t last_ts = ATOMIC_LOAD(&last_ordinary_purge_ts_);
  if (now - last_ts >= ORDINARY_PURGE_MIN_INTERVAL_US) {
    ATOMIC_STORE(&last_ordinary_purge_ts_, now);
    (void)purge_free_blocks(ORDINARY_PURGE_BUDGET,
        ORDINARY_PURGE_DELAY_US,
        ORDINARY_PURGE_MAX_BLOCKS,
        nullptr);
  }
#endif
}
