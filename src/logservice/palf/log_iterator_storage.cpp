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

#include "log_iterator_storage.h"
namespace oceanbase
{
namespace palf
{
IteratorStorage::IteratorStorage() :
  start_lsn_(),
  end_lsn_(),
  read_buf_(),
  block_size_(0),
  log_storage_(NULL),
  io_ctx_(),
  is_inited_(false) {}

IteratorStorage::~IteratorStorage()
{
  destroy();
}

int IteratorStorage::init(
    const LSN &start_lsn,
    const int64_t block_size,
    const GetFileEndLSN &get_file_end_lsn,
    ILogStorage *log_storage)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
  } else {
    start_lsn_ = start_lsn;
    end_lsn_ = start_lsn;
    read_buf_.reset();
    block_size_ = block_size;
    log_storage_ = log_storage;
    get_file_end_lsn_ = get_file_end_lsn;
    is_inited_ = true;
  }
  if (OB_SUCC(ret) && !get_file_end_lsn_.is_valid()) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    destroy();
  }
  return ret;
}

void IteratorStorage::destroy()
{
  is_inited_ = false;
  start_lsn_.reset();
  end_lsn_.reset();
  block_size_ = 0;
  if (NULL != log_storage_ && !is_memory_storage_()) {
    free_read_buf(read_buf_);
  }
  log_storage_ = NULL;
  io_ctx_.destroy();
}

void IteratorStorage::reuse(const LSN &start_lsn)
{
  start_lsn_ = start_lsn;
  end_lsn_ = start_lsn;
}

// read data from 'read_buf_'
int IteratorStorage::pread(
    const int64_t pos,
    const int64_t in_read_size,
    char *&buf,
    int64_t &out_read_size,
    LogIOContext &io_ctx)
{
  int ret = OB_SUCCESS;
  const int64_t real_in_read_size = MIN(in_read_size, get_file_end_lsn_() - (start_lsn_ + pos));
  int64_t real_pos = pos;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  // there is no valid data
  } else if (0 > pos || 0 > real_in_read_size) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", K(ret), K(pos), K(in_read_size), KPC(this));
  } else if (pos > get_valid_data_len_()) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(ERROR, "want to read position is greater than max valid data len", K(pos), KPC(this));
  } else if (0 == real_in_read_size) {
    ret = OB_ITER_END;
    PALF_LOG(WARN, "IteratorStorage has iterate end", K(ret), KPC(this));
  } else if (OB_FAIL(read_data_from_storage_(real_pos, real_in_read_size, buf, out_read_size, io_ctx))) {
  } else {
    start_lsn_ = start_lsn_ + real_pos;
    end_lsn_ = start_lsn_ + out_read_size;
  }
  return ret;
}

int IteratorStorage::read_data_from_storage_(
    int64_t &pos,
    const int64_t in_read_size,
    char *&buf,
    int64_t &out_read_size,
    LogIOContext &io_ctx)
{
  int ret = OB_SUCCESS;
  int64_t remain_valid_data_size = 0;
  if (OB_FAIL(ensure_memory_layout_correct_(pos, in_read_size, remain_valid_data_size))) {
  } else {
    // avoid read repeated data from disk
    const LSN curr_round_read_lsn = start_lsn_ + pos + remain_valid_data_size;
    const int64_t real_in_read_size = in_read_size - remain_valid_data_size;
    read_buf_.buf_ += remain_valid_data_size;
    if (0ul == real_in_read_size) {
      ret = OB_ERR_UNEXPECTED;
      PALF_LOG(ERROR, "real read size is zero, unexpected error!!!", K(ret), K(real_in_read_size));
    } else if (OB_FAIL(log_storage_->pread(curr_round_read_lsn,
            real_in_read_size,
            read_buf_, out_read_size, io_ctx))) {
    }
    read_buf_.buf_ -= remain_valid_data_size;
    if (OB_SUCC(ret)) {
      buf = read_buf_.buf_;
      out_read_size += remain_valid_data_size;
      // check the 'read_buf_' whether has LogBlockHeader, if has, the return LSN need decrese MAX_INFO_BLOCK_SIZE
    }
  }
  return ret;
}

