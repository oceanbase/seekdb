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

#ifndef OCEANBASE_BASIC_OB_CHUNK_ROW_STORE_CPP_
#define OCEANBASE_BASIC_OB_CHUNK_ROW_STORE_CPP_

#define USING_LOG_PREFIX SQL_ENG

#include "rust/config.h"
#include "sql/engine/basic/ob_chunk_row_store.h"


namespace oceanbase
{
using namespace common;

namespace sql
{
int ObChunkRowStore::BlockBuffer::init(char *buf, const int64_t buf_size)
{
  int ret = OB_SUCCESS;
  if (NULL == buf || buf_size <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    data_ = buf;
    cur_pos_ = 0;
    cap_ = buf_size - sizeof(BlockBuffer);
  }
  return ret;
}

namespace chunk_row_store {

template <typename T, typename B>
void pointer2off(T *&pointer, B *base)
{
  pointer = reinterpret_cast<T *>(
      reinterpret_cast<const char *>(pointer) - reinterpret_cast<const char *>(base));
}

template <typename T, typename B>
void off2pointer(T *&pointer, B *base)
{
  pointer = reinterpret_cast<T *>(
      reinterpret_cast<intptr_t>(pointer) + reinterpret_cast<char *>(base));
}

template <typename T, typename B>
void point2pointer(T *&dst_pointer, B *dst_base, T *src_pointer, const B *src_base)
{
  dst_pointer = reinterpret_cast<T *>(reinterpret_cast<char *>(dst_base) +
      reinterpret_cast<intptr_t>(src_pointer) - reinterpret_cast<const char *>(src_base));
}

}

int ObChunkRowStore::StoredRow::copy_row(const ObNewRow &r, char *buf,
    const int64_t size, const int64_t row_size, const uint32_t row_extend_size)
{
  int ret = OB_SUCCESS;
  const int64_t extra = NativeValueFlat::span(row_extend_size);
  const int64_t count = r.get_count();
  if (payload_ != buf || size < 0 || count < 0 || count > UINT32_MAX
      || row_size < sizeof(*this) || row_size > UINT32_MAX
      || extra > UINT32_MAX || count > size / sizeof(DatumValue)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    int64_t pos = sizeof(DatumValue) * count + extra;
    if (pos > size) { ret = OB_BUF_NOT_ENOUGH; }
    else {
      cnt_ = static_cast<uint32_t>(count);
      row_size_ = static_cast<uint32_t>(row_size);
      extra_size_ = static_cast<uint32_t>(extra);
      for (int64_t i = 0; OB_SUCC(ret) && i < cnt_; ++i) {
        new (&cells()[i]) DatumValue();
        ret = DatumValueBuffer::deep_copy(r.get_cell(i), cells()[i], buf, size, pos);
      }
    }
  }
  return ret;
}

int ObChunkRowStore::StoredRow::assign(const StoredRow *sr)
{
  if (sr == nullptr || sr->row_size_ < sizeof(*sr)) { return OB_INVALID_ARGUMENT; }
  MEMMOVE(this, sr, sr->row_size_);
  return swizzling();
}

int ObChunkRowStore::StoredRow::unswizzling(char *base)
{
  int ret = OB_SUCCESS;
  UNUSED(base);
  const int64_t length = int64_t(row_size_) - sizeof(*this);
  const int64_t start = int64_t(cnt_) * sizeof(DatumValue) + extra_size_;
  if (length < 0 || start > length) { return OB_INVALID_DATA; }
  // Both passes walk this copied flat payload. No stale source address is
  // followed, including the address bytes inside nested SQL arrays.
  for (int pass = 0; OB_SUCC(ret) && pass < 2; ++pass) {
    int64_t pos = start;
    for (int64_t i = 0; OB_SUCC(ret) && i < cnt_; ++i) {
      ret = DatumValueBuffer::make_offsets(cells()[i], payload_, length, pos,
          reinterpret_cast<char *>(this), pass == 1);
    }
  }
  return ret;
}

int ObChunkRowStore::StoredRow::swizzling()
{
  int ret = OB_SUCCESS;
  const int64_t length = int64_t(row_size_) - sizeof(*this);
  const int64_t start = int64_t(cnt_) * sizeof(DatumValue) + extra_size_;
  if (length < 0 || start > length) { return OB_INVALID_DATA; }
  for (int pass = 0; OB_SUCC(ret) && pass < 2; ++pass) {
    int64_t pos = start;
    for (int64_t i = 0; OB_SUCC(ret) && i < cnt_; ++i) {
      ret = DatumValueBuffer::relocate(cells()[i], payload_, length, pos, pass == 1);
    }
  }
  return ret;
}

int ObChunkRowStore::Block::add_row(const ObNewRow &row, const int64_t row_size,
    uint32_t row_extend_size, StoredRow **stored_row)
{
  int ret = OB_SUCCESS;
  BlockBuffer *buf = get_buffer();
  if (!buf->is_inited()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (row_size > buf->remain()) {
    ret = OB_BUF_NOT_ENOUGH;
    LOG_WARN("buffer not enough", K(row_size), "remain", buf->remain());
  } else {
    StoredRow *sr = new (buf->head())StoredRow;
    if (OB_FAIL(sr->copy_row(
        row, buf->head() + ROW_HEAD_SIZE, row_size - ROW_HEAD_SIZE, row_size, row_extend_size))) {
    } else {
      if (OB_FAIL(buf->advance(row_size))) {
      } else {
        rows_++;
        if (NULL != stored_row) {
          *stored_row = sr;
        }
      }
    }
    //LOG_DEBUG("RowStore blk add_row", K_(sr->cnt), K_(rows), K(row_size), K(buf->data_size()));
  }

  return ret;
}


int ObChunkRowStore::Block::copy_row(const StoredRow *stored_row)
{
  int ret = OB_SUCCESS;
  BlockBuffer *buf = get_buffer();
  int64_t row_size =  stored_row->row_size_;
  if (!buf->is_inited()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    StoredRow *sr = new (buf->head())StoredRow;
    if (OB_FAIL(sr->assign(stored_row))) {
    } else if (OB_FAIL(buf->advance(row_size))) {
    } else {
      rows_++;
    }
  }

  return ret;
}

int ObChunkRowStore::Block::get_store_row(int64_t &cur_pos, const StoredRow *&sr)
{
  int ret = OB_SUCCESS;
  const int64_t payload_size = int64_t(blk_size_) - sizeof(Block);
  if (cur_pos < 0 || cur_pos > payload_size || payload_size - cur_pos < sizeof(StoredRow)) {
    ret = OB_INDEX_OUT_OF_RANGE;
  } else {
    const auto *row = reinterpret_cast<const StoredRow *>(payload_ + cur_pos);
    const int64_t cells_size = int64_t(row->cnt_) * sizeof(DatumValue);
    if (row->row_size_ < sizeof(*row) || row->row_size_ > payload_size - cur_pos
        || cells_size > row->row_size_ - sizeof(*row)
        || row->extra_size_ > row->row_size_ - sizeof(*row) - cells_size) {
      ret = OB_INVALID_DATA;
    } else {
      cur_pos += row->row_size_;
      sr = row;
    }
  }
  return ret;
}

int ObChunkRowStore::Block::unswizzling()
{
  int ret = OB_SUCCESS;
  int64_t cur_pos = 0;
  for (uint32_t i = 0; OB_SUCC(ret) && i < rows_; ++i) {
    const StoredRow *row = nullptr;
    if (OB_FAIL(get_store_row(cur_pos, row))) {}
    else { ret = const_cast<StoredRow *>(row)->unswizzling(); }
  }
  return ret;
}

int ObChunkRowStore::Block::swizzling(int64_t *col_cnt)
{
  int ret = OB_SUCCESS;
  int64_t cur_pos = 0;
  const StoredRow *row = nullptr;
  for (uint32_t i = 0; OB_SUCC(ret) && i < rows_; ++i) {
    if (OB_FAIL(get_store_row(cur_pos, row))) {}
    else { ret = const_cast<StoredRow *>(row)->swizzling(); }
  }
  if (OB_SUCC(ret) && row != nullptr && col_cnt != nullptr) { *col_cnt = row->cnt_; }
  return ret;
}

ObChunkRowStore::ObChunkRowStore(common::ObIAllocator *alloc /* = NULL */)
  : inited_(false), label_(nullptr), ctx_id_(0), mem_limit_(0),
    cur_blk_(NULL), max_blk_size_(0), min_blk_size_(INT64_MAX), default_block_size_(BLOCK_SIZE),
    n_blocks_(0), col_count_(0),
    row_cnt_(0), enable_dump_(true), has_dumped_(false), dumped_row_cnt_(0),
    file_size_(0), n_block_in_file_(0),
    mem_hold_(0), mem_used_(0), max_hold_mem_(0),
    allocator_(NULL == alloc ? &inner_allocator_ : alloc),
    row_store_mode_(STORE_MODE::WITHOUT_PROJECTOR), projector_(NULL), projector_size_(0),
    row_extend_size_(0), alloc_size_(0), free_size_(0), callback_(nullptr)
{
  io_.fd_ = -1;
  io_.dir_id_ = -1;
}

int ObChunkRowStore::init(int64_t mem_limit,
    int64_t mem_ctx_id /* = common::ObCtxIds::DEFAULT_CTX_ID */,
    const char *label /* = common::ObNewModIds::OB_SQL_CHUNK_ROW_STORE) */,
    bool enable_dump /* = true */,
    STORE_MODE row_store_mode /* WITHOUT_PROJECTOR */,
    uint32_t row_extend_size /* = 0 */
)
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
  } else {
    enable_dump_ = enable_dump;
    
    ctx_id_ = mem_ctx_id;
    label_ = label;
    if (0 == config::_chunk_row_store_mem_limit()) {
      mem_limit_ = mem_limit;
    } else {
      mem_limit_ = config::_chunk_row_store_mem_limit();
    }
    inited_ = true;
    default_block_size_ = BLOCK_SIZE;
    max_blk_size_ = default_block_size_;
    min_blk_size_ = INT64_MAX;
    io_.fd_ = -1;
    row_store_mode_ = row_store_mode;
    row_extend_size_ = row_extend_size;
  }
  return ret;
}

void ObChunkRowStore::reset()
{
  int ret = OB_SUCCESS;
  label_ = common::ObModIds::OB_SQL_ROW_STORE;
  ctx_id_ = common::ObCtxIds::DEFAULT_CTX_ID;

  mem_limit_ = 0;
  row_cnt_ = 0;

  if (is_file_open()) {
    if (OB_FAIL(data_plane::tmp_file_remove(io_.fd_))) {
    } else {
    }
    io_.fd_ = -1;
  }
  
  n_block_in_file_ = 0;

  while (!blocks_.is_empty()) {
    Block *item = blocks_.remove_first();
    mem_hold_ -= item->get_buffer()->mem_size();
    mem_used_ -= item->get_buffer()->mem_size();
    callback_free(item->get_buffer()->mem_size());
    if (NULL != item) {
      allocator_->free(item);
    }
  }
  blocks_.reset();
  cur_blk_ = NULL;
  while (!free_list_.is_empty()) {
    Block *item = free_list_.remove_first();
    mem_hold_ -= item->get_buffer()->mem_size();
    callback_free(item->get_buffer()->mem_size());
    if (NULL != item) {
      allocator_->free(item);
    }
  }
  free_list_.reset();

  max_blk_size_ = 0;
  n_blocks_ = 0;
  row_cnt_ = 0;

  if(OB_SUCC(ret)) {
    if (nullptr != callback_) {
      callback_->dumped(-file_size_);
    }
  }
  file_size_ = 0;

  if (NULL != projector_) {
    allocator_->free(projector_);
  }
  projector_ = NULL;
  projector_size_ = 0;
  if (alloc_size_ != free_size_) {
    LOG_ERROR("It may has memory no free", K(alloc_size_), K(free_size_), K(lbt()));
  }
  alloc_size_ = 0;
  free_size_ = 0;
  callback_ = nullptr;

  inited_ = false;
}



void *ObChunkRowStore::alloc_blk_mem(const int64_t size, const bool for_iterator)
{
  void *blk = NULL;
  int ret = OB_SUCCESS;
  if (size < 0) {
  } else {
    ObMemAttr attr(label_, ctx_id_);
    void *mem = allocator_->alloc(size, attr);
    if (NULL == mem) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      blk = static_cast<char *>(mem);
      if (!for_iterator) {
        mem_hold_ += size;
        max_hold_mem_ = max(max_hold_mem_, mem_hold_);
      }
      callback_alloc(size);
    }
  }
  return blk;
}

