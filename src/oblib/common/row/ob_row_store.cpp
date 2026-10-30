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
          stored_row->compact_row_size_ = static_cast<int32_t>(cell_writer.size());
          advance(cell_writer.size());
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
// This is the native same-build memory image, not ordinary row value wire.
// Pointers are rewritten only in the serialization copy; source rows stay live.
int transform_native_block(char *block, int64_t length, bool compact, bool to_offsets,
                             const char *source_block = nullptr)
{
  int ret = OB_SUCCESS; int64_t cursor = 0;
  while (OB_SUCC(ret) && cursor < length) {
    if (length - cursor < sizeof(ObRowStore::StoredRow)) { return OB_INVALID_DATA; }
    auto *stored = reinterpret_cast<ObRowStore::StoredRow *>(block + cursor);
    const int64_t count = stored->reserved_cells_count_;
    if (count < 0 || stored->compact_row_size_ < 0) { return OB_INVALID_DATA; }
    const int64_t values_size = DatumValueBuffer::allocation_size(count);
    if (values_size == INT64_MAX || values_size > length - cursor - sizeof(*stored)) { return OB_SIZE_OVERFLOW; }
    const int64_t header_size = sizeof(*stored) + values_size;
    if (stored->compact_row_size_ > length - cursor - header_size) { return OB_SIZE_OVERFLOW; }
    char *row_bytes = block + cursor + header_size;
    const int64_t row_length = stored->compact_row_size_;
    if (!compact) {
      if (row_length < sizeof(ObNewRow)) { return OB_INVALID_DATA; }
      if (to_offsets) {
        auto *row = reinterpret_cast<ObNewRow *>(row_bytes);
        if (row->count_ < 0 || row->count_ > INT64_MAX / sizeof(DatumValue)
            || row->projector_size_ < 0 || row->projector_size_ > INT64_MAX / sizeof(int32_t)) { return OB_INVALID_DATA; }
        const int64_t descriptor_size = sizeof(DatumValue) * row->count_;
        int64_t pos = cursor + header_size + sizeof(ObNewRow); char *memory = nullptr;
        if (OB_FAIL(NativeValueFlat::take_span(block, length, pos, descriptor_size, memory))) { return ret; }
        auto *values = reinterpret_cast<DatumValue *>(memory);
        row->cells_ = reinterpret_cast<DatumValue *>(memory - block);
        for (int64_t i = 0; OB_SUCC(ret) && i < row->count_; ++i) {
          ret = DatumValueBuffer::make_offsets(values[i], block, length, pos);
        }
        if (OB_SUCC(ret) && row->projector_size_ > 0) {
          ret = NativeValueFlat::take_span(block, length, pos, sizeof(int32_t) * row->projector_size_, memory);
          if (OB_SUCC(ret)) { row->projector_ = reinterpret_cast<int32_t *>(memory - block); }
        } else { row->projector_ = nullptr; }
        if (OB_SUCC(ret) && pos != cursor + header_size + row_length) { ret = OB_INVALID_DATA; }
      } else {
        ObNewRow *row = nullptr; int64_t pos = 0;
        if (OB_FAIL(ObNewRow::construct(row_bytes, row_length, pos, row))) {}
        else if (pos != row_length) { ret = OB_INVALID_DATA; }
      }
    }
    // Reserved values borrow either their fixed inline scalar slot or native
    // compact/row bytes in this same block. Nested arrays are fixed above once.
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      DatumValue &value = stored->reserved_cells_[i];
      if (value.is_null()) { value.datum_.ptr_ = nullptr; }
      else if (to_offsets) {
        const uintptr_t address = reinterpret_cast<uintptr_t>(value.datum_.ptr_);
        const uintptr_t source = reinterpret_cast<uintptr_t>(source_block);
        if (address < source || address - source >= static_cast<uint64_t>(length)
            || value.datum_.len_ > length - static_cast<int64_t>(address - source)) { ret = OB_INVALID_DATA; }
        else { value.datum_.ptr_ = reinterpret_cast<char *>(address - source); }
      } else {
        const uintptr_t offset = reinterpret_cast<uintptr_t>(value.datum_.ptr_);
        if (offset >= static_cast<uint64_t>(length) || value.datum_.len_ > length - static_cast<int64_t>(offset)) { ret = OB_INVALID_DATA; }
        else { value.datum_.ptr_ = block + offset; }
      }
    }
    cursor += header_size + row_length;
  }
  return ret;
}
} // namespace
int ObRowStore::adjust_row_cells_reference()
{
  int ret = OB_SUCCESS;
  for (BlockInfo *block = blocks_.get_first(); OB_SUCC(ret) && block != nullptr; block = block->get_next_block()) {
    ret = transform_native_block(const_cast<char *>(block->get_buffer_head()), block->get_curr_data_pos(), use_compact_row_, false);
  }
  return ret;
}

