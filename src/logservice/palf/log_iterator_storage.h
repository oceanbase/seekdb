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

#ifndef OCEANBASE_LOGSERVICE_ITERATOR_STORAGE_
#define OCEANBASE_LOGSERVICE_ITERATOR_STORAGE_
#include <cstdint>
#include "lib/file/ob_file.h"
#include "lib/ob_errno.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_utility.h"
#include "lib/function/ob_function.h"   // ObFunction
#include "log_io_context.h"
#include "log_storage_interface.h"
#include "lsn.h"
#include "log_reader_utils.h"
namespace oceanbase
{
namespace palf
{
typedef ObFunction<LSN()> GetFileEndLSN;
class IteratorStorage 
{
public:
  IteratorStorage();
  ~IteratorStorage();
  int init(const LSN &start_lsn,
           const int64_t block_size,
           const GetFileEndLSN &get_file_end_lsn,
           ILogStorage *log_storage);
  void destroy();
  void reuse(const LSN &start_lsn);
  inline const LSN get_lsn(const offset_t pos) const
  { return start_lsn_ + pos; }
  inline bool check_iterate_end(const offset_t pos) const
  { return start_lsn_ + pos > get_file_end_lsn_(); }
  int pread(const int64_t pos,
            const int64_t in_read_size,
            char *&buf,
            int64_t &out_read_size, 
            LogIOContext &io_ctx);
  TO_STRING_KV(K_(start_lsn), K_(end_lsn), K_(read_buf), K_(block_size), KP(log_storage_), KPC(log_storage_),
               "storage_type", (NULL == log_storage_ ? "dummy" : log_storage_->get_log_storage_type_str()));
private:
  int read_data_from_storage_(
      int64_t &pos,
      const int64_t in_read_size,
      char *&buf,
      int64_t &out_read_size,
      LogIOContext &io_ctx);

  int ensure_memory_layout_correct_(const int64_t pos,
                                    const int64_t in_read_size,
                                    int64_t &remain_valid_data_size);
  void do_memove_(ReadBuf &dst,
                  const int64_t pos,
                  int64_t &valid_tail_part_size);
  bool is_memory_storage_() const
  { return ILogStorageType::MEMORY_STORAGE == log_storage_->get_log_storage_type(); }
  bool is_hybrid_storage_() const
  { return ILogStorageType::HYBRID_STORAGE == log_storage_->get_log_storage_type(); }
  inline int64_t get_valid_data_len_()
  { return end_lsn_ - start_lsn_; }
private:
  // update after read_data_from_storage
  // 'start_lsn_' is the base position
  LSN start_lsn_;
  // end_lsn_ is always same sa start_lsn_ + read_buf_.buf_len_, except for reuse
  LSN end_lsn_;
  ReadBuf read_buf_;
  int64_t block_size_;
  ILogStorage *log_storage_;
  GetFileEndLSN get_file_end_lsn_;
  LogIOContext io_ctx_;
  bool is_inited_;
};

class MemoryStorage : public ILogStorage {
public:
  MemoryStorage();
  ~MemoryStorage();
  int init(const LSN &start_lsn);
  void destroy();
  bool is_inited() const { return is_inited_; }
  int append(const char *buf, const int64_t buf_len);
  int pread(const LSN& lsn,
	    const int64_t in_read_size,
	    ReadBuf &read_buf,
	    int64_t &out_read_size, 
            LogIOContext &io_ctx) final;
  INHERIT_TO_STRING_KV("ILogStorage", ILogStorage, K_(start_lsn), K_(log_tail), KP(buf_), K_(buf_len), K_(is_inited));
private:
  const char *buf_;
  int64_t buf_len_;
  LSN start_lsn_;
  LSN log_tail_;
  bool is_inited_;
};

} // end namespace palf
} // end namespace oceanbase
#endif