void ObChunkRowStore::free_blk_mem(void *mem, const int64_t size /* = 0 */)
{
  if (NULL != mem) {
    allocator_->free(mem);
    mem_hold_ -= size;
    callback_free(size);
  }
}

void ObChunkRowStore::free_block(Block *item)
{
  if (NULL != item) {
    BlockBuffer* buffer = item->get_buffer();
    if (!buffer->is_empty()) {
      mem_used_ -= buffer->mem_size();
    }
    mem_hold_ -= buffer->mem_size();
    callback_free(buffer->mem_size());
    allocator_->free(item);
  }
}


//free mem of specified size
bool ObChunkRowStore::shrink_block(int64_t size)
{
  int64_t freed_size = 0;
  bool succ = false;
  int ret = OB_SUCCESS;
  if ((0 == blocks_.get_size() && 0 == free_list_.get_size()) || 0 == size) {
    LOG_DEBUG("RowStore no need to shrink", K(size), K(blocks_.get_size()));
  } else {
    Block* item = free_list_.remove_first();
    //free those blocks haven't been used yet
    while (NULL != item) {
      freed_size += item->get_buffer()->mem_size();
      free_block(item);
      item = free_list_.remove_first();
    }

    item = blocks_.remove_first();
    while (OB_SUCC(ret) && NULL != item) {
      if (OB_FAIL(dump_one_block(item->get_buffer()))) {
      }
      dumped_row_cnt_ += item->rows();
      // Regardless of success or failure, memory needs to be released
      freed_size += item->get_buffer()->mem_size();
      LOG_DEBUG("RowStore shrink dump and free", K(size), K(freed_size),
          K(item->get_buffer()->mem_size()), K(item));
      free_block(item);
      item = blocks_.remove_first();
    }
  }
  if (freed_size >= size) {
    succ = true;
  }
  return succ;
}

int ObChunkRowStore::init_block_buffer(void* mem, const int64_t size, Block *&block)
{
  int ret = OB_SUCCESS;
  block = new(mem)Block;
  BlockBuffer* blkbuf = new(static_cast<char *>(mem) + size - sizeof(BlockBuffer))BlockBuffer;
  if (OB_FAIL(blkbuf->init(static_cast<char *>(mem), size))) {
  } else {
    if (OB_FAIL(blkbuf->advance(sizeof(Block)))) {
    } else {
      block->set_block_size(static_cast<uint32_t>(blkbuf->capacity()));
      block->next_ = NULL;
      block->rows_ = 0;
    }
  }
  return ret;
}

int ObChunkRowStore::alloc_block_buffer(Block *&block, const int64_t data_size,
  const int64_t min_size, const bool for_iterator)
{
  int ret = OB_SUCCESS;
  int64_t size = std::max(min_size, data_size);
  size = next_pow2(size);
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    void *mem = alloc_blk_mem(size, for_iterator);
    if (OB_ISNULL(mem)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(init_block_buffer(mem, size, block))){
      free_blk_mem(mem, size);
    } else if (!for_iterator) {
      if (size > max_blk_size_) {
        max_blk_size_ = size;
      } else if (size < min_blk_size_) {
        min_blk_size_ = size;
      }
    }
  }
  return ret;
}

int ObChunkRowStore::alloc_block_buffer(Block *&block, const int64_t data_size,
    const bool for_iterator)
{
  int64_t min_size = std::max(static_cast<int64_t>(BLOCK_SIZE), default_block_size_);
  return alloc_block_buffer(block, data_size, min_size, for_iterator);
}

inline int ObChunkRowStore::dump_one_block(BlockBuffer *item)
{
  int ret = OB_SUCCESS;
  if (item->cur_pos_ <= 0) {
    LOG_WARN("unexpected: dump zero", K(item), K(item->cur_pos_));
  }
  item->block->magic_ = Block::MAGIC;
  if (OB_FAIL(item->get_block()->unswizzling())) {
  } else if (OB_FAIL(write_file(item->data(), item->capacity()))) {
  } else {
    n_block_in_file_++;
    LOG_DEBUG("RowStore Dumpped block", K_(item->block->rows), K_(item->cur_pos), K(item->capacity()));
  }
  return ret;
}

