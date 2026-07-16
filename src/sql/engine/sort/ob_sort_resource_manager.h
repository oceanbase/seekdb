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

#ifndef OCEANBASE_SQL_ENGINE_SORT_OB_SORT_RESOURCE_MANAGER_H_
#define OCEANBASE_SQL_ENGINE_SORT_OB_SORT_RESOURCE_MANAGER_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/ob_errno.h"
#include "sql/engine/ob_sql_mem_mgr_processor.h"

namespace oceanbase
{
namespace sql
{

class ObSortResourceManager
{
public:
  static const int64_t MIN_MEMORY_LIMIT = 8LL * 1024 * 1024;
  static const int64_t DEFAULT_FILE_BUF_SIZE = 2LL * 1024 * 1024;
  static const int64_t MIN_MERGE_WAYS = 2;
  static const int64_t MAX_MERGE_WAYS = 256;

  ObSortResourceManager() : mem_limit_(0), file_buf_size_(DEFAULT_FILE_BUF_SIZE),
    merge_ways_(MIN_MERGE_WAYS), estimated_rows_(0), row_width_(0),
    mem_processor_(nullptr) {}
  virtual ~ObSortResourceManager() {}

  virtual int init(ObSqlMemMgrProcessor &mem_processor, int64_t estimated_rows, int64_t row_width)
  {
    int ret = OB_SUCCESS;
    mem_processor_ = &mem_processor;
    estimated_rows_ = estimated_rows;
    row_width_ = row_width;
    if (OB_FAIL(update_memory_limit())) {
    } else if (OB_FAIL(calc_merge_ways())) {
    }
    return ret;
  }

  virtual int update_memory_limit()
  {
    int ret = OB_SUCCESS;
    if (mem_processor_ != nullptr) {
      mem_processor_->get_max_available_mem_size(mem_limit_);
      if (mem_limit_ < MIN_MEMORY_LIMIT) {
        mem_limit_ = MIN_MEMORY_LIMIT;
      }
    }
    return ret;
  }

  virtual int calc_merge_ways()
  {
    int ret = OB_SUCCESS;
    if (file_buf_size_ > 0) {
      merge_ways_ = (mem_limit_ - MIN_MEMORY_LIMIT) / file_buf_size_ / 2;
      if (merge_ways_ < MIN_MERGE_WAYS) {
        merge_ways_ = MIN_MERGE_WAYS;
      }
      if (merge_ways_ > MAX_MERGE_WAYS) {
        merge_ways_ = MAX_MERGE_WAYS;
      }
    }
    return ret;
  }

  virtual bool should_dump(int64_t current_mem_used) const
  {
    return current_mem_used >= mem_limit_;
  }

  OB_INLINE int64_t get_mem_limit() const { return mem_limit_; }
  OB_INLINE int64_t get_file_buf_size() const { return file_buf_size_; }
  OB_INLINE int64_t get_merge_ways() const { return merge_ways_; }
  OB_INLINE int64_t get_estimated_rows() const { return estimated_rows_; }
  OB_INLINE int64_t get_row_width() const { return row_width_; }

  void set_file_buf_size(int64_t sz) { file_buf_size_ = sz; }

protected:
  int64_t mem_limit_;
  int64_t file_buf_size_;
  int64_t merge_ways_;
  int64_t estimated_rows_;
  int64_t row_width_;
  ObSqlMemMgrProcessor *mem_processor_;
};

class ObSQLSortResourceManager : public ObSortResourceManager
{
public:
  ObSQLSortResourceManager() : part_cnt_(0), topn_cnt_(0), partition_mem_(0) {}
  virtual ~ObSQLSortResourceManager() {}

  int init(ObSqlMemMgrProcessor &mem_processor, int64_t estimated_rows,
           int64_t row_width, int64_t part_cnt, int64_t topn_cnt)
  {
    int ret = OB_SUCCESS;
    part_cnt_ = part_cnt;
    topn_cnt_ = topn_cnt;
    if (OB_FAIL(ObSortResourceManager::init(mem_processor, estimated_rows, row_width))) {
    } else {
      calc_partition_memory();
    }
    return ret;
  }

  virtual int update_memory_limit() override
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(ObSortResourceManager::update_memory_limit())) {
    } else {
      calc_partition_memory();
    }
    return ret;
  }

  virtual bool should_dump(int64_t current_mem_used) const override
  {
    return current_mem_used + partition_mem_ >= mem_limit_;
  }

  OB_INLINE int64_t get_partition_memory() const { return partition_mem_; }

private:
  void calc_partition_memory()
  {
    if (part_cnt_ > 0 && topn_cnt_ > 0) {
      partition_mem_ = part_cnt_ * topn_cnt_ * row_width_;
    }
  }

  int64_t part_cnt_;
  int64_t topn_cnt_;
  int64_t partition_mem_;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_SORT_OB_SORT_RESOURCE_MANAGER_H_ */