DEFINE_SERIALIZE(ObRowStore)
{
  int ret = OB_SUCCESS;
  int64_t count = blocks_.get_block_count();
  if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, count))) {
  } else {
    const BlockInfo *block = blocks_.get_first();

    while (NULL != block && OB_SUCC(ret)) {
      // serialize data_buf_size
      if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, block->get_curr_data_pos()))) {
      } else {
        // serialize block data
        if (buf_len - pos < block->get_curr_data_pos()) {
          ret = OB_SIZE_OVERFLOW;
        } else {
          MEMCPY(buf + pos, block->get_buffer_head(), block->get_curr_data_pos());
          ret = transform_native_block(buf + pos, block->get_curr_data_pos(), use_compact_row_, true,
                                         block->get_buffer_head());
          if (OB_SUCC(ret)) { pos += block->get_curr_data_pos(); }
          // serialize next block
          block = block->get_next_block();
        }
      }
    } //end while
  }

  OB_UNIS_ENCODE(block_size_);
  OB_UNIS_ENCODE(row_count_);
  OB_UNIS_ENCODE(col_count_);
  OB_UNIS_ENCODE(is_read_only_);
  OB_UNIS_ENCODE(last_row_size_);
  OB_UNIS_ENCODE(use_compact_row_);
  return ret;
}


DEFINE_DESERIALIZE(ObRowStore)
{
  int ret = OB_SUCCESS;
  int64_t old_pos = pos;
  int64_t block_size = 0;
  int64_t data_buffer_size = 0;
  int64_t block_count = 0;
  BlockInfo *block = NULL;
  if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &block_count))) {
  } else {
    reuse();
    is_read_only_ = true;
    for (int64_t i = 0; i < block_count && OB_SUCC(ret); ++i) {
      if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &data_buffer_size))) {
      } else {
        block_size = data_buffer_size + sizeof(BlockInfo);
        if (OB_ISNULL(block = new_block(block_size))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          OB_LOG(ERROR, "fail to allocate new block", K(ret), K(block_size));
        } else if (block->get_remain_size() >= data_buffer_size) {
          // copy data to store
          MEMCPY(block->get_buffer(), buf + pos, data_buffer_size);
          block->advance(data_buffer_size);
          pos += data_buffer_size;
          data_size_ += data_buffer_size;
          if (OB_FAIL(blocks_.add_last(block))) {
          }
        } else {
          ret = OB_BUF_NOT_ENOUGH;
          OB_LOG(WARN, "block is not enough, ",
              K(ret), "remain_size", block->get_remain_size(), K(data_buffer_size));
        }
      }
    } // end for
  }

  OB_UNIS_DECODE(block_size_);
  OB_UNIS_DECODE(row_count_);
  OB_UNIS_DECODE(col_count_);
  OB_UNIS_DECODE(is_read_only_);
  OB_UNIS_DECODE(last_row_size_);
  OB_UNIS_DECODE(use_compact_row_);
  if (OB_SUCC(ret)) {
    if (OB_FAIL(adjust_row_cells_reference())) {
    }
  }
  if (OB_FAIL(ret)) {
    pos = old_pos;
  }
  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(ObRowStore)
{
  int64_t count = blocks_.get_block_count();
  int64_t len = serialization::encoded_length_vi64(count);
  const BlockInfo *block = blocks_.get_first();
  while (NULL != block) {
    len += serialization::encoded_length_vi64(block->get_curr_data_pos());
    len += block->get_curr_data_pos();
    block = block->get_next_block();
  }
  OB_UNIS_ADD_LEN(block_size_);
  OB_UNIS_ADD_LEN(row_count_);
  OB_UNIS_ADD_LEN(col_count_);
  OB_UNIS_ADD_LEN(is_read_only_);
  OB_UNIS_ADD_LEN(last_row_size_);
  OB_UNIS_ADD_LEN(use_compact_row_);
  return len;
}
} //end common
} //end oceanbase