// only clean memory data
// Add a mode, dump only the blocks that are full, leaving the last block
int ObChunkRowStore::dump(bool reuse, bool all_dump)
{
  int ret = OB_SUCCESS;
  BlockBuffer* buf = NULL;
  if (!enable_dump_) {
    ret = OB_EXCEED_MEM_LIMIT;
  } else {
    int64_t n_block = blocks_.get_size();
    const int64_t org_n_block = n_block;
    Block* cur = nullptr;
    while (OB_SUCC(ret) && 0 < n_block && (all_dump || 1 < n_block)) {
      cur = blocks_.remove_first();
      --n_block;
      if (OB_ISNULL(cur)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        buf = cur->get_buffer();
        if (!buf->is_empty() && OB_FAIL(dump_one_block(buf))) {
        }
        // Regardless of success or failure, memory needs to be released
        if (!buf->is_empty()) {
          mem_used_ -= buf->mem_size();
          dumped_row_cnt_ += cur->rows();
        }
        if (reuse && buf->mem_size() <= default_block_size_) {
          buf->reuse();
          free_list_.add_last(cur);
        } else {
          mem_hold_ -= buf->mem_size();
          callback_free(buf->mem_size());
          allocator_->free(cur);
        }
      }
    }
    if (all_dump && (mem_used_ != 0 || (!reuse && mem_hold_ != 0) || blocks_.get_size() != 0)) {
    }
    if (OB_SUCC(ret)) {
      if (all_dump) {
        cur_blk_ = NULL;
      }
      if (!has_dumped_) {
        has_dumped_ = (org_n_block != blocks_.get_size());
      }
    }
  }
  return ret;
}

bool ObChunkRowStore::find_block_can_hold(const int64_t size, bool &need_shrink)
{
  bool found = false;
  need_shrink = false;
  if (NULL != cur_blk_ && size <= cur_blk_->get_buffer()->remain()) {
    found = true;
  } else if (free_list_.get_size() > 0 && default_block_size_ >= size) {
    Block* next = free_list_.remove_first();
    LOG_DEBUG("reuse block", K(next), K(*next), K(next->get_buffer()), K(*next->get_buffer()));
    found = true;
    use_block(next);
    blocks_.add_last(next);
    n_blocks_++;
  } else if (mem_limit_ > 0 && mem_hold_ > mem_used_ && mem_hold_ + size > mem_limit_) {
    need_shrink = true;
  }
  return found;
}

int ObChunkRowStore::switch_block(const int64_t min_size)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (min_size <= 0 || OB_ISNULL(cur_blk_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (need_dump(min_size) && OB_FAIL(dump(true, true))) {
    if (OB_EXCEED_MEM_LIMIT != ret) {
    }
  } else {
    Block *new_block = NULL;
    bool need_shrink = false;
    bool can_find = find_block_can_hold(min_size, need_shrink);
    if (need_shrink) {
      if (shrink_block(min_size)) {
      }
    }
    if (!can_find) { // need alloc new block
      if (OB_FAIL(alloc_block_buffer(new_block, min_size, false))) {
      }
      if (!can_find && OB_SUCC(ret)){
        blocks_.add_last(new_block);
        n_blocks_++;
        use_block(new_block);
        LOG_DEBUG("RowStore switch block", K(new_block), K(*new_block), K(new_block->get_buffer()),
            K(n_blocks_), K(min_size));
      }
    }
  }
  return ret;
}

int ObChunkRowStore::add_row(const common::ObNewRow &row, StoredRow **stored_row)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    int64_t bak_projector_size_ = 0;
    int32_t *bak_projector_ = NULL;
    ObNewRow &r = const_cast<ObNewRow &>(row);
    //store project
    if (STORE_MODE::FULL == row_store_mode_ && NULL != row.projector_ && row.projector_size_ > 0) {
      if (OB_ISNULL(projector_)) {
        ObMemAttr attr(label_, ctx_id_);
        projector_size_ = row.projector_size_;
        const int64_t size = projector_size_ * sizeof(*projector_);
        projector_ = static_cast<int32_t *>(allocator_->alloc(size));
        if (OB_ISNULL(projector_)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          MEMCPY(projector_, row.projector_, size);
        }
      } else if (projector_size_ != row.projector_size_) {
        ret = OB_ERR_UNEXPECTED;
      }
      if (OB_SUCC(ret)) {
        bak_projector_ = r.projector_;
        bak_projector_size_ = r.projector_size_;
        r.projector_size_ = 0;
        r.projector_ = NULL;
      }
    }
    const int64_t row_size = Block::row_store_size(row, row_extend_size_);
    int64_t min_buf_size = 0;
    if (OB_SUCC(ret)) {
      const int64_t overhead = BlockBuffer::HEAD_SIZE + sizeof(BlockBuffer);
      if (row_size == INT64_MAX || row_size > INT64_MAX - overhead) {
        ret = OB_SIZE_OVERFLOW;
      } else { min_buf_size = Block::min_buf_size(row_size); }
    }
    if (OB_SUCC(ret)) {
      if (NULL == cur_blk_) {
        Block *new_blk = nullptr;
        if (OB_FAIL(alloc_block_buffer(new_blk, min_buf_size, false))) {
        } else {
          use_block(new_blk);
          blocks_.add_last(cur_blk_);
          n_blocks_++;
        }
      }
      if (OB_SUCC(ret) && row_size > cur_blk_->get_buffer()->remain()) {
        if (OB_FAIL(switch_block(min_buf_size)) && OB_EXCEED_MEM_LIMIT != ret) {
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(cur_blk_->add_row(row, row_size, row_extend_size_, stored_row))) {
        } else {
          row_cnt_++;
          if (0 >= col_count_) {
            col_count_ = row.get_count();
          }
        }
      }
    }
    // always keep row unchanged.
    if (NULL != bak_projector_) {
      r.projector_size_ = bak_projector_size_;
      r.projector_ = bak_projector_;
    }
   // if (stored_row != NULL) {
   //   stored_type_ = (*stored_row)->TYPE;
  //  }
  }

  return ret;
}

int ObChunkRowStore::copy_row(const StoredRow *stored_row, ObChunkRowStore* crs)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    const int64_t row_size = stored_row->row_size_;
    const int64_t min_buf_size = Block::min_buf_size(row_size);
    if (OB_NOT_NULL(crs) && OB_NOT_NULL(crs->projector_) && nullptr == projector_) {
      projector_size_ = crs->projector_size_;
      const int64_t size = projector_size_ * sizeof(*projector_);
      projector_ = static_cast<int32_t *>(allocator_->alloc(size));
      if (OB_ISNULL(projector_)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        MEMCPY(projector_, crs->projector_, size);
      }
    } else if (OB_NOT_NULL(crs) && projector_size_ != crs->projector_size_) {
      ret = OB_ERR_UNEXPECTED;
    }
    if (OB_SUCC(ret)) {
      if (NULL == cur_blk_) {
        Block *new_blk = nullptr;
        if (OB_FAIL(alloc_block_buffer(new_blk, min_buf_size, false))) {
        } else {
          use_block(new_blk);
          blocks_.add_last(cur_blk_);
          n_blocks_++;
        }
      }
      if (OB_SUCC(ret) && row_size > cur_blk_->get_buffer()->remain()) {
        if (OB_FAIL(switch_block(min_buf_size)) && OB_EXCEED_MEM_LIMIT != ret) {
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(cur_blk_->copy_row(stored_row))) {
        } else {
          row_cnt_++;
          if (col_count_ <= 0) {
            col_count_ = stored_row->cnt_;
          }
        }
      }
    }
  }

  return ret;
}

int ObChunkRowStore::finish_add_row(bool need_dump)
{
  int ret = OB_SUCCESS;
  int64_t timeout_ms = 0;
  if (free_list_.get_size() > 0) {
    Block *block = free_list_.remove_first();
    while (NULL != block) {
      mem_hold_ -= block->get_buffer()->mem_size();
      callback_free(block->get_buffer()->mem_size());
      allocator_->free(block);
      block = free_list_.remove_first();
    }
    free_list_.reset();
  }
  if (is_file_open()) {
    if (need_dump && OB_FAIL(dump(false, true)) && OB_EXCEED_MEM_LIMIT != ret) {
    } else if (OB_FAIL(get_timeout(timeout_ms))) {
    }
  } else {
  }

  return ret;
}

//add block manually, this block must be initialized by init_block_buffer()
//and must be removed by remove_added_block before ObChunkRowStore<>::reset()
int ObChunkRowStore::add_block(Block* block, bool need_swizzling, bool *added)
{
  int ret = OB_SUCCESS;
  int64_t col_cnt = 0;
  bool need_add = true;

  if (need_swizzling) {
    //for iterate
    if (block->rows_ <= 0) {
      need_add = false;
    } else if (OB_FAIL(block->swizzling(&col_cnt))) {
    } else if (this->col_count_ != 0 && this->col_count_ != col_cnt) {
      LOG_WARN("add block failed col cnt not match", K(block), K(need_swizzling),
          K(col_cnt), K_(col_count));
    }
  }

  if (need_add) {
    this->row_cnt_ += block->rows_;
    this->n_blocks_++;
    if (nullptr != added) {
      *added = true;
    }
    blocks_.add_last(block);
    if (NULL == cur_blk_) {
      //cur_blk_ point to the first added block
      cur_blk_ = block;
    }
  }
  return ret;
}