int IteratorStorage::ensure_memory_layout_correct_(
    const int64_t pos,
    const int64_t in_read_size,
    int64_t &remain_valid_data_size)
{
  int ret = OB_SUCCESS;
  remain_valid_data_size = 0;
  // NB: For memory storage, no need alloc_read_buf.
  if (!is_memory_storage_()) {
    const int64_t max_valid_buf_len = read_buf_.buf_len_ - LOG_DIO_ALIGN_SIZE - LOG_CACHE_ALIGN_SIZE;
    ReadBuf tmp_read_buf = read_buf_;
    // buf not enough, need alloc or expand
    if (in_read_size > max_valid_buf_len) {
      ret = alloc_read_buf("IteratorStorage", in_read_size, tmp_read_buf);
    }
    if (OB_SUCC(ret)) {
      // memmove tail valid part data to header
      do_memove_(tmp_read_buf, pos, remain_valid_data_size);
      read_buf_ = tmp_read_buf;
    }
  }
  return ret;
}

void IteratorStorage::do_memove_(ReadBuf &dst, const int64_t pos, int64_t &valid_tail_part_size)
{
  valid_tail_part_size = lower_align(get_valid_data_len_() - pos, LOG_DIO_ALIGN_SIZE);
  OB_ASSERT(valid_tail_part_size >= 0);
  if (false == read_buf_.is_valid()) {
    // do nothing
  } else {
    OB_ASSERT(valid_tail_part_size < dst.buf_len_);
    MEMMOVE(dst.buf_, read_buf_.buf_ + pos, valid_tail_part_size);
    if (read_buf_ != dst) {
      free_read_buf(read_buf_);
    }
  }
}

MemoryStorage::MemoryStorage() : ILogStorage(ILogStorageType::MEMORY_STORAGE),
                                 buf_(NULL),
                                 buf_len_(0),
                                 start_lsn_(),
                                 log_tail_(),
                                 is_inited_(false)
{
}

MemoryStorage::~MemoryStorage()
{
  destroy();
}

int MemoryStorage::init(const LSN &start_lsn)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
  } else if (false == start_lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", K(ret), K(start_lsn));
  } else {
    buf_ = NULL;
    buf_len_ = 0;
    log_tail_ = start_lsn_ = start_lsn;
    is_inited_ = true;
  }
  return ret;
}

void MemoryStorage::destroy()
{
  is_inited_ = false;
  buf_ = NULL;
  buf_len_ = 0;
  log_tail_.reset();
  start_lsn_.reset();
}

int MemoryStorage::append(const char *buf, const int64_t buf_len)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (NULL == buf || 0 >= buf_len) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    buf_ = buf;
    buf_len_ = buf_len;
    start_lsn_ = log_tail_;
    log_tail_ = log_tail_ + buf_len;
  }
  return ret;
}

int MemoryStorage::pread(const LSN &lsn,
			 const int64_t in_read_size,
			 ReadBuf &read_buf,
			 int64_t &out_read_size,
			 LogIOContext &io_ctx)
{
  int ret = OB_SUCCESS;
  UNUSED(io_ctx);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (false == lsn.is_valid() || 0 >= in_read_size) {
    ret = OB_INVALID_ARGUMENT;
  } else if (lsn >= log_tail_) {
    ret = OB_ERR_OUT_OF_UPPER_BOUND;
  } else if (lsn < start_lsn_) {
    ret = OB_ERR_OUT_OF_LOWER_BOUND;
  } else {
    const offset_t pos = lsn - start_lsn_;
    read_buf.buf_ = const_cast<char*>(buf_) + pos;
    out_read_size = MIN(log_tail_ - lsn, in_read_size);
    read_buf.buf_len_ = out_read_size;
  }
  return ret;
}

} // end namespace palf
} // end namespace oceanbase
