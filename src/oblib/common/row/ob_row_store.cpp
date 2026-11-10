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
#include "common/row/ob_row_store.h"
#include "common/row/ob_row_util.h"
#include "common/cell/ob_cell_reader.h"
#include "lib/allocator/page_arena.h"
namespace oceanbase
{
namespace common
{

////////////////////////////////////////////////////////////////
struct ObRowStore::BlockInfo
{
  explicit BlockInfo(int64_t block_size)
      :magic_(0xabcd4444abcd4444),
       next_(NULL),
       prev_(NULL),
       curr_data_pos_(0),
       block_size_(block_size)
  {
  }
  OB_INLINE int64_t get_remain_size() const{return block_size_ - curr_data_pos_ - sizeof(BlockInfo);}
  OB_INLINE int64_t get_remain_size_for_read(int64_t pos) const{return curr_data_pos_ - pos;}
  OB_INLINE char *get_buffer(){return data_ + curr_data_pos_;}
  OB_INLINE const char* get_buffer(int64_t pos) const{return data_ + pos;}
  OB_INLINE const char *get_buffer_head() const{return data_;}
  OB_INLINE void advance(const int64_t length){curr_data_pos_ += length;}
  OB_INLINE int64_t get_curr_data_pos() const{return curr_data_pos_;}
  OB_INLINE void set_curr_data_pos(int64_t pos){curr_data_pos_ = pos;}
  OB_INLINE BlockInfo *get_next_block() {return next_;}
  OB_INLINE const BlockInfo *get_next_block() const { return next_; }
  OB_INLINE void set_next_block(BlockInfo *blk) {next_ = blk;};
  OB_INLINE bool is_empty() const { return (0 == curr_data_pos_); }
  OB_INLINE void set_block_size(const int64_t block_size) {block_size_ = block_size;};
  OB_INLINE int64_t get_block_size() const {return block_size_;};

  int append_row(const ObIArray<int64_t> &reserved_columns,
                 const ObNewRow &row,
                 int64_t payload,
                 StoredRow *&stored_row);
  int append_row_by_copy(const ObIArray<int64_t> &reserved_columns,
                         const ObNewRow &row,
                         int64_t payload,
                         StoredRow *&stored_row);
  friend class ObRowStore::BlockList;
private:
#ifdef __clang__
  int64_t magic_ [[gnu::unused]];
#else
  int64_t magic_;
#endif
  BlockInfo *next_;
  BlockInfo *prev_;
  /**
   * cur_data_pos_ must be set when BlockInfo deserialized
   */
  int64_t curr_data_pos_;
  int64_t block_size_;
  char data_[0];
};

int ObRowStore::BlockInfo::append_row(const ObIArray<int64_t> &reserved_columns,
                                      const ObNewRow &row,
                                      int64_t payload,
                                      StoredRow *&stored_row)
{
  int ret = OB_SUCCESS;
  // store header
  const int64_t reserved_columns_count = reserved_columns.count();
  const int64_t reserved_cells_size = get_reserved_cells_size(reserved_columns_count);
  stored_row = reinterpret_cast<StoredRow *>(get_buffer());

  advance(reserved_cells_size);
  if (OB_ISNULL(stored_row)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "get invalid stored row", K(ret));
  } else if (OB_UNLIKELY(curr_data_pos_ > block_size_ - SIZEOF(*this))) {
    ret = OB_BUF_NOT_ENOUGH;
  } else {
    stored_row->reserved_cells_count_ = static_cast<int32_t>(reserved_columns_count);
    stored_row->payload_ = payload;
    // store row data
    DatumValue *reserved_values = nullptr;
    DatumValueBuffer::initialize(stored_row->reserved_cells_, reserved_columns_count, reserved_values);
    DatumValue cell_clone; char cell_clone_payload[16];
    ObCellWriter cell_writer;
    if (OB_FAIL(cell_writer.init(get_buffer(), get_remain_size(), DENSE))) {
    } else {
      int32_t reserved_count = 0;
      int64_t count = row.get_count();
      for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
        // honor the projector
        const DatumValue &cell = row.get_cell(i);
        if (cell.is_ext() && ObActionFlag::OP_END_FLAG == cell.get_ext()) {
          ret = OB_ERR_UNEXPECTED;
          OB_LOG(WARN, "append row is invalid", K(row));
        } else if (OB_FAIL(cell_writer.append(OB_INVALID_ID, cell, &cell_clone, cell_clone_payload))) {
          if (OB_BUF_NOT_ENOUGH != ret) {
            OB_LOG(WARN, "failed to append cell", K(ret));
          }
        }
        // whether reserve this cell
        for (int64_t j = 0; OB_SUCC(ret) && j < reserved_columns_count; ++j) {
          const int64_t index = reserved_columns.at(j);
          if (index == i) {
            ret = DatumValueBuffer::assign_view(cell_clone, reserved_values, reserved_columns_count, j);
            if (OB_SUCC(ret)) { ++reserved_count; }
            // should not break when ORDER BY c1, c1 DESC, ...
            // If the break goes out here, reserved_count will be less than reserved_columns_count
            // break;
          }
        } // end for j
      } // end for i
      if (OB_SUCC(ret)) {
        if (reserved_columns_count != reserved_count) {
          OB_LOG(WARN, "reserved columns count not equeal to actual add count",
                 K(reserved_columns_count), K(reserved_count));
          ret = OB_ERR_UNEXPECTED;
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(cell_writer.row_finish())) {
          if (OB_BUF_NOT_ENOUGH != ret) {
            OB_LOG(WARN, "failed to append cell", K(ret));
          }
        } else {
          // success
          const int64_t aligned_size = NativeValueFlat::span(cell_writer.size());
          if (aligned_size > get_remain_size()) { ret = OB_BUF_NOT_ENOUGH; }
          else {
            MEMSET(get_buffer() + cell_writer.size(), 0, aligned_size - cell_writer.size());
            stored_row->compact_row_size_ = static_cast<int32_t>(aligned_size);
            advance(aligned_size);
          }
          if (OB_UNLIKELY(curr_data_pos_ > block_size_ - SIZEOF(*this))) {
            ret = OB_ERR_UNEXPECTED;
            OB_LOG(ERROR, "fail to append row, overwrite the head", K(ret), K_(curr_data_pos),
                   K_(block_size), K(SIZEOF(*this)));
          }
        }
      }
    }
  }
  if (OB_BUF_NOT_ENOUGH == ret) {
    // rollback header
    advance(-reserved_cells_size);
    if (OB_UNLIKELY(0 > get_curr_data_pos())) {
      ret = OB_ERR_SYS;
      OB_LOG(WARN, "out of memory range, ",
          K(ret), K_(block_size), K_(curr_data_pos), K(reserved_cells_size));
    }
  }
  return ret;
}