void ObChunkRowStore::remove_added_blocks()
{
  blocks_.reset();
  n_blocks_ = 0;
  row_cnt_ = 0;
}

int ObChunkRowStore::load_next_chunk_blocks(ChunkIterator &it)
{
  int ret = OB_SUCCESS;
  int64_t read_off = 0;
  if (NULL == it.chunk_mem_) {
    if (it.chunk_read_size_ > file_size_) {
      it.chunk_read_size_ = file_size_;
    }
    it.chunk_mem_ = static_cast<char*>(alloc_blk_mem(sizeof(char) * it.chunk_read_size_, true));
    if (OB_ISNULL(it.chunk_mem_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
  } else {
    /* when the last block haven't been read fully,
       copy the head half to the start of chunk-mem and read next chunk after it */
    char* last_blk_end = static_cast<char*>(static_cast<void*>(it.cur_iter_blk_)) + it.cur_iter_blk_->blk_size_;
    char* chunk_end = it.chunk_mem_ + it.chunk_read_size_;
    if (chunk_end > last_blk_end) {
      MEMCPY(it.chunk_mem_, last_blk_end, chunk_end - last_blk_end);
      read_off += chunk_end - last_blk_end;
    }
  }

  if (OB_SUCC(ret)) {
    int64_t read_n_blocks = 0;
    int64_t read_size = it.chunk_read_size_ - read_off;
    int64_t chunk_size = it.chunk_read_size_;
    int64_t tmp_read_size = 0;
    it.chunk_n_rows_ = 0;
    if (read_size > (it.file_size_ - it.cur_iter_pos_)) {
      tmp_read_size = read_size;
      read_size = it.file_size_ - it.cur_iter_pos_;
      chunk_size = read_size + read_off;
    }
    int64_t tmp_file_size = -1;
    if (0 == read_size) {
      // ret end
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = read_file(
          it.chunk_mem_ + read_off, tmp_read_size, it.cur_iter_pos_,
          it.file_size_, it.cur_iter_pos_, tmp_file_size))) {
      }
      if (OB_ITER_END != tmp_ret) {
        if (OB_UNLIKELY(OB_SUCCESS == tmp_ret)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected read succ", K(read_size), K(tmp_read_size), K(it), K(read_off));
        } else {
          ret = tmp_ret;
          LOG_WARN("unexpected status", K(ret), K(tmp_ret));
        }
      } else {
        ret = OB_ITER_END;
      }
    } else if (OB_FAIL(read_file(it.chunk_mem_ + read_off, read_size, it.cur_iter_pos_,
                                 it.file_size_, it.cur_iter_pos_, tmp_file_size))) {
    } else {
      int64_t cur_pos = 0;
      Block* block = reinterpret_cast<Block *>(it.chunk_mem_);
      Block* prev_block = block;
      do {
        if (!block->magic_check()) {
          ret = OB_ERR_UNEXPECTED;
        } else if (block->blk_size_ <= chunk_size - cur_pos) {
          prev_block->next_ = block;
          cur_pos += block->blk_size_;
          prev_block = block;
          block = reinterpret_cast<Block*>(it.chunk_mem_ + cur_pos);
          read_n_blocks++;
          if (prev_block->blk_size_ == 0 || prev_block->rows_ == 0) {
            ret = OB_INNER_STAT_ERROR;
          } else if (OB_FAIL(prev_block->swizzling(NULL))){
          } else {
            it.chunk_n_rows_ += prev_block->rows_;
          }
        } else {
          break;
        }
      } while (OB_SUCC(ret) && cur_pos < (chunk_size - BlockBuffer::HEAD_SIZE));

      if (OB_SUCC(ret)) {
        prev_block->next_ = NULL;
        it.cur_iter_blk_ = reinterpret_cast<Block *>(it.chunk_mem_);
        it.cur_iter_pos_ += read_size;
        it.cur_chunk_n_blocks_ = read_n_blocks;
        it.cur_nth_blk_ += read_n_blocks;
        if (0 == read_n_blocks) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to chunk read blocks", K(read_n_blocks), K(it), K(read_off),
            K(it.cur_iter_pos_), K(read_size), K(chunk_size), K(cur_pos), K(ret));
        } else {
        }
      }
    }
  }
  if (OB_ITER_END == ret) {
    it.set_read_file_iter_end();
    if (nullptr != it.chunk_mem_) {
      callback_free(it.chunk_read_size_);
      allocator_->free(it.chunk_mem_);
    }
    it.chunk_mem_ = nullptr;
    it.cur_iter_blk_ = nullptr;
  }
  return ret;
}

/* get next block from BlockItemList(when all rows in mem) or read from file
 * and let it.cur_iter_blk_ point to the new block
 */
