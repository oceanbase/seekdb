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

#ifndef SRC_SQL_ENGINE_JOIN_HASH_JOIN_JOIN_HASH_TABLE_H_
#define SRC_SQL_ENGINE_JOIN_HASH_JOIN_JOIN_HASH_TABLE_H_

#include "sql/engine/join/hash_join/hash_table.h"

namespace oceanbase
{
namespace sql
{

class JoinHashTable {
public:
  JoinHashTable() : hash_table_(NULL)
  {}
  int init(JoinTableCtx &hjt_ctx, ObIAllocator &allocator);
  int init_generic_ht(JoinTableCtx &hjt_ctx, ObIAllocator &allocator);
  bool use_normalized_ht(JoinTableCtx &hjt_ctx);
  int build_prepare(JoinTableCtx &ctx, int64_t row_count, int64_t bucket_count);
  int build(JoinPartitionRowIter &iter, JoinTableCtx &jt_ctx);
  int probe_prepare(JoinTableCtx &ctx, OutputInfo &output_info);
  int probe_batch(JoinTableCtx &ctx, OutputInfo &output_info);
  int project_matched_rows(JoinTableCtx &ctx, OutputInfo &output_info) {
    return hash_table_->project_matched_rows(ctx, output_info);
  };
  int get_unmatched_rows(JoinTableCtx &ctx, OutputInfo &output_info);
  void reset() {
    if (NULL != hash_table_) {
      hash_table_->reset();
    }
  };
  int64_t get_mem_used() const {
    return hash_table_->get_mem_used();
  }
  void free(ObIAllocator *allocator) {
    if (NULL != hash_table_) {
      hash_table_->free(allocator);
      allocator->free(hash_table_);
      hash_table_ = NULL;
    }
  }
  int64_t get_one_bucket_size() const { return hash_table_->get_one_bucket_size(); }
  int64_t get_normalized_key_size() const { return hash_table_->get_normalized_key_size(); }

  int64_t get_row_count() { return hash_table_->get_row_count(); };
  int64_t get_used_buckets() { return hash_table_->get_used_buckets(); }
  int64_t get_nbuckets() { return hash_table_->get_nbuckets(); }
  int64_t get_collisions() { return hash_table_->get_collisions(); }

private:
  IHashTable *hash_table_;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* SRC_SQL_ENGINE_JOIN_HASH_JOIN_JOIN_HASH_TABLE_H_*/