int ObRowStore::BlockInfo::append_row_by_copy(const ObIArray<int64_t> &reserved_columns,
                                              const ObNewRow &row,
                                              int64_t payload,
                                              StoredRow *&stored_row)
{
  int ret = OB_SUCCESS;
  // store header
  stored_row = reinterpret_cast<StoredRow *>(get_buffer());
  if (OB_UNLIKELY(row.projector_size_ > 0)) {
    ret = OB_NOT_SUPPORTED;
    OB_LOG(WARN, "row with projector is not supported", K(ret), K(row.projector_size_));
  } else if (OB_ISNULL(stored_row)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "get invalid stored row", K(ret));
  } else {
    const int64_t header_size = get_reserved_cells_size(reserved_columns.count());
    const int64_t new_row_size = sizeof(ObNewRow) + row.get_deep_copy_size();

    if (OB_UNLIKELY(get_remain_size() < header_size + new_row_size)) {
      ret = OB_BUF_NOT_ENOUGH;
    } else {
      stored_row->reserved_cells_count_ = static_cast<int32_t>(reserved_columns.count());
      stored_row->payload_ = payload;
      stored_row->compact_row_size_ = static_cast<int32_t>(new_row_size);
      DatumValue *reserved_values = nullptr;
      DatumValueBuffer::initialize(stored_row->reserved_cells_, reserved_columns.count(), reserved_values);
      UNUSED(reserved_values);
      advance(header_size);

      ObNewRow *row_clone = new(get_buffer()) ObNewRow();
      int64_t pos = sizeof(ObNewRow);
      if (OB_FAIL(row_clone->deep_copy(row, get_buffer(), new_row_size, pos))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("failed to deep copy row", K(ret), K(new_row_size), K(pos));
      } else if (new_row_size != pos) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("size != pos", K(ret), K(new_row_size), K(pos));
      } else {
        advance(new_row_size);
        for (int64_t i = 0; OB_SUCC(ret) && i < reserved_columns.count(); ++i) {
          int64_t index = reserved_columns.at(i);
          if (OB_UNLIKELY(index >= row.count_)) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            stored_row->reserved_cells_[i] = row_clone->cells_[reserved_columns.at(i)];
          }
        }
      }
    }
  }
  return ret;
}

////////////////////////////////////////////////////////////////
ObRowStore::BlockList::BlockList()
    :first_(NULL),
     last_(NULL),
     count_(0),
     used_mem_size_(0)
{
};

void ObRowStore::BlockList::reset()
{
  first_ = NULL;
  last_ = NULL;
  count_ = 0;
  used_mem_size_ = 0;
}

int ObRowStore::BlockList::add_last(BlockInfo *block)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(block)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid argument", K(ret), K(block));
  } else {
    block->next_ = NULL;
    block->prev_ = last_;
    if (OB_ISNULL(last_)) {
      if (OB_ISNULL(first_)) {
        first_ = block;
        last_ = block;
      } else {
        ret = OB_ERR_UNEXPECTED;
        OB_LOG(WARN, "invalid block list", K(ret), K_(last), K_(first));
      }
    } else {
      last_->next_ = block;
      last_ = block;
    }
  }
  if (OB_SUCC(ret)) {
    used_mem_size_ += block->get_block_size();
    ++count_;
  }
  return ret;
}

ObRowStore::BlockInfo *ObRowStore::BlockList::pop_last()
{
  BlockInfo *block = last_;
  if (NULL != block) {
    if (NULL != block->prev_) {
      block->prev_->next_ = NULL;
      last_ = block->prev_;
    } else {
      // the only one
      first_ = NULL;
      last_ = NULL;
    }
    block->next_ = NULL;
    block->prev_ = NULL;
    --count_;
    used_mem_size_ -= block->get_block_size();
  }
  return block;
}

////////////////////////////////////////////////////////////////
ObRowStore::Iterator::Iterator()
  :row_store_(NULL),
   cur_iter_block_(NULL),
   cur_iter_pos_(0),
   decode_owner_(nullptr), decode_values_(nullptr), decode_capacity_(0)
{
}

ObRowStore::Iterator::Iterator(const ObRowStore *row_store)
    : row_store_(row_store),
      cur_iter_block_(NULL),
      cur_iter_pos_(0),
      decode_owner_(row_store == nullptr ? nullptr : &row_store->alloc_),
      decode_values_(nullptr), decode_capacity_(0)
{
}


ObRowStore::Iterator::Iterator(const Iterator &other)
    : row_store_(other.row_store_), cur_iter_block_(other.cur_iter_block_),
      cur_iter_pos_(other.cur_iter_pos_), decode_owner_(other.decode_owner_),
      decode_values_(nullptr), decode_capacity_(0)
{}
ObRowStore::Iterator &ObRowStore::Iterator::operator=(const Iterator &other)
{
  if (this != &other) {
    release_decode_slots(); row_store_ = other.row_store_;
    cur_iter_block_ = other.cur_iter_block_; cur_iter_pos_ = other.cur_iter_pos_;
    decode_owner_ = other.decode_owner_;
  }
  return *this;
}
ObRowStore::Iterator::~Iterator() { release_decode_slots(); }
void ObRowStore::Iterator::release_decode_slots()
{
  if (decode_values_ != nullptr) { decode_owner_->free(decode_values_); }
  decode_values_ = nullptr; decode_capacity_ = 0;
}
int ObRowStore::Iterator::reserve_decode_slots(int64_t count)
{
  int ret = OB_SUCCESS;
  if (decode_owner_ == nullptr) { ret = OB_NOT_INIT; }
  else if (count > decode_capacity_) {
    DatumValue *candidate = nullptr;
    if (OB_FAIL(DatumValueBuffer::allocate(*decode_owner_, count, candidate))) {}
    else { release_decode_slots(); decode_values_ = candidate; decode_capacity_ = count; }
  }
  return ret;
}

//Why not reset row_store_, which does not satisfy the semantics of reset, and renamed to reuse?
void ObRowStore::Iterator::reset()
{
  // don't reset row_store_
  cur_iter_block_ = NULL;
  cur_iter_pos_ = 0;
}


int ObRowStore::Iterator::get_next_stored_row(StoredRow *&stored_row)
{
  int ret = OB_SUCCESS;
  stored_row = NULL;
  if (NULL == row_store_) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    if (NULL == cur_iter_block_) {
      cur_iter_block_ = row_store_->blocks_.get_first();
      cur_iter_pos_ = 0;
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(next_iter_pos(cur_iter_block_, cur_iter_pos_))) {
    if (OB_ITER_END != ret) {
      OB_LOG(WARN, "failed to get next pos", K(ret));
    }
  } else if (OB_ISNULL(cur_iter_block_)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "invalid cur iter block", K(ret), K(cur_iter_block_));
  } else {
    stored_row = const_cast<StoredRow*>(reinterpret_cast<const StoredRow *>
                                          (cur_iter_block_->get_buffer_head() + cur_iter_pos_));
    // next
    cur_iter_pos_ += (row_store_->get_reserved_cells_size(stored_row->reserved_cells_count_)
                      + stored_row->compact_row_size_);
  } //end else
  return ret;
}