int ObChunkRowStore::load_next_block(ChunkIterator &it)
{
  int ret = OB_SUCCESS;
  if (it.file_size_ != file_size_) {
    it.reset_cursor(file_size_);
  }
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (it.cur_nth_blk_ < -1 || it.cur_nth_blk_ >= n_blocks_) {
    ret = OB_INVALID_ARGUMENT;
  } else if (is_file_open() && !it.read_file_iter_end()) {
    if (it.chunk_read_size_ > 0 && it.chunk_read_size_ >= this->max_blk_size_) {
      if (OB_FAIL(load_next_chunk_blocks(it))) {
      }
    } else {
      /* read from file:
      * read a block of min_size first, if the actual size is lager, then alloc a new block and copy
      * the previous block to the head of it, then read the rest from file
      * */
      int64_t block_size = INT64_MAX == min_blk_size_ ? it.chunk_read_size_ :
                      (it.chunk_read_size_ > min_blk_size_ ? it.chunk_read_size_ : min_blk_size_);
      int64_t read_size = block_size - sizeof(BlockBuffer);
      if (it.chunk_mem_ != NULL) {
        // Generally, the data written to disk will be greater than one block size, but when testing, such as forcing the write of N lines, it will be written to disk
        // will cause reading data to, since the entire data is relatively small, less than blk size, at this time it will allocate memory the same size as the data being written (to save memory)
        // This situation will lead to here, mainly used for testing the code
        allocator_->free(it.chunk_mem_);
        it.chunk_mem_ = NULL;
        it.cur_iter_blk_ = NULL;
        callback_free(it.chunk_read_size_);
      }
      if (NULL == it.cur_iter_blk_) {
        //for the first time, it.cur_iter_blk_ need to be freed by Iterator
        if (OB_FAIL(alloc_block_buffer(it.cur_iter_blk_, block_size, true))) {
        } else {
          it.cur_iter_blk_buf_ = it.cur_iter_blk_->get_buffer();
          LOG_DEBUG("alloc a block for reading", K(ret), K(it.cur_iter_blk_),
              K(it.cur_iter_blk_->get_buffer()->capacity()), K(block_size));
        }
      } else {
        if (it.cur_iter_blk_buf_ == NULL) {
          ret = OB_INNER_STAT_ERROR;
        } else {
          it.cur_iter_blk_buf_->reuse();
          LOG_DEBUG("StoreRow reuse block", K(it.cur_iter_blk_buf_->capacity()),
              K(it.cur_iter_blk_->get_buffer()), K_(it.cur_iter_blk_buf), K(block_size), K(read_size));
        }
      }

      if (OB_SUCC(ret)) {
        int64_t blk_cap = it.cur_iter_blk_buf_->capacity();
        int64_t tmp_file_size = -1;
        LOG_DEBUG("RowStore need read file", K(it.cur_iter_blk_buf_->capacity()),
            K(it.cur_iter_blk_->blk_size_), K(block_size), K_(file_size), K_(it.cur_iter_pos),
            K_(it.cur_iter_blk));
        //read a normal size of Block first
        if (OB_FAIL(read_file(it.cur_iter_blk_, read_size, it.cur_iter_pos_,
                              it.file_size_, it.cur_iter_pos_, tmp_file_size))) {
          if (OB_ITER_END != ret) {
          }
        } else if (!it.cur_iter_blk_->magic_check()) {
          ret = OB_ERR_UNEXPECTED;
        } else if (it.cur_iter_blk_->blk_size_ > read_size) {
          Block* blk_head = it.cur_iter_blk_;
          int64_t ac_size = it.cur_iter_blk_->blk_size_;
          int64_t pre_size = it.cur_iter_blk_buf_->mem_size();
          if (read_size + it.cur_iter_pos_ > file_size_) {
            ret = OB_ITER_END;
          } else if (blk_cap < ac_size) {
            //need to alloc new blk to hold data
            it.cur_iter_blk_ = nullptr;
            if (OB_FAIL(alloc_block_buffer(it.cur_iter_blk_, ac_size + sizeof(BlockBuffer), true))
                || ac_size != it.cur_iter_blk_->get_buffer()->capacity()) {
              allocator_->free(blk_head);
              callback_free(pre_size);
            } else {
              it.cur_iter_blk_buf_ = it.cur_iter_blk_->get_buffer();
              MEMCPY(it.cur_iter_blk_, blk_head, read_size);
              allocator_->free(blk_head);
              callback_free(pre_size);
            }
          }
          if (OB_FAIL(ret)) {
          } else if (OB_FAIL(read_file(static_cast<void*>(it.cur_iter_blk_->payload_ + read_size - sizeof(Block)),
                                ac_size - read_size,
                                it.cur_iter_pos_ + read_size,
                                it.file_size_,
                                it.cur_iter_pos_,
                                tmp_file_size))) {
              if (OB_ITER_END != ret) {
              }
          } else {
            read_size = ac_size;
          }
        }

        if (OB_FAIL(ret)) {
          if (OB_ITER_END == ret) {
            it.set_read_file_iter_end();
          }
        } else if (OB_FAIL(it.cur_iter_blk_->swizzling(NULL))){
        } else if (read_size <= 0
                  || it.cur_iter_blk_->blk_size_ == 0
                  || it.cur_iter_blk_->rows_ == 0) {
          ret = OB_INNER_STAT_ERROR;
        }
      }
      if (OB_SUCC(ret)) {
        it.cur_iter_pos_ += read_size;
        it.cur_nth_blk_++;
        it.cur_chunk_n_blocks_ = 1;
        it.cur_iter_blk_->next_ = NULL;
        it.chunk_n_rows_ = it.cur_iter_blk_->rows_;
      } else {
        // first read disk data then read memory data, so it must free cur_iter_blk_
        if (NULL != it.cur_iter_blk_) {
          callback_free(it.cur_iter_blk_->get_buffer()->mem_size());
          allocator_->free(it.cur_iter_blk_);
          it.cur_iter_blk_ = NULL;
          it.cur_iter_blk_buf_ = nullptr;
        }
      }
    }
  } else {
    it.set_read_file_iter_end();
  }
  // when it's iter_end from disk or disk is no data
  // then read data from in memory
  if (OB_FAIL(ret) && OB_ITER_END != ret) {
  } else if (!is_file_open() || it.read_file_iter_end()) {
    ret = OB_SUCCESS;
    if (OB_ISNULL(blocks_.get_first())) {
      it.set_read_mem_iter_end();
      ret = OB_ITER_END;
    } else if (nullptr == blocks_.get_first()) {
      ret = OB_ITER_END;
    } else {
      //all in mem or read blocks in mem at first
      it.cur_iter_blk_ = blocks_.get_first();
      it.cur_chunk_n_blocks_ = blocks_.get_size();
      it.cur_nth_blk_ += it.cur_chunk_n_blocks_;
      it.chunk_n_rows_ = this->get_row_cnt_in_memory();
      if (it.cur_nth_blk_ != n_blocks_ - 1) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  return ret;
}

int ObChunkRowStore::get_store_row(RowIterator &it, const StoredRow *&sr)
{
  int ret = OB_SUCCESS;
  if (!is_inited() || NULL == it.cur_iter_blk_) {
    ret = OB_NOT_INIT;
  } else {
    if (OB_UNLIKELY(!it.cur_blk_has_next())) {
      if (OB_UNLIKELY(Block::MAGIC == it.cur_iter_blk_->magic_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (it.cur_iter_blk_->get_next() != NULL) {
        it.cur_iter_blk_ = it.cur_iter_blk_->get_next();
        it.cur_row_in_blk_ = 0;
        it.cur_pos_in_blk_ = 0;
        it.cur_nth_block_++;
      } else if (it.cur_nth_block_ != it.n_blocks_ - 1) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ret = OB_ITER_END;
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(it.cur_iter_blk_->get_store_row(it.cur_pos_in_blk_, sr))) {
      } else {
        it.cur_row_in_blk_++;
      }
    }
  }
  return ret;
}

////////////////////////////////////////////////////////////////
ObChunkRowStore::RowIterator::RowIterator()
  : store_(NULL),
    cur_iter_blk_(NULL),
    cur_row_in_blk_(0),
    cur_pos_in_blk_(0),
    n_blocks_(0),
    cur_nth_block_(0)
{

}

ObChunkRowStore::RowIterator::RowIterator(ObChunkRowStore *row_store)
  : store_(row_store),
    cur_iter_blk_(NULL),
    cur_row_in_blk_(0),
    cur_pos_in_blk_(0),
    n_blocks_(0),
    cur_nth_block_(0)
{
}

int ObChunkRowStore::RowIterator::init(ChunkIterator *chunk_it)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(chunk_it) || !chunk_it->is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    store_ = chunk_it->store_;
    row_.projector_ = NULL;
    row_.projector_size_ = 0;
  }
  return ret;
}

ObChunkRowStore::ChunkIterator::ChunkIterator()
  : store_(NULL),
    cur_iter_blk_(NULL),
    cur_iter_blk_buf_(NULL),
    cur_nth_blk_(-1),
    cur_chunk_n_blocks_(0),
    cur_iter_pos_(0),
    file_size_(0),
    chunk_read_size_(0),
    chunk_mem_(NULL),
    chunk_n_rows_(0),
    iter_end_flag_(IterEndState::PROCESSING)
{

}

int ObChunkRowStore::ChunkIterator::init(ObChunkRowStore *store, int64_t chunk_read_size)
{
  int ret = OB_SUCCESS;
  store_ = store;
  chunk_read_size_ = chunk_read_size;
  CK (chunk_read_size >= 0);
  return ret;
}

ObChunkRowStore::ChunkIterator::~ChunkIterator()
{
  reset_cursor(0);
}

void ObChunkRowStore::ChunkIterator::reset_cursor(const int64_t file_size)
{
  file_size_ = file_size;

  if (chunk_mem_ != NULL) {
    store_->allocator_->free(chunk_mem_);
    chunk_mem_ = NULL;
    cur_iter_blk_ = NULL;
    store_->callback_free(chunk_read_size_);
    if (read_file_iter_end()) {
      LOG_ERROR_RET(OB_ERR_UNEXPECTED, "unexpect status: chunk mem is allocated, but don't free");
    }
  }
  if (!read_file_iter_end() && cur_iter_blk_ != NULL) {
    if (cur_iter_pos_ > 0 && NULL != store_) {
      if (nullptr != cur_iter_blk_buf_) {
        store_->callback_free(cur_iter_blk_buf_->mem_size());
      }
      store_->allocator_->free(cur_iter_blk_);
    }
  }
  cur_iter_blk_buf_ = nullptr;
  cur_iter_blk_ = nullptr;
  cur_nth_blk_ = -1;
  cur_iter_pos_ = 0;
  iter_end_flag_ = IterEndState::PROCESSING;
}

void ObChunkRowStore::ChunkIterator::reset()
{
  reset_cursor(0);
  chunk_read_size_ = 0;
  chunk_n_rows_ = 0;
}

int ObChunkRowStore::ChunkIterator::load_next_chunk(RowIterator& it)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_NOT_INIT;
  } else if (!has_next_chunk()){
    ret = OB_ITER_END;
  } else {
    if (chunk_read_size_ > 0) {
      cur_iter_blk_ = it.cur_iter_blk_;
    }
    if (OB_FAIL(store_->load_next_block(*this))) {
    } else {
      it.store_ = store_;
      it.cur_iter_blk_ = cur_iter_blk_;
      it.cur_pos_in_blk_ = 0;
      it.cur_row_in_blk_ = 0;
      it.n_blocks_ = cur_chunk_n_blocks_;
      it.cur_nth_block_ = 0;
    }
  }
  return ret;
}

int ObChunkRowStore::Iterator::init(ObChunkRowStore *store, int64_t chunk_read_size)
{
  reset();
  switch (store->row_store_mode_) {
  case STORE_MODE::WITHOUT_PROJECTOR:
    (Iterator::convert_row_with_obj_fun_) = &Iterator::convert_to_row_with_obj;
    Iterator::convert_row_fun_ = &Iterator::convert_to_row;
    break;
  case STORE_MODE::FULL:
    Iterator::convert_row_with_obj_fun_ = &Iterator::convert_to_row_full_with_obj;
    Iterator::convert_row_fun_ = &Iterator::convert_to_row_full;
    break;
  }
  int ret = OB_SUCCESS;
  if (OB_FAIL(chunk_it_.init(store, chunk_read_size))
      || OB_FAIL(row_it_.init(&chunk_it_))) {
  }
  return ret;
}

