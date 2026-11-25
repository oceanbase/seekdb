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

#pragma once

#include "storage/tmp_file/ob_tmp_file_global.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{
namespace tmp_file
{
class ObTmpFileIOCtx;
class ObTmpWriteBufferPool;
class ObTmpFileFlushPriorityManager;

//for virtual table show
class ObTmpFileInfo
{
public:
  ObTmpFileInfo() :
    trace_id_(),
    dir_id_(ObTmpFileGlobal::INVALID_TMP_FILE_DIR_ID),
    fd_(ObTmpFileGlobal::INVALID_TMP_FILE_FD),
    file_size_(0),
    truncated_offset_(0),
    is_deleting_(false),
    cached_data_page_num_(0),
    write_back_data_page_num_(0),
    flushed_data_page_num_(0),
    ref_cnt_(0),
    birth_ts_(-1),
    tmp_file_ptr_(nullptr),
    label_(),
    write_info_(),
    read_info_() {}
  virtual ~ObTmpFileInfo() { reset(); }
  virtual int init(const ObCurTraceId::TraceId &trace_id,
                   const int64_t dir_id,
                   const int64_t fd,
                   const int64_t file_size,
                   const int64_t truncated_offset,
                   const bool is_deleting,
                   const int64_t cached_page_num,
                   const int64_t write_back_data_page_num,
                   const int64_t flushed_data_page_num,
                   const int64_t ref_cnt,
                   const int64_t birth_ts,
                   const void* const tmp_file_ptr,
                   const char* const label,
                   const int64_t write_req_cnt,
                   const int64_t unaligned_write_req_cnt,
                   const int64_t write_persisted_tail_page_cnt,
                   const int64_t lack_page_cnt,
                   const int64_t last_modify_ts,
                   const int64_t read_req_cnt,
                   const int64_t unaligned_read_req_cnt,
                   const int64_t total_truncated_page_read_cnt,
                   const int64_t total_kv_cache_page_read_cnt,
                   const int64_t total_uncached_page_read_cnt,
                   const int64_t total_wbp_page_read_cnt,
                   const int64_t truncated_page_read_hits,
                   const int64_t kv_cache_page_read_hits,
                   const int64_t uncached_page_read_hits,
                   const int64_t wbp_page_read_hits,
                   const int64_t total_read_size,
                   const int64_t last_access_ts);
  virtual void reset();
public:
  struct ObTmpFileWriteInfo
  {
  public:
    ObTmpFileWriteInfo() :
      write_req_cnt_(0),
      unaligned_write_req_cnt_(0),
      write_persisted_tail_page_cnt_(0),
      lack_page_cnt_(0),
      last_modify_ts_(-1) {}
    void reset();
  public:
    int64_t write_req_cnt_;
    int64_t unaligned_write_req_cnt_;
    int64_t write_persisted_tail_page_cnt_;
    int64_t lack_page_cnt_;
    int64_t last_modify_ts_;
    TO_STRING_KV(K_(write_req_cnt), K_(unaligned_write_req_cnt),
        K_(write_persisted_tail_page_cnt), K_(lack_page_cnt), K_(last_modify_ts));
  };
  struct ObTmpFileReadInfo
  {
  public:
    ObTmpFileReadInfo() :
      read_req_cnt_(0),
      unaligned_read_req_cnt_(0),
      total_truncated_page_read_cnt_(0),
      total_kv_cache_page_read_cnt_(0),
      total_uncached_page_read_cnt_(0),
      total_wbp_page_read_cnt_(0),
      truncated_page_read_hits_(0),
      kv_cache_page_read_hits_(0),
      uncached_page_read_hits_(0),
      wbp_page_read_hits_(0),
      total_read_size_(0),
      last_access_ts_(-1) {}
    void reset();
  public:
    int64_t read_req_cnt_;
    int64_t unaligned_read_req_cnt_;
    int64_t total_truncated_page_read_cnt_;          // the total read count of truncated pages
    int64_t total_kv_cache_page_read_cnt_;           // the total read count of pages in kv_cache
    int64_t total_uncached_page_read_cnt_;           // the total read count of pages with io
    int64_t total_wbp_page_read_cnt_;                // the total read count of pages in wbp
    int64_t truncated_page_read_hits_;               // the hit count of truncated pages when read
    int64_t kv_cache_page_read_hits_;                // the hit count of pages in kv_cache when read
    int64_t uncached_page_read_hits_;                // the hit count of persisted pages when read
    int64_t wbp_page_read_hits_;                     // the hit count of pages in wbp when read
    int64_t total_read_size_;
    int64_t last_access_ts_;
    TO_STRING_KV(K_(read_req_cnt), K_(unaligned_read_req_cnt), K_(total_truncated_page_read_cnt),
        K_(total_kv_cache_page_read_cnt), K_(total_uncached_page_read_cnt),
        K_(total_wbp_page_read_cnt), K_(truncated_page_read_hits), K_(kv_cache_page_read_hits),
        K_(uncached_page_read_hits), K_(wbp_page_read_hits), K_(total_read_size), K_(last_access_ts));
  };

public:
  // common info
  common::ObCurTraceId::TraceId trace_id_;

  int64_t dir_id_;
  int64_t fd_;
  int64_t file_size_;
  int64_t truncated_offset_;
  bool is_deleting_;
  int64_t cached_data_page_num_;
  int64_t write_back_data_page_num_;
  int64_t flushed_data_page_num_;
  int64_t ref_cnt_;
  int64_t birth_ts_;
  const void *tmp_file_ptr_;
  ObFixedLengthString<ObTmpFileGlobal::TMP_FILE_MAX_LABEL_SIZE + 1> label_;
  ObTmpFileWriteInfo write_info_;
  ObTmpFileReadInfo read_info_;

  TO_STRING_KV(K(trace_id_), K(dir_id_), K(fd_), K(file_size_),
               K(truncated_offset_), K(is_deleting_), K(cached_data_page_num_),
               K(write_back_data_page_num_), K(flushed_data_page_num_),
               K(ref_cnt_), K(birth_ts_), KP(tmp_file_ptr_), K(label_),
               K(write_info_), K(read_info_));
};

class ObSharedNothingTmpFile;

class ObSNTmpFileHandle final
{
public:
  ObSNTmpFileHandle() : ptr_(nullptr) {}
  ObSNTmpFileHandle(ObSharedNothingTmpFile *tmp_file);
  ObSNTmpFileHandle(const ObSNTmpFileHandle &handle);
  ObSNTmpFileHandle & operator=(const ObSNTmpFileHandle &other);
  ~ObSNTmpFileHandle() { reset(); }
  OB_INLINE ObSharedNothingTmpFile * get() const {return ptr_; }
  bool is_inited() { return nullptr != ptr_; }
  void reset();
  int init(ObSharedNothingTmpFile *tmp_file);
  TO_STRING_KV(KP(ptr_));
private:
  ObSharedNothingTmpFile *ptr_;
};

struct ObTmpFileKey final
{
  explicit ObTmpFileKey(const int64_t fd) : fd_(fd) {}
  OB_INLINE int hash(uint64_t &hash_val) const
  {
    hash_val = murmurhash(&fd_, sizeof(int64_t), 0);
    return OB_SUCCESS;
  }
  OB_INLINE bool operator==(const ObTmpFileKey &other) const { return fd_ == other.fd_; }
  TO_STRING_KV(K(fd_));
  int64_t fd_;
};

}  // end namespace tmp_file
}  // end namespace oceanbase