int ObRowStore::Iterator::get_next_row(ObNewRow &row)
{
  return get_next_row(row, NULL, NULL);
}

int ObRowStore::Iterator::get_next_row(ObNewRow &row,
                                       common::ObString *compact_row,
                                       StoredRow **r_stored_row)
{
  int ret = OB_SUCCESS;
  StoredRow *stored_row = NULL;
  if (OB_ISNULL(row_store_)) {
    OB_LOG(WARN, "row_store_ is NULL, should not reach here");
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(row.count_ < row_store_->get_col_count())) {
    ret = OB_BUF_NOT_ENOUGH;
    OB_LOG(WARN, "column buffer count is not enough", K_(row.count),
               K(row_store_->get_col_count()));
  } else if (OB_FAIL(reserve_decode_slots(row_store_->get_col_count()))) {
  } else if (OB_FAIL(get_next_stored_row(stored_row))) {
    // do nothing
  } else if (OB_ISNULL(stored_row)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "stored_row should not be NULL", K(ret));
  } else if (OB_FAIL(ObRowUtil::convert(stored_row->get_compact_row(), row, decode_values_, decode_capacity_))) {
  } else {
    if (NULL != r_stored_row) {
      *r_stored_row = stored_row;
    }
    if (NULL != compact_row) {
      *compact_row = stored_row->get_compact_row();
    }
  }
  return ret;
}

int ObRowStore::Iterator::get_next_row(ObNewRow *&row, StoredRow **r_stored_row)
{
  int ret = OB_SUCCESS;
  StoredRow *stored_row = NULL;
  if (OB_ISNULL(row_store_)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(ERROR, "row_store_ is NULL", K(ret));
  } else if (row_store_->use_compact_row()) {
    ret = OB_NOT_SUPPORTED;
    OB_LOG(WARN, "this row store uses compact_row and you should not use this iterator", K(ret));
  } else if (OB_FAIL(get_next_stored_row(stored_row))) {
    // do nothing
  } else if (OB_ISNULL(stored_row)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "stored_row should not be NULL", K(ret));
  } else {
    row = reinterpret_cast<ObNewRow*>(stored_row->get_compact_row_ptr());
    if (NULL != r_stored_row) {
      *r_stored_row = stored_row;
    }
  }
  return ret;
}

int ObRowStore::Iterator::next_iter_pos(const BlockInfo *&iter_block, int64_t &iter_pos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(iter_block)) {
    ret = OB_ITER_END;
  } else if (0 >= iter_block->get_remain_size_for_read(iter_pos)) {
    if (OB_ISNULL(iter_block->get_next_block())) {
      // the last block
      ret = OB_ITER_END;
    } else {
      // next block
      iter_block = iter_block->get_next_block();
      iter_pos = 0;
    }
  } else {}
  return ret;
}


////////////////////////////////////////////////////////////////
ObRowStore::ObRowStore(ObIAllocator &alloc,
                       const lib::ObLabel &label,
                       bool use_compact_row /*= true*/)
:   inner_alloc_(),
    reserved_columns_(alloc),
    blocks_(),
    block_size_(NORMAL_BLOCK_SIZE),
    data_size_(0),
    row_count_(0),
    col_count_(0),
    last_last_row_size_(-1),
    last_row_size_(-1),
    label_(label),
    ctx_id_(ObCtxIds::DEFAULT_CTX_ID),
    is_read_only_(false),
    has_big_block_(false),
    use_compact_row_(use_compact_row),
    alloc_(alloc),
    pre_project_buf_(NULL),
    pre_alloc_block_(false)
{
  inner_alloc_.set_label(label);
}
ObRowStore::ObRowStore(const lib::ObLabel &label,
                       bool use_compact_row /*= true*/)
:   inner_alloc_(),
    reserved_columns_(inner_alloc_),
    blocks_(),
    block_size_(NORMAL_BLOCK_SIZE),
    data_size_(0),
    row_count_(0),
    col_count_(0),
    last_last_row_size_(-1),
    last_row_size_(-1),
    label_(label),
    ctx_id_(ObCtxIds::DEFAULT_CTX_ID),
    is_read_only_(false),
    has_big_block_(false),
    use_compact_row_(use_compact_row),
    alloc_(inner_alloc_),
    pre_project_buf_(NULL),
    pre_alloc_block_(false)
{
  inner_alloc_.set_label(label);
}


ObRowStore::~ObRowStore()
{
  clear_rows();
}


void ObRowStore::reset()
{
  clear_rows();
  reserved_columns_.reset();
  pre_alloc_block_ = false;
}

// method for ObAggregateFunction::prepare()
// prepare need to reuse ObRowStore for WRITE,
// it needs to reuse reserved_columns_ which should not be cleared
void ObRowStore::clear_rows()
{
  data_size_ = 0;
  row_count_ = 0;
  col_count_ = 0;
  last_last_row_size_ = -1;
  last_row_size_ = -1;
  is_read_only_ = false;
  has_big_block_ = false;

  // free all blocks
  BlockInfo *block = blocks_.get_first();
  while (NULL != block) {
    BlockInfo *next = block->get_next_block();
    block->~BlockInfo();
    alloc_.free(block);
    block = next;
  }
  blocks_.reset();
}

/*
 * Use with caution, improper use can lead to memory leaks,
 * Decide whether to use this function based on memory management with alloc_ and your own usage scenario,
 * This function retains the most recently allocated block and deletes other blocks, similar to reuse
*/


ObRowStore::BlockInfo* ObRowStore::new_block(int64_t block_size)
{
  BlockInfo *block = NULL;
  // normalize block size
  if (block_size > BIG_BLOCK_SIZE) {
    block_size = OB_MAX_ROW_LENGTH_IN_MEMTABLE;
  } else if (block_size > NORMAL_BLOCK_SIZE) {
    block_size = BIG_BLOCK_SIZE;
  } else {
    block_size = NORMAL_BLOCK_SIZE;
  }
  // Make sure all memory is allocated under the active runtime context.
  ObMemAttr attr(label_, ctx_id_);
  block = static_cast<BlockInfo *>(alloc_.alloc(block_size, attr));
  if (OB_ISNULL(block)) {
    OB_LOG_RET(WARN, common::OB_ERR_UNEXPECTED, "no memory");
  } else {
    block = new(block) BlockInfo(block_size);
    if (!has_big_block_ && block_size == BIG_BLOCK_SIZE) {
      has_big_block_ = true;
    }
  }
  return block;
}