int ObChunkRowStore::Iterator::get_next_row(ObNewRow &row)
{
  int ret = OB_SUCCESS;
  const StoredRow *sr = NULL;
  if (OB_FAIL(get_next_row(sr))) {
    if (OB_ITER_END != ret) {
    }
  } else if (OB_FAIL((this->*convert_row_with_obj_fun_)(row_it_, row, sr))) {
  }
  return ret;
}

int ObChunkRowStore::Iterator::get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  const StoredRow *sr = NULL;
  if (OB_FAIL(get_next_row(sr))) {
    if (OB_ITER_END != ret) {
    }
  } else if (OB_FAIL((this->*convert_row_fun_)(row_it_, row, sr))) {
  }
  return ret;
}

int ObChunkRowStore::Iterator::get_next_row(const StoredRow *&sr)
{
  int ret = OB_SUCCESS;
  if (!start_iter_) {
    if (OB_FAIL(chunk_it_.load_next_chunk(row_it_))) {
      if (OB_ITER_END != ret) {
      }
    } else {
      start_iter_ = true;
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(row_it_.get_next_row(sr))) {
    if (OB_ITER_END == ret) {
      if (OB_FAIL(chunk_it_.load_next_chunk(row_it_))) {
        if (OB_ITER_END != ret) {
        }
      } else if (OB_FAIL(row_it_.get_next_row(sr))) {
      }
    }
  }
  return ret;
}

int ObChunkRowStore::RowIterator::get_next_row(const StoredRow *&sr)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(store_->get_store_row(*this, sr))) {
    if (OB_ITER_END != ret) {
    }
  }
  return ret;
}

/* from StoredRow to NewRow */
int ObChunkRowStore::RowIterator::convert_to_row(common::ObNewRow &row, const StoredRow *sr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sr)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ret = store_row2new_row(row, *sr);
  }
  return ret;
}


int ObChunkRowStore::RowIterator::store_row2new_row(common::ObNewRow &row, const StoredRow &sr)
{
  int ret = OB_SUCCESS;
  if (row.count_ < sr.cnt_){
    ret = OB_BUF_NOT_ENOUGH;
    OB_LOG(WARN, "column buffer count is not enough", K_(row.count), K_(sr.cnt));
  } else {
    //do not overwite row.count_
    //row.count_ = sr.cnt_;
    MEMCPY(row.cells_, sr.cells(), sizeof(DatumValue) * sr.cnt_);
  }
  return ret;
}

/* from StoredRow to NewRow */
int ObChunkRowStore::RowIterator::convert_to_row_full(common::ObNewRow &row, const StoredRow *sr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sr)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (row.count_ < sr->cnt_){
    ret = OB_BUF_NOT_ENOUGH;
    OB_LOG(WARN, "column buffer count is not enough", K_(row.count), K(store_->get_col_cnt()));
  } else {
    //do not overwite row.count_
    //row.count_ = sr->cnt_;
    MEMCPY(row.cells_, sr->cells(), sizeof(DatumValue) * sr->cnt_);
    MEMCPY(row.projector_, store_->projector_, sizeof(*store_->projector_) * sr->cnt_);
    row.projector_size_ = store_->projector_size_;
  }
  return ret;
}

/* from StoredRow to NewRow */
int ObChunkRowStore::RowIterator::convert_to_row(common::ObNewRow *&row, const StoredRow *sr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sr)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    //do not overwite row.count_
    //row.count_ = sr->cnt_;
    row_.projector_ = NULL;
    row_.projector_size_ = 0;
    row_.count_ = sr->cnt_;
    row_.cells_ = const_cast<DatumValue *>(sr->cells());
    row = &row_;
  }
  return ret;
}

/* from StoredRow to NewRow */
int ObChunkRowStore::RowIterator::convert_to_row_full(common::ObNewRow *&row, const StoredRow *sr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sr)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    row_.projector_ = store_->projector_;
    row_.projector_size_ = store_->projector_size_;
    row_.count_ = sr->cnt_;
    row_.cells_ = const_cast<DatumValue *>(sr->cells());
    row = &row_;
  }
  return ret;
}

int ObChunkRowStore::get_timeout(int64_t &timeout_ms)
{
  int ret = OB_SUCCESS;
  const int64_t timeout_us = THIS_WORKER.get_timeout_remain();
  if (timeout_us / 1000 <= 0) {
    ret = OB_TIMEOUT;
  } else {
    timeout_ms = timeout_us / 1000;
  }
  return ret;
}

int ObChunkRowStore::alloc_dir_id()
{
  int ret = OB_SUCCESS;
  if (-1 == io_.dir_id_ && OB_FAIL(ObChunkStoreUtil::alloc_dir_id(io_.dir_id_))) {
  }
  return ret;
}

int ObChunkRowStore::write_file(void *buf, int64_t size)
{
  int ret = OB_SUCCESS;
  int64_t timeout_ms = 0;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (size < 0 || (size > 0 && NULL == buf)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_timeout(timeout_ms))) {
  } else {
    if (!is_file_open()) {
      if (-1 == io_.dir_id_) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(data_plane::tmp_file_open(io_.fd_, io_.dir_id_))) {
      } else {
        file_size_ = 0;
        io_.io_desc_.set_wait_event(ObWaitEventIds::ROW_STORE_DISK_WRITE);
        io_.io_timeout_ms_ = timeout_ms;
      }
    }
    ret = OB_E(EventTable::EN_8) ret;
  }
  if (OB_SUCC(ret) && size > 0) {
    set_io(size, static_cast<char *>(buf));
    if (OB_FAIL(data_plane::tmp_file_write(io_))) {
    }
  }
  if (OB_SUCC(ret)) {
    file_size_ += size;
    if (nullptr != callback_) {
      callback_->dumped(size);
    }
  }
  return ret;
}

int ObChunkRowStore::read_file(void *buf, const int64_t size, const int64_t offset,
                               const int64_t file_size, const int64_t cur_pos,
                               int64_t &tmp_file_size)
{
  int ret = OB_SUCCESS;
  int64_t timeout_ms = 0;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (offset < 0 || size < 0 || (size > 0 && NULL == buf)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_timeout(timeout_ms))) {
  }
  int64_t read_size = file_size - cur_pos;
  if (OB_FAIL(ret)) {
  } else if (0 >= size) {
    CK (cur_pos >= file_size);
    OX (ret = OB_ITER_END);
  } else {
    this->set_io(size, static_cast<char *>(buf));
    io_.io_desc_.set_wait_event(ObWaitEventIds::ROW_STORE_DISK_READ);
    io_.io_timeout_ms_ = timeout_ms;
    data_plane::ObTmpFileIOHandle handle;
    if (0 == read_size
        && OB_FAIL(data_plane::tmp_file_get_size(io_.fd_, tmp_file_size))) {
    } else if (OB_FAIL(data_plane::tmp_file_pread(io_, offset, handle))) {
      if (OB_ITER_END != ret) {
      }
    } else if (handle.get_done_size() != size) {
      ret = OB_INNER_STAT_ERROR;
    }
  }
  return ret;
}

bool ObChunkRowStore::need_dump(int64_t extra_size)
{
  bool dump = false;
  if (!config::enable_sql_operator_dump()) {
    // no dump
  } else if (mem_limit_ > 0) {
    if (mem_used_ + extra_size > mem_limit_) {
      dump = true;
    }
  }
  return dump;
}

int ObChunkStoreUtil::alloc_dir_id(int64_t &dir_id)
{
  int ret = OB_SUCCESS;
  dir_id = 0;
  if (OB_FAIL(data_plane::tmp_file_alloc_dir(dir_id))) {
  }
  return ret;
}