// If there are already big blocks, then all new blocks are big.
ObRowStore::BlockInfo *ObRowStore::new_block()
{
  BlockInfo *blk = NULL;
  if (OB_LIKELY(!has_big_block_)) { // use config BLOCK size
    if (OB_ISNULL(blk = new_block(block_size_))) {
      OB_LOG_RET(WARN, common::OB_ERR_UNEXPECTED, "failed to new block", K_(block_size));
    }
  } else { // use BIG_BLOCK
    if (OB_ISNULL(blk = new_block(BIG_BLOCK_SIZE))) {
      OB_LOG_RET(WARN, common::OB_ERR_UNEXPECTED, "failed to new big block", K(blk));
    }
  }
  return blk;
}

int ObRowStore::add_row(const ObNewRow &row,
                        const StoredRow *&stored_row,
                        int64_t payload,
                        bool by_projector)
{
  int ret = OB_SUCCESS;
  if (!by_projector) {
    //Remove row's projector
    ObNewRow tmp_row;
    tmp_row.cells_ = row.cells_;
    tmp_row.count_ = row.count_;
    ret = add_row_by_projector(tmp_row, stored_row, payload);
  } else {
    ret = add_row_by_projector(row, stored_row, payload);
  }
  return ret;
}

int ObRowStore::add_row(const ObNewRow &row, bool by_projector)
{
  const StoredRow *stored_row = NULL; // value ignored
  return add_row(row, stored_row, 0, by_projector);
}

int ObRowStore::add_row_by_projector(const ObNewRow &row,
                                     const StoredRow *&stored_row,
                                     int64_t payload)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_read_only_)) {
    ret = OB_ERR_READ_ONLY;
    OB_LOG(WARN, "read only ObRowStore, not allowed to add row", K(ret));
  } else if (OB_UNLIKELY(0 < col_count_) && OB_UNLIKELY(row.get_count() != col_count_)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "all rows should have the same columns", K(col_count_), K(row.get_count()));
  } else {
    if (!use_compact_row_) {
      const int64_t flat_size = row.get_projected_deep_copy_size();
      const int64_t header_size = get_reserved_cells_size(reserved_columns_.count());
      if (flat_size == INT64_MAX || header_size > INT64_MAX - sizeof(ObNewRow)
          || flat_size > INT64_MAX - sizeof(ObNewRow) - header_size) {
        return OB_SIZE_OVERFLOW;
      }
    }
    stored_row = NULL;
    const int32_t reserved_columns_count = static_cast<int32_t>(reserved_columns_.count());
    int64_t min_need_size = get_compact_row_min_size(row.get_count()) +
        get_reserved_cells_size(reserved_columns_count);
    BlockInfo *block = NULL;
    if (OB_ISNULL(blocks_.get_last()) || blocks_.get_last()->get_remain_size() <= min_need_size) {
      if (pre_alloc_block_) {
        ret = OB_SIZE_OVERFLOW;
      } else if (OB_ISNULL(block = new_block())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        OB_LOG(ERROR, "failed to new block", K(ret));
      }
    } else {
      block = blocks_.get_last();
    }
    if (OB_SUCC(ret) && OB_ISNULL(block)) {
      ret = OB_ERR_UNEXPECTED;
      OB_LOG(WARN, "fail to get valid block", K(ret), K(block));
    }
    // try three times:
    // first, try to append into the current block
    // if failed, then try to append into a new NORMAL block
    // if still failed, then append into a new BIG block
    // if the row is larger than the BIG block, it should fail anyway
    int64_t retry = 0;
    StoredRow *stored = NULL;
    while (OB_SUCC(ret) && retry <= 3) {
      if (use_compact_row_) {
        ret = block->append_row(reserved_columns_, row, payload, stored);
      } else {
        if (NULL != row.projector_) {
          if (OB_UNLIKELY(NULL == pre_project_buf_)) {
            if (OB_FAIL(init_pre_project_row(row.get_count()))) {
            }
          }
          if (OB_SUCC(ret)) {
            if (OB_FAIL(pre_project(row))) {
            } else {
              ret = block->append_row_by_copy(reserved_columns_, pre_project_row_, payload, stored);
            }
          }
        } else {
          ret = block->append_row_by_copy(reserved_columns_, row, payload, stored);
        }
      }
      if (OB_FAIL(ret)) {
        if (OB_BUF_NOT_ENOUGH == ret) {
          if (pre_alloc_block_) {
            ret = OB_SIZE_OVERFLOW;
          } else {
            ret = OB_SUCCESS;
            ++retry;
            if (block->is_empty()) {
              // a fresh block is not large enough, free it and new a BIG one
              int64_t big_block_size = block->get_block_size() >= BIG_BLOCK_SIZE ? OB_MAX_ROW_LENGTH_IN_MEMTABLE : BIG_BLOCK_SIZE;
              if (block == blocks_.get_last()) { // to be robust
                blocks_.pop_last();
              }
              alloc_.free(block);
              if (OB_ISNULL(block = new_block(big_block_size))) {
                ret = OB_ALLOCATE_MEMORY_FAILED;
                OB_LOG(ERROR, "failed to new block", K(ret));
              }
            } else if (OB_ISNULL(block = new_block())) {  // normal case
              ret = OB_ALLOCATE_MEMORY_FAILED;
              OB_LOG(ERROR, "failed to new block", K(ret));
            }
          }
        } else {
          OB_LOG(WARN, "failed to append row", K(ret));
        }
      } else if (OB_ISNULL(stored)) {
        ret = OB_ERR_UNEXPECTED;
        OB_LOG(WARN, "get invalid stored row", K(ret), K(stored));
      } else {
        if (block != blocks_.get_last()) {
          // new block
          ret = blocks_.add_last(block);
        }
        stored_row = stored;
        ++row_count_;
        last_last_row_size_ = last_row_size_;
        last_row_size_ = stored_row->compact_row_size_ +
            get_reserved_cells_size(stored_row->reserved_cells_count_);
        data_size_ += last_row_size_;
        if (0 >= col_count_) {
          col_count_ = row.get_count();
        }
        break;                  // done
      }
    } // end while
    if (OB_SUCC(ret) && 3 < retry) {
      ret = OB_ERR_UNEXPECTED;
      OB_LOG(ERROR, "unexpected branch");
    }
  }
  return ret;
}