namespace {
// serialization::decode(vi64) historically examines a continuation byte before
// its length check. Check every byte before reading any untrusted row header.
int decode_chunk_integer(const char *buf, int64_t length, int64_t &pos, int64_t &value)
{
  if (buf == nullptr || pos < 0 || pos > length) { return OB_INVALID_ARGUMENT; }
  int64_t cursor = pos;
  uint64_t bits = 0;
  for (int shift = 0; shift < 70; shift += 7) {
    if (cursor >= length) { return OB_DESERIALIZE_ERROR; }
    const uint8_t byte = static_cast<uint8_t>(buf[cursor++]);
    if (shift == 63 && (byte & 0xfe) != 0) { return OB_DESERIALIZE_ERROR; }
    bits |= uint64_t(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0) {
      MEMCPY(&value, &bits, sizeof(value));
      pos = cursor;
      return OB_SUCCESS;
    }
  }
  return OB_DESERIALIZE_ERROR;
}

bool chunk_row_range(const ObChunkRowStore::Block &block, int64_t offset,
                     const ObChunkRowStore::StoredRow *&row)
{
  const int64_t bytes = int64_t(block.blk_size_) - sizeof(ObChunkRowStore::Block);
  if (offset < 0 || offset > bytes
      || bytes - offset < sizeof(ObChunkRowStore::StoredRow)) { return false; }
  const auto *candidate = reinterpret_cast<const ObChunkRowStore::StoredRow *>(block.payload_ + offset);
  const int64_t header = sizeof(*candidate);
  const int64_t cells = int64_t(candidate->cnt_) * sizeof(DatumValue);
  if (candidate->row_size_ < header || candidate->row_size_ > bytes - offset
      || cells > candidate->row_size_ - header
      || candidate->extra_size_ > candidate->row_size_ - header - cells) { return false; }
  row = candidate;
  return true;
}

// The same traversal computes size and writes cells, so new wire never exposes
// native DatumValue/DatumParameter layouts, padding, addresses or vptrs.
class ChunkWireWriter
{
public:
  ChunkWireWriter(char *buf, int64_t length, int64_t pos)
    : buf_(buf), length_(length), pos_(pos), ret_(OB_SUCCESS) {}
  void integer(int64_t value)
  {
    if (ret_ == OB_SUCCESS) {
      if (buf_ == nullptr) {
        if (__builtin_add_overflow(pos_, serialization::encoded_length(value), &pos_)) { ret_ = OB_SIZE_OVERFLOW; }
      } else { ret_ = serialization::encode(buf_, length_, pos_, value); }
    }
  }
  void boolean(bool value)
  {
    if (ret_ == OB_SUCCESS) {
      if (buf_ == nullptr) {
        if (__builtin_add_overflow(pos_, int64_t(1), &pos_)) { ret_ = OB_SIZE_OVERFLOW; }
      } else { ret_ = serialization::encode(buf_, length_, pos_, value); }
    }
  }
  void bytes(const char *data, int64_t size)
  {
    if (ret_ == OB_SUCCESS) {
      if (size < 0 || pos_ < 0 || size > INT64_MAX - pos_) { ret_ = OB_SIZE_OVERFLOW; }
      else if (buf_ != nullptr && (pos_ > length_ || size > length_ - pos_)) { ret_ = OB_BUF_NOT_ENOUGH; }
      else {
        if (buf_ != nullptr && size > 0) { MEMCPY(buf_ + pos_, data, size); }
        pos_ += size;
      }
    }
  }
  void cell(const DatumValue &value)
  {
    if (ret_ == OB_SUCCESS) {
      int64_t bytes = 0;
      if (buf_ == nullptr) {
        ret_ = NativeValueFlat::row_value_size(value, bytes);
        if (ret_ == OB_SUCCESS && __builtin_add_overflow(pos_, bytes, &pos_)) { ret_ = OB_SIZE_OVERFLOW; }
      } else { ret_ = NativeValueFlat::encode_row_value(value, buf_, length_, pos_); }
    }
  }
  int result() const { return ret_; }
  int64_t position() const { return pos_; }
private:
  char *buf_; int64_t length_; int64_t pos_; int ret_;
};

int encode_chunk_rows(const ObChunkRowStore::BlockList &blocks, int64_t row_count,
                       int64_t column_count, uint32_t extra_length, ChunkWireWriter &writer)
{
  int ret = OB_SUCCESS;
  int64_t rows = 0;
  writer.integer(row_count);
  for (const auto *block = blocks.get_first(); OB_SUCC(ret) && block != nullptr; block = block->get_next()) {
    int64_t offset = 0;
    for (uint32_t i = 0; OB_SUCC(ret) && i < block->rows_; ++i) {
      const ObChunkRowStore::StoredRow *row = nullptr;
      if (!chunk_row_range(*block, offset, row) || row->cnt_ != column_count
          || row->extra_size_ < extra_length) { ret = OB_INVALID_DATA; }
      else {
        writer.integer(row->cnt_);
        writer.integer(extra_length);
        writer.bytes(static_cast<const char *>(row->get_extra_payload()), extra_length);
        for (uint32_t j = 0; j < row->cnt_ && writer.result() == OB_SUCCESS; ++j) { writer.cell(row->cells()[j]); }
        ret = writer.result();
        offset += row->row_size_;
        ++rows;
      }
    }
  }
  if (OB_SUCC(ret) && rows != row_count) { ret = OB_INVALID_DATA; }
  return ret;
}
} // namespace

int ObChunkRowStore::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  const int64_t bytes = get_serialize_size_();
  const int64_t header = serialization::encoded_length(int64_t(2)) + serialization::OB_SERIALIZE_SIZE_NEED_BYTES;
  int64_t cursor = pos;
  if (buf == nullptr || pos < 0 || pos > buf_len) { ret = OB_INVALID_ARGUMENT; }
  else if (bytes < 0 || bytes > INT64_MAX - header) { ret = OB_INVALID_DATA; }
  else if (header + bytes > buf_len - pos) { ret = OB_BUF_NOT_ENOUGH; }
  else if (OB_FAIL(serialization::encode(buf, buf_len, cursor, int64_t(2)))) {}
  else {
    const int64_t length_slot = cursor;
    cursor += serialization::OB_SERIALIZE_SIZE_NEED_BYTES;
    if (OB_FAIL(serialize_(buf, buf_len, cursor))) {}
    else {
      int64_t header_pos = 0;
      ret = serialization::encode_fixed_bytes_i64(buf + length_slot,
          serialization::OB_SERIALIZE_SIZE_NEED_BYTES, header_pos, bytes);
    }
    if (OB_SUCC(ret)) { pos = cursor; }
  }
  return ret;
}

int ObChunkRowStore::serialize_(char *buf, const int64_t buf_len, int64_t &pos) const
{
  if (enable_dump_ || has_dumped_) { return OB_ERR_UNEXPECTED; }
  if (buf == nullptr || pos < 0 || pos > buf_len) { return OB_INVALID_ARGUMENT; }
  ChunkWireWriter writer(buf, buf_len, pos);
  writer.integer(ctx_id_);
  writer.integer(mem_limit_);
  writer.integer(default_block_size_);
  writer.integer(col_count_);
  writer.boolean(false);
  writer.integer(row_extend_size_);
  int ret = writer.result();
  if (OB_SUCC(ret)) { ret = encode_chunk_rows(blocks_, row_cnt_, col_count_, row_extend_size_, writer); }
  if (OB_SUCC(ret)) { pos = writer.position(); }
  return ret;
}

int64_t ObChunkRowStore::get_serialize_size_() const
{
  if (enable_dump_ || has_dumped_) { return -1; }
  ChunkWireWriter writer(nullptr, 0, 0);
  writer.integer(ctx_id_); writer.integer(mem_limit_);
  writer.integer(default_block_size_); writer.integer(col_count_);
  writer.boolean(false); writer.integer(row_extend_size_);
  const int ret = writer.result() == OB_SUCCESS
      ? encode_chunk_rows(blocks_, row_cnt_, col_count_, row_extend_size_, writer) : writer.result();
  return ret == OB_SUCCESS ? writer.position() : -1;
}

int64_t ObChunkRowStore::get_serialize_size() const
{
  int64_t size = get_serialize_size_();
  const int64_t header = serialization::encoded_length(int64_t(2)) + serialization::OB_SERIALIZE_SIZE_NEED_BYTES;
  return size < 0 || __builtin_add_overflow(size, header, &size) ? -1 : size;
}

int ObChunkRowStore::deserialize(const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t cursor = pos, version = 0, body_length = 0;
  if (buf == nullptr || pos < 0 || pos >= data_len) { ret = OB_INVALID_ARGUMENT; }
  else if (OB_FAIL(decode_chunk_integer(buf, data_len, cursor, version))) {}
  else if (version != 1 && version != 2) { ret = OB_NOT_SUPPORTED; }
  else if (OB_FAIL(decode_chunk_integer(buf, data_len, cursor, body_length))) {}
  else if (body_length < 0 || body_length > data_len - cursor) { ret = OB_DESERIALIZE_ERROR; }
  else {
    int64_t body_pos = 0;
    ret = deserialize_body(buf + cursor, body_length, body_pos, version);
    // Keep the UNIS outer cursor policy on body failure; destination ownership
    // changes only after the complete body has passed validation.
    pos = cursor + body_length;
  }
  return ret;
}

int ObChunkRowStore::deserialize_(const char *buf, const int64_t data_len, int64_t &pos)
{ return deserialize_body(buf, data_len, pos, 2); }

int ObChunkRowStore::deserialize_body(const char *buf, int64_t length, int64_t &pos, int64_t version)
{
  int ret = OB_SUCCESS;
  int64_t cursor = pos;
  int64_t context = ctx_id_, limit = mem_limit_, block_size = default_block_size_;
  int64_t columns = col_count_, extra = row_extend_size_, count = 0;
  bool dump = false;
  const bool legacy = version == 1;
  if (buf == nullptr || pos < 0 || pos > length) { return OB_INVALID_ARGUMENT; }
  // Old UNIS bodies allow omitted trailing fields. New logical records require
  // their complete header and exact row/cell counts.
  auto integer = [&](int64_t &value) {
    if (ret == OB_SUCCESS && (!legacy || cursor < length)) {
      ret = decode_chunk_integer(buf, length, cursor, value);
    }
  };
  integer(context); integer(limit); integer(block_size); integer(columns);
  if (OB_SUCC(ret) && (!legacy || cursor < length)) {
    if (cursor >= length) { ret = OB_DESERIALIZE_ERROR; }
    else { dump = buf[cursor++] != 0; }
  }
  integer(extra); integer(count);
  if (OB_SUCC(ret) && (block_size <= 0 || block_size > UINT32_MAX || columns < 0 || columns > INT32_MAX
      || extra < 0 || extra > UINT32_MAX || count < 0 || count > length - cursor || dump)) {
    ret = OB_INVALID_DATA;
  }
  ObChunkRowStore candidate(allocator_);
  // The same callback sees candidate allocations and rollback frees. On success
  // ownership/accounting move together; no decoded row borrows temporary bytes.
  candidate.set_callback(callback_);
  if (OB_SUCC(ret)) {
    ret = candidate.init(limit, context, "ChunkRowDE", false, WITHOUT_PROJECTOR, static_cast<uint32_t>(extra));
    candidate.default_block_size_ = block_size;
  }
  ObArenaAllocator temporary("ChunkWireRead");
  const int64_t iterations = count;
  for (int64_t i = 0; OB_SUCC(ret) && i < iterations; ++i) {
    int64_t block_length = 0, rows = legacy ? 0 : 1, block_start = cursor;
    if (legacy) {
      if (OB_SUCC(ret)) { ret = decode_chunk_integer(buf, length, cursor, block_length); }
      if (OB_SUCC(ret)) { ret = decode_chunk_integer(buf, length, cursor, rows); }
      block_start = cursor;
      if (OB_SUCC(ret) && (block_length < 16 || block_length > length - cursor
          || rows < 0 || rows > block_length / 16)) { ret = OB_DESERIALIZE_ERROR; }
    }
    int64_t row_offset = 0;
    for (int64_t j = 0; OB_SUCC(ret) && j < rows; ++j) {
      temporary.reuse();
      int64_t cell_count = 0, extra_length = 0, old_row_length = 0;
      const char *old_row = legacy ? buf + block_start + row_offset : nullptr;
      const char *extra_data = nullptr;
      if (legacy) {
        if (row_offset > block_length || block_length - row_offset < 16) { ret = OB_DESERIALIZE_ERROR; }
        else {
          uint32_t header[4];
          MEMCPY(header, old_row, sizeof(header));
          cell_count = header[0]; old_row_length = header[1]; extra_length = header[2];
          if (old_row_length < 16 || old_row_length > block_length - row_offset
              || cell_count != columns || cell_count > (old_row_length - 16) / 16
              || extra_length < extra || extra_length > old_row_length - 16 - cell_count * 16) {
            ret = OB_INVALID_DATA;
          } else { extra_data = old_row + 16 + cell_count * 16; }
        }
      } else {
        integer(cell_count); integer(extra_length);
        if (OB_SUCC(ret) && (cell_count != columns || cell_count > (length - cursor) / 11
            || extra_length != extra || extra_length > length - cursor)) { ret = OB_INVALID_DATA; }
        if (OB_SUCC(ret)) { extra_data = buf + cursor; cursor += extra_length; }
      }
      DatumValue *cells = nullptr;
      if (OB_SUCC(ret) && cell_count > 0) {
        cells = static_cast<DatumValue *>(temporary.alloc(cell_count * sizeof(DatumValue)));
        if (cells == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      }
      for (int64_t k = 0; OB_SUCC(ret) && k < cell_count; ++k) {
        new (&cells[k]) DatumValue();
        ret = legacy
            ? NativeValueFlat::read_legacy_1d6_value(old_row, old_row_length, 16 + k * 16, temporary, cells[k])
            : NativeValueFlat::decode_row_value(buf, length, cursor, temporary, cells[k]);
      }
      if (OB_SUCC(ret)) {
        ObNewRow row; row.cells_ = cells; row.count_ = cell_count;
        StoredRow *stored = nullptr;
        if (OB_FAIL(candidate.add_row(row, &stored))) {}
        else if (extra > 0) { MEMCPY(stored->get_extra_payload(), extra_data, extra); }
      }
      if (legacy && OB_SUCC(ret)) { row_offset += old_row_length; }
    }
    if (legacy && OB_SUCC(ret)) { cursor = block_start + block_length; }
  }
  if (OB_SUCC(ret) && cursor != length) { ret = OB_DESERIALIZE_ERROR; }
  if (OB_SUCC(ret)) {
    // An empty logical store still carries its declared column count.
    candidate.col_count_ = columns;
    if (OB_FAIL(append_decoded_rows(candidate))) {}
    else { pos = cursor; }
  }
  return ret;
}

int ObChunkRowStore::append_decoded_rows(ObChunkRowStore &candidate)
{
  int ret = OB_SUCCESS;
  int64_t ignored = 0;
  if ((row_cnt_ > 0 && candidate.row_cnt_ > 0 && col_count_ != candidate.col_count_)
      || __builtin_add_overflow(row_cnt_, candidate.row_cnt_, &ignored)
      || __builtin_add_overflow(n_blocks_, candidate.n_blocks_, &ignored)
      || __builtin_add_overflow(blocks_.get_size(), candidate.blocks_.get_size(), &ignored)
      || __builtin_add_overflow(mem_hold_, candidate.mem_hold_, &ignored)
      || __builtin_add_overflow(mem_used_, candidate.mem_used_, &ignored)
      || __builtin_add_overflow(alloc_size_, candidate.alloc_size_, &ignored)
      || __builtin_add_overflow(free_size_, candidate.free_size_, &ignored)) {
    ret = OB_INVALID_DATA;
  } else {
    // Preserve the established deserialize-on-reuse append behavior. Incoming
    // blocks were fully validated in a separate store; this splice allocates
    // nothing and cannot partially replace an existing row or file.
    if (!inited_) { label_ = candidate.label_; row_store_mode_ = WITHOUT_PROJECTOR; }
    inited_ = true;
    ctx_id_ = candidate.ctx_id_; mem_limit_ = candidate.mem_limit_;
    default_block_size_ = candidate.default_block_size_; enable_dump_ = false;
    row_extend_size_ = candidate.row_extend_size_;
    if (row_cnt_ == 0) { col_count_ = candidate.col_count_; }
    if (cur_blk_ == nullptr) { cur_blk_ = candidate.cur_blk_; }
    while (!candidate.blocks_.is_empty()) { blocks_.add_last(candidate.blocks_.remove_first()); }
    max_blk_size_ = MAX(max_blk_size_, candidate.max_blk_size_);
    min_blk_size_ = MIN(min_blk_size_, candidate.min_blk_size_);
    n_blocks_ += candidate.n_blocks_; row_cnt_ += candidate.row_cnt_;
    mem_hold_ += candidate.mem_hold_; mem_used_ += candidate.mem_used_;
    max_hold_mem_ = MAX(max_hold_mem_, mem_hold_);
    alloc_size_ += candidate.alloc_size_; free_size_ += candidate.free_size_;
    candidate.cur_blk_ = nullptr;
    candidate.n_blocks_ = 0; candidate.row_cnt_ = 0;
    candidate.mem_hold_ = 0; candidate.mem_used_ = 0;
    candidate.alloc_size_ = 0; candidate.free_size_ = 0; candidate.callback_ = nullptr;
  }
  return ret;
}



} // end namespace sql
} // end namespace oceanbase
#endif // OCEANBASE_BASIC_OB_CHUNK_ROW_STORE_CPP_