int ObRowStore::init_pre_project_row(int64_t count)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == pre_project_buf_)) {
    if (OB_UNLIKELY(count <= 0)) {
      ret = OB_INVALID_ARGUMENT;
      OB_LOG(WARN, "invalid count", K(count), K(ret));
    } else {
      ObMemAttr attr(label_, ctx_id_);
      pre_project_buf_ = static_cast<DatumValue *>(
          alloc_.alloc(count * sizeof(DatumValue), attr));
      if (OB_UNLIKELY(NULL == pre_project_buf_)) {
        ret = OB_ERR_UNEXPECTED;
        OB_LOG(WARN, "invalid count", K(count), K(ret));
      } else {
        new (pre_project_buf_) DatumValue[count];
        pre_project_row_.assign(pre_project_buf_, count);
      }
    }
  }
  return ret;
}

int ObRowStore::pre_project(const ObNewRow &row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(row.is_invalid())) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid row", K(row), K(ret));
  } else {
    int64_t count = row.get_count();
    for (int64_t i = 0; i < count; ++i) {
      pre_project_buf_[i] = row.get_cell(i);
    }
  }
  return ret;
}

int ObRowStore::rollback_last_row()
{
  int ret = OB_SUCCESS;
  // only support add_row->rollback->add_row->rollback->add_row->add_row->rollback
  // NOT support  ..->rollback->rollback->...
  BlockInfo *last = blocks_.get_last();
  if (0 >= last_row_size_
      || (0 < last_row_size_ && 0 >= last_last_row_size_ && 1 < row_count_)/*already rollbacked once*/) {
    ret = OB_NOT_SUPPORTED;
    OB_LOG(WARN, "only one row could be rollback after called add_row() once");
  } else if (OB_ISNULL(last)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "last blockinfo is null ,can't rollback", K(ret), K(last));
  } else {
    last->advance(-last_row_size_);
    if (OB_UNLIKELY(0 > last->get_curr_data_pos())) {
      ret = OB_ERR_SYS;
      OB_LOG(ERROR, "out of memory range, ",
          K(ret), "block_size", last->get_block_size(), "curr_data_pos", last->get_curr_data_pos(),
          K_(last_row_size));
    } else {
      --row_count_;
      if (last->is_empty()) {
        blocks_.pop_last();
        alloc_.free(last);
        last = NULL;
      }
      last_row_size_ = last_last_row_size_;
      last_last_row_size_ = -1;
    }
  }
  return ret;
}


ObRowStore::Iterator ObRowStore::begin() const
{
  return Iterator(this);
}

void ObRowStore::dump() const
{
  int ret = OB_SUCCESS;
  StoredRow *stored_row = NULL;
  ObNewRow row;
  static const int64_t INLINE_OBJS_SIZE = 16;
  DatumValue inline_objs[INLINE_OBJS_SIZE];
  row.cells_ = inline_objs;
  char *buf = NULL;
  if (INLINE_OBJS_SIZE < col_count_) {
    ObMemAttr attr(label_);
    buf = static_cast<char *>(ob_malloc(sizeof(DatumValue) * col_count_, attr));
    if (OB_ISNULL(buf)) {
      OB_LOG(WARN, "failed to alloc memory for objs");
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      row.cells_ = new(buf) DatumValue[col_count_];
    }
  }
  if (OB_SUCC(ret)) {
    row.count_ = static_cast<int32_t>(col_count_);
    Iterator it = begin();
    int64_t row_count = 1;
    while (OB_SUCCESS == (ret = it.get_next_row(row, NULL, &stored_row))) {
      ++row_count;
    }
  }
  if (NULL != buf) {
    ob_free(buf);
  }
}


// @todo improve performance, copy by block
int ObRowStore::assign(const ObRowStore &other_store)
{
  int ret = OB_SUCCESS;
  ObRowStore::Iterator row_store_it = other_store.begin();
  ObNewRow cur_row;
  int64_t col_count = other_store.get_col_count();
  DatumValue *cell = NULL;
  clear_rows();
  
  use_compact_row_ = other_store.use_compact_row_;

  if (OB_FAIL(set_col_count(col_count))) {
  } else if (OB_ISNULL(cell = static_cast<DatumValue *>(alloca(sizeof(DatumValue) * col_count)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    OB_LOG(WARN, "fail to alloc obj array", K(ret));
  } else {
    cur_row.cells_ = new (cell) DatumValue[col_count];
    cur_row.count_ = col_count;
    while (true) {
      if (OB_FAIL(row_store_it.get_next_row(cur_row))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        } else {
          OB_LOG(WARN, "fail to get next row", K(ret));
        }
        break;
      } else if (OB_FAIL(add_row(cur_row))) {
        OB_LOG(WARN, "fail to add row", K(ret));
        break;
      }
    }
  }
  return ret;
}

namespace {
// V2 logical rows use the explicit physical record, never C++ images.
constexpr int64_t ROW_STORE_LOGICAL_V2 = -2;
class RowWireWriter
{
public:
  RowWireWriter(char *buffer, int64_t length, int64_t &pos, bool sizing)
      : buffer_(buffer), length_(length), pos_(pos), sizing_(sizing), ret_(OB_SUCCESS) {}
  template <typename T> void field(const T &value)
  {
    if (ret_ == OB_SUCCESS) {
      if (sizing_) { add(serialization::encoded_length(value)); }
      else { ret_ = serialization::encode(buffer_, length_, pos_, value); }
    }
  }
  void value(const DatumValue &value)
  {
    if (ret_ == OB_SUCCESS) {
      if (sizing_) {
        int64_t bytes = 0;
        if (OB_SUCCESS == (ret_ = NativeValueFlat::row_value_size(value, bytes))) { add(bytes); }
      } else {
        ret_ = NativeValueFlat::encode_row_value(value, buffer_, length_, pos_);
      }
    }
  }
  int result() const { return ret_; }
private:
  void add(int64_t bytes)
  {
    if (bytes < 0 || pos_ < 0 || bytes > INT64_MAX - pos_) { ret_ = OB_SIZE_OVERFLOW; }
    else { pos_ += bytes; }
  }
  char *buffer_; int64_t length_; int64_t &pos_; bool sizing_; int ret_;
};

template <typename T> int read_row_field(const char *buf, int64_t length, int64_t &pos, T &value)
{
  return buf == nullptr || pos < 0 || pos >= length
      ? OB_DESERIALIZE_ERROR : serialization::decode(buf, length, pos, value);
}

int read_row_field(const char *buf, int64_t length, int64_t &pos, int64_t &output)
{
  if (buf == nullptr) { return OB_INVALID_ARGUMENT; }
  uint64_t bits = 0;
  int64_t next = pos;
  for (uint32_t i = 0; i < 10; ++i) {
    if (next < 0 || next >= length) { return OB_DESERIALIZE_ERROR; }
    const uint8_t byte = static_cast<uint8_t>(buf[next++]);
    if (i == 9 && (byte & 0xfe) != 0) { return OB_DESERIALIZE_ERROR; }
    bits |= uint64_t(byte & 0x7f) << (7 * i);
    if ((byte & 0x80) == 0) { MEMCPY(&output, &bits, sizeof(output)); pos = next; return OB_SUCCESS; }
  }
  return OB_DESERIALIZE_ERROR;
}

bool row_range(int64_t length, int64_t start, int64_t bytes)
{ return start >= 0 && start <= length && bytes >= 0 && bytes <= length - start; }

template <typename T> void image_field(const char *image, int64_t offset, T &value)
{ MEMCPY(&value, image + offset, sizeof(value)); }

int allocate_row_values(ObIAllocator &owner, int64_t count, DatumValue *&values)
{
  if (count < 0 || count > INT64_MAX / sizeof(DatumValue)) { return OB_SIZE_OVERFLOW; }
  if (count == 0) { values = nullptr; return OB_SUCCESS; }
  void *memory = owner.alloc(count * sizeof(DatumValue));
  if (memory == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
  values = new (memory) DatumValue[count];
  return OB_SUCCESS;
}

int read_compact_row(const char *bytes, int64_t length, int64_t count,
                     ObIAllocator &owner, ObNewRow &row)
{
  int ret = OB_SUCCESS;
  DatumValue *values = nullptr;
  ObCellReader reader;
  if (OB_FAIL(allocate_row_values(owner, count, values))) {}
  else if (OB_FAIL(reader.init(bytes, length, DENSE))) {}
  else {
    int64_t index = 0;
    bool finished = false;
    while (OB_SUCC(ret) && !finished) {
      DatumValue value;
      uint64_t column_id = OB_INVALID_ID;
      if (OB_FAIL(reader.next_cell())) {}
      else if (OB_FAIL(reader.get_cell(column_id, value, &finished))) {}
      else if (!finished) {
        if (index >= count) { ret = OB_DESERIALIZE_ERROR; }
        else if (value.datum_.has_ext_value()) {
          // The reader reuses its marker wrapper on next_cell(). Copy only
          // that carrier span into the caller's owner before advancing it.
          if (OB_FAIL(values[index].deep_copy(value, owner))) {}
          else { ++index; }
        } else { values[index++] = value; }
      }
    }
    if (OB_SUCC(ret) && index != count) { ret = OB_DESERIALIZE_ERROR; }
    if (OB_SUCC(ret)) { row = ObNewRow(values, count); }
  }
  return ret;
}
} // namespace

int ObRowStore::write_wire_body(char *buf, int64_t length, int64_t &pos, bool sizing) const
{
  int ret = OB_SUCCESS;
  if (row_count_ < 0 || col_count_ < 0 || (row_count_ > 0 && col_count_ == 0)) { return OB_INVALID_DATA; }
  RowWireWriter writer(buf, length, pos, sizing);
  writer.field(block_size_);
  writer.field(row_count_);
  writer.field(col_count_);
  writer.field(is_read_only_);
  writer.field(use_compact_row_);
  ObArenaAllocator temporary("RowWireRead");
  int64_t rows = 0;
  for (const BlockInfo *block = blocks_.get_first(); OB_SUCC(ret) && block != nullptr;
       block = block->get_next_block()) {
    const char *data = block->get_buffer_head();
    const int64_t size = block->get_curr_data_pos();
    int64_t cursor = 0;
    while (OB_SUCC(ret) && cursor < size) {
      if (!row_range(size, cursor, sizeof(StoredRow))) { ret = OB_INVALID_DATA; break; }
      const auto *stored = reinterpret_cast<const StoredRow *>(data + cursor);
      const int64_t reserved_count = stored->reserved_cells_count_;
      const int64_t reserve_bytes = DatumValueBuffer::allocation_size(reserved_count);
      if (reserved_count < 0 || reserve_bytes == INT64_MAX
          || !row_range(size, cursor + sizeof(StoredRow), reserve_bytes)) { ret = OB_INVALID_DATA; break; }
      const int64_t header = sizeof(StoredRow) + reserve_bytes;
      if (!row_range(size, cursor + header, stored->compact_row_size_)) { ret = OB_INVALID_DATA; break; }
      const char *body = data + cursor + header;
      ObNewRow row;
      if (use_compact_row_) {
        ret = read_compact_row(body, stored->compact_row_size_, col_count_, temporary, row);
      } else if (stored->compact_row_size_ < sizeof(ObNewRow)) {
        ret = OB_INVALID_DATA;
      } else {
        row = *reinterpret_cast<const ObNewRow *>(body);
        if (row.count_ != col_count_ || row.projector_size_ != 0
            || (row.count_ > 0 && row.cells_ == nullptr)) { ret = OB_INVALID_DATA; }
      }
      if (OB_SUCC(ret)) {
        writer.field(stored->payload_);
        writer.field(reserved_count);
        for (int64_t i = 0; i < reserved_count && writer.result() == OB_SUCCESS; ++i) {
          writer.value(stored->reserved_cells_[i]);
        }
        writer.field(row.count_);
        for (int64_t i = 0; i < row.count_ && writer.result() == OB_SUCCESS; ++i) { writer.value(row.cells_[i]); }
        ret = writer.result();
        ++rows;
        cursor += header + stored->compact_row_size_;
      }
      temporary.reuse();
    }
  }
  if (OB_SUCC(ret)) { ret = writer.result(); }
  if (OB_SUCC(ret) && rows != row_count_) { ret = OB_INVALID_DATA; }
  return ret;
}

// Rebuild one decoded record with final-owner bytes. Reserved reference spans
// live after the logical row in this same record; compact readers stop at END.
int ObRowStore::append_wire_row(const ObNewRow &row, const DatumValue *reserved,
                                int64_t reserved_count, int64_t payload)
{
  int ret = OB_SUCCESS;
  if (row.count_ <= 0 || row.cells_ == nullptr || row.projector_size_ != 0
      || reserved_count < 0 || reserved_count > INT32_MAX
      || (reserved_count > 0 && reserved == nullptr)) { return OB_INVALID_ARGUMENT; }
  const int64_t header = get_reserved_cells_size(reserved_count);
  if (header < 0 || header >= OB_MAX_ROW_LENGTH_IN_MEMTABLE) { return OB_SIZE_OVERFLOW; }
  BlockInfo *block = nullptr;
  for (int64_t attempt = 0; attempt < 4; ++attempt) {
    const bool reuse_last = attempt == 0 && blocks_.get_last() != nullptr;
    const int64_t request = attempt <= 1 ? block_size_
        : attempt == 2 ? BIG_BLOCK_SIZE : OB_MAX_ROW_LENGTH_IN_MEMTABLE;
    block = reuse_last ? blocks_.get_last() : new_block(request);
    if (block == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
    char *base = block->get_buffer();
    const int64_t capacity = block->get_remain_size();
    int64_t pos = header;
    StoredRow *stored = nullptr;
    if (pos > capacity) { ret = OB_BUF_NOT_ENOUGH; }
    else {
      stored = new (base) StoredRow();
      stored->reserved_cells_count_ = static_cast<int32_t>(reserved_count);
      stored->payload_ = payload;
      DatumValue *values = nullptr;
      DatumValueBuffer::initialize(stored->reserved_cells_, reserved_count, values);
      if (use_compact_row_) {
        ObCellWriter writer;
        if (OB_FAIL(writer.init(base + pos, capacity - pos, DENSE))) {}
        for (int64_t i = 0; OB_SUCC(ret) && i < row.count_; ++i) { ret = writer.append(row.cells_[i]); }
        if (OB_SUCC(ret)) { ret = writer.row_finish(); }
        if (OB_SUCC(ret)) { pos += writer.size(); }
      } else if (capacity - pos < sizeof(ObNewRow)) {
        ret = OB_BUF_NOT_ENOUGH;
      } else {
        auto *copy = new (base + pos) ObNewRow();
        pos += sizeof(ObNewRow);
        ret = copy->deep_copy(row, base, capacity, pos);
      }
      if (OB_SUCC(ret)) {
        const int64_t aligned = NativeValueFlat::span(pos);
        if (aligned > capacity) { ret = OB_BUF_NOT_ENOUGH; }
        else { MEMSET(base + pos, 0, aligned - pos); pos = aligned; }
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < reserved_count; ++i) {
        ret = NativeValueFlat::copy(reserved[i], values[i], base, capacity, pos);
      }
    }
    if (OB_SUCC(ret)) {
      stored->compact_row_size_ = static_cast<int32_t>(pos - header);
      block->advance(pos);
      if (!reuse_last) { ret = blocks_.add_last(block); }
      if (OB_SUCC(ret)) {
        ++row_count_; col_count_ = row.count_;
        last_last_row_size_ = last_row_size_; last_row_size_ = pos; data_size_ += pos;
        return OB_SUCCESS;
      }
    }
    if (!reuse_last) { block->~BlockInfo(); alloc_.free(block); }
    block = nullptr;
    if (ret != OB_BUF_NOT_ENOUGH && ret != OB_SIZE_OVERFLOW && ret != OB_ALLOCATE_MEMORY_FAILED) { break; }
  }
  return ret;
}

int ObRowStore::read_wire_v2(const char *buf, int64_t length, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t expected_rows = 0, expected_columns = 0;
  bool read_only = false, compact = true;
  if (OB_FAIL(read_row_field(buf, length, pos, block_size_))) {}
  else if (OB_FAIL(read_row_field(buf, length, pos, expected_rows))) {}
  else if (OB_FAIL(read_row_field(buf, length, pos, expected_columns))) {}
  else if (OB_FAIL(read_row_field(buf, length, pos, read_only))) {}
  else if (OB_FAIL(read_row_field(buf, length, pos, compact))) {}
  else if (block_size_ <= 0 || block_size_ > OB_MAX_ROW_LENGTH_IN_MEMTABLE
      || expected_rows < 0 || expected_rows > length - pos || expected_columns < 0
      || (expected_rows > 0 && expected_columns == 0)) { ret = OB_DESERIALIZE_ERROR; }
  if (OB_SUCC(ret)) { use_compact_row_ = compact; col_count_ = expected_columns; }
  ObArenaAllocator temporary("RowWireDecode");
  for (int64_t index = 0; OB_SUCC(ret) && index < expected_rows; ++index) {
    int64_t payload = 0, reserved_count = 0, count = 0;
    DatumValue *reserved = nullptr, *values = nullptr;
    if (OB_FAIL(read_row_field(buf, length, pos, payload))) {}
    else if (OB_FAIL(read_row_field(buf, length, pos, reserved_count))) {}
    else if (reserved_count < 0 || reserved_count > length - pos || reserved_count > INT32_MAX) { ret = OB_DESERIALIZE_ERROR; }
    else if (OB_FAIL(allocate_row_values(temporary, reserved_count, reserved))) {}
    for (int64_t i = 0; OB_SUCC(ret) && i < reserved_count; ++i) {
      ret = NativeValueFlat::decode_row_value(buf, length, pos, temporary, reserved[i]);
    }
    if (OB_SUCC(ret) && OB_FAIL(read_row_field(buf, length, pos, count))) {}
    else if (OB_SUCC(ret) && (count != expected_columns || count > length - pos)) { ret = OB_DESERIALIZE_ERROR; }
    else if (OB_SUCC(ret) && OB_FAIL(allocate_row_values(temporary, count, values))) {}
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      ret = NativeValueFlat::decode_row_value(buf, length, pos, temporary, values[i]);
    }
    if (OB_SUCC(ret)) { ret = append_wire_row(ObNewRow(values, count), reserved, reserved_count, payload); }
    temporary.reuse();
  }
  if (OB_SUCC(ret)) { is_read_only_ = read_only; }
  return ret;
}

int ObRowStore::read_wire_1d6(const char *buf, int64_t length, int64_t &pos, int64_t block_count)
{
  int ret = OB_SUCCESS;
  const int64_t blocks_start = pos;
  if (block_count < 0 || block_count > length - pos) { return OB_DESERIALIZE_ERROR; }
  // Locate the old footer before interpreting a block: compact mode lives there.
  for (int64_t i = 0; OB_SUCC(ret) && i < block_count; ++i) {
    int64_t bytes = 0;
    if (OB_FAIL(read_row_field(buf, length, pos, bytes))) {}
    else if (!row_range(length, pos, bytes)) { ret = OB_DESERIALIZE_ERROR; }
    else { pos += bytes; }
  }
  int64_t expected_rows = -1, columns = 0, old_last_size = -1;
  bool read_only = true;
  if (OB_SUCC(ret) && pos < length) { ret = read_row_field(buf, length, pos, block_size_); }
  if (OB_SUCC(ret) && pos < length) { ret = read_row_field(buf, length, pos, expected_rows); }
  if (OB_SUCC(ret) && pos < length) { ret = read_row_field(buf, length, pos, columns); }
  if (OB_SUCC(ret) && pos < length) { ret = read_row_field(buf, length, pos, read_only); }
  if (OB_SUCC(ret) && pos < length) { ret = read_row_field(buf, length, pos, old_last_size); }
  if (OB_SUCC(ret) && pos < length) { ret = read_row_field(buf, length, pos, use_compact_row_); }
  if (OB_SUCC(ret) && (block_size_ <= 0 || block_size_ > OB_MAX_ROW_LENGTH_IN_MEMTABLE
      || columns < 0 || expected_rows < -1)) { ret = OB_DESERIALIZE_ERROR; }
  const int64_t end = pos;
  int64_t input = blocks_start;
  ObArenaAllocator temporary("RowOldImage");
  for (int64_t block = 0; OB_SUCC(ret) && block < block_count; ++block) {
    int64_t bytes = 0;
    if (OB_FAIL(read_row_field(buf, end, input, bytes))) { break; }
    const char *image = buf + input;
    int64_t cursor = 0;
    while (OB_SUCC(ret) && cursor < bytes) {
      int32_t body_size = 0, reserved_count = 0;
      int64_t payload = 0;
      if (!row_range(bytes, cursor, 16)) { ret = OB_DESERIALIZE_ERROR; break; }
      image_field(image, cursor, body_size);
      image_field(image, cursor + 4, reserved_count);
      image_field(image, cursor + 8, payload);
      if (reserved_count < 0 || !row_range(bytes, cursor + 16, int64_t(reserved_count) * 32)) {
        ret = OB_DESERIALIZE_ERROR; break;
      }
      const int64_t body = cursor + 16 + int64_t(reserved_count) * 32;
      if (!row_range(bytes, body, body_size)) { ret = OB_DESERIALIZE_ERROR; break; }
      DatumValue *reserved = nullptr;
      ObNewRow row;
      if (OB_FAIL(allocate_row_values(temporary, reserved_count, reserved))) {}
      for (int64_t i = 0; OB_SUCC(ret) && i < reserved_count; ++i) {
        ret = NativeValueFlat::read_legacy_1d6_value(image, bytes, cursor + 16 + i * 16, temporary, reserved[i]);
      }
      if (OB_SUCC(ret) && use_compact_row_) {
        if (columns <= 0) { ret = OB_DESERIALIZE_ERROR; }
        else { ret = read_compact_row(image + body, body_size, columns, temporary, row); }
      } else if (OB_SUCC(ret)) {
        int64_t count = 0, projector_count = 0;
        uint64_t cells_offset = 0, projector_offset = 0;
        if (body_size < 32) { ret = OB_DESERIALIZE_ERROR; }
        else {
          image_field(image, body, cells_offset);
          image_field(image, body + 8, count);
          image_field(image, body + 16, projector_count);
          image_field(image, body + 24, projector_offset);
          if (count <= 0 || count > body_size / 16 || projector_count != 0
              || cells_offset != static_cast<uint64_t>(body + 32)
              || !row_range(body + body_size, body + 32, count * 16)
              || (columns > 0 && count != columns)) { ret = OB_DESERIALIZE_ERROR; }
          else if (OB_FAIL(allocate_row_values(temporary, count, row.cells_))) {}
          else {
            row.count_ = count;
            for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
              ret = NativeValueFlat::read_legacy_1d6_value(image, bytes, body + 32 + i * 16, temporary, row.cells_[i]);
            }
          }
        }
        UNUSED(projector_offset);
      }
      if (OB_SUCC(ret)) { ret = append_wire_row(row, reserved, reserved_count, payload); }
      cursor = body + body_size;
      temporary.reuse();
    }
    input += bytes;
  }
  if (OB_SUCC(ret) && expected_rows >= 0 && row_count_ != expected_rows) { ret = OB_DESERIALIZE_ERROR; }
  if (OB_SUCC(ret)) {
    if (row_count_ == 0) { col_count_ = columns; }
    is_read_only_ = read_only; pos = end;
  }
  UNUSED(old_last_size);
  return ret;
}

void ObRowStore::adopt_wire_rows(ObRowStore &source)
{
  reset();
  blocks_ = source.blocks_; source.blocks_.reset();
  block_size_ = source.block_size_; data_size_ = source.data_size_;
  row_count_ = source.row_count_; col_count_ = source.col_count_;
  last_row_size_ = source.last_row_size_; last_last_row_size_ = source.last_last_row_size_;
  is_read_only_ = source.is_read_only_; has_big_block_ = source.has_big_block_;
  use_compact_row_ = source.use_compact_row_;
  source.data_size_ = 0; source.row_count_ = 0; source.col_count_ = 0;
}

DEFINE_SERIALIZE(ObRowStore)
{
  if (buf == nullptr || pos < 0 || pos > buf_len) { return OB_INVALID_ARGUMENT; }
  int64_t body = 0;
  int ret = write_wire_body(nullptr, 0, body, true);
  int64_t next = pos;
  if (OB_SUCC(ret)) { ret = serialization::encode_vi64(buf, buf_len, next, ROW_STORE_LOGICAL_V2); }
  if (OB_SUCC(ret)) { ret = serialization::encode_vi64(buf, buf_len, next, body); }
  if (OB_SUCC(ret) && body > buf_len - next) { ret = OB_SIZE_OVERFLOW; }
  if (OB_SUCC(ret)) { ret = write_wire_body(buf, buf_len, next, false); }
  if (OB_SUCC(ret)) { pos = next; }
  return ret;
}

DEFINE_DESERIALIZE(ObRowStore)
{
  if (buf == nullptr || pos < 0 || pos > data_len) { return OB_INVALID_ARGUMENT; }
  int64_t next = pos, first = 0;
  int ret = read_row_field(buf, data_len, next, first);
  ObRowStore candidate(alloc_, label_, use_compact_row_);
  candidate.ctx_id_ = ctx_id_;
  if (OB_SUCC(ret) && first == ROW_STORE_LOGICAL_V2) {
    int64_t length = 0;
    if (OB_FAIL(read_row_field(buf, data_len, next, length))) {}
    else if (!row_range(data_len, next, length)) { ret = OB_DESERIALIZE_ERROR; }
    else {
      const int64_t end = next + length;
      ret = candidate.read_wire_v2(buf, end, next);
      if (OB_SUCC(ret) && next != end) { ret = OB_DESERIALIZE_ERROR; }
    }
  } else if (OB_SUCC(ret) && first >= 0) {
    ret = candidate.read_wire_1d6(buf, data_len, next, first);
  } else if (OB_SUCC(ret)) { ret = OB_NOT_SUPPORTED; }
  if (OB_SUCC(ret)) { adopt_wire_rows(candidate); pos = next; }
  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(ObRowStore)
{
  int64_t body = 0;
  if (write_wire_body(nullptr, 0, body, true) != OB_SUCCESS) { return INT64_MAX; }
  int64_t total = serialization::encoded_length_vi64(ROW_STORE_LOGICAL_V2);
  if (!NativeValueFlat::add_size(total, serialization::encoded_length_vi64(body))
      || !NativeValueFlat::add_size(total, body)) { return INT64_MAX; }
  return total;
}
} //end common
} //end oceanbase
