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

#ifndef OCEANBASE_STORAGE_BLOCKSSTABLE_OB_SKIP_INDEX_FILTER_EXECUTOR_H
#define OCEANBASE_STORAGE_BLOCKSSTABLE_OB_SKIP_INDEX_FILTER_EXECUTOR_H

#include "share/schema/ob_table_param.h"
#include "sql/engine/ob_bit_vector.h"
#include "sql/engine/basic/ob_pushdown_filter.h"
#include "storage/blocksstable/index_block/ob_agg_row_struct.h"
#include "storage/blocksstable/index_block/ob_index_block_row_struct.h"

namespace oceanbase
{
namespace blocksstable
{
class ObAggRowReader;
class ObSkipIndexFilterExecutor final
{
public:
  ObSkipIndexFilterExecutor()
      : agg_row_reader_(), meta_(), skip_bit_(nullptr), allocator_(nullptr), is_inited_(false) {}
  ~ObSkipIndexFilterExecutor() { reset(); }
  void reset()
  {
    agg_row_reader_.reset();
    if (OB_NOT_NULL(allocator_)) {
      if (OB_NOT_NULL(skip_bit_)) {
        allocator_->free(skip_bit_);
        skip_bit_ = nullptr;
      }
    }
    allocator_ = nullptr;
    is_inited_ = false;
  }
  int init(const int64_t batch_size, common::ObIAllocator *allocator);
  int falsifiable_pushdown_filter(const uint32_t col_idx,
                                  const ObObjMeta &obj_meta,
                                  const ObSkipIndexType index_type,
                                  const ObMicroIndexInfo &index_info,
                                  sql::ObPhysicalFilterExecutor &filter,
                                  common::ObIAllocator &allocator,
                                  const bool use_vectorize);

private:
  int filter_on_min_max(const uint32_t col_idx,
                        const uint64_t row_count,
                        const ObObjMeta &obj_meta,
                        sql::ObWhiteFilterExecutor &filter,
                        common::ObIAllocator &allocator);

  int read_aggregate_data(const uint32_t col_idx,
                   common::ObIAllocator &allocator,
                   const share::schema::ObColumnParam *col_param,
                   const ObObjMeta &obj_meta,
                   const bool is_padding_mode,
                   ObStorageDatum &null_count,
                   ObStorageDatum &min_datum,
                   bool &is_min_prefix,
                   ObStorageDatum &max_datum,
                   bool &is_max_prefix);
  int pad_column(const ObObjMeta &obj_meta,
                 const share::schema::ObColumnParam *col_param,
                 const bool is_padding_mode,
                 common::ObIAllocator &padding_alloc,
                 blocksstable::ObStorageDatum &datum);

  // *_operator args are the same
  int eq_operator(const sql::ObWhiteFilterExecutor &filter,
                  const common::ObDatum &min_datum,
                  const bool &is_min_prefix,
                  const common::ObDatum &max_datum,
                  const bool &is_max_prefix,
                  sql::ObBoolMask &fal_desc);

  int ne_operator(const sql::ObWhiteFilterExecutor &filter,
                  const common::ObDatum &min_datum,
                  const bool &is_min_prefix,
                  const common::ObDatum &max_datum,
                  const bool &is_max_prefix,
                  sql::ObBoolMask &fal_desc);
  int gt_operator(const sql::ObWhiteFilterExecutor &filter,
                  const common::ObDatum &min_datum,
                  const bool &is_min_prefix,
                  const common::ObDatum &max_datum,
                  const bool &is_max_prefix,
                  sql::ObBoolMask &fal_desc);

  int ge_operator(const sql::ObWhiteFilterExecutor &filter,
                  const common::ObDatum &min_datum,
                  const bool &is_min_prefix,
                  const common::ObDatum &max_datum,
                  const bool &is_max_prefix,
                  sql::ObBoolMask &fal_desc);

  int lt_operator(const sql::ObWhiteFilterExecutor &filter,
                  const common::ObDatum &min_datum,
                  const bool &is_min_prefix,
                  const common::ObDatum &max_datum,
                  const bool &is_max_prefix,
                  sql::ObBoolMask &fal_desc);

  int le_operator(const sql::ObWhiteFilterExecutor &filter,
                  const common::ObDatum &min_datum,
                  const bool &is_min_prefix,
                  const common::ObDatum &max_datum,
                  const bool &is_max_prefix,
                  sql::ObBoolMask &fal_desc);

  int bt_operator(const sql::ObWhiteFilterExecutor &filter,
                  const common::ObDatum &min_datum,
                  const bool &is_min_prefix,
                  const common::ObDatum &max_datum,
                  const bool &is_max_prefix,
                  sql::ObBoolMask &fal_desc);

  int in_operator(const sql::ObWhiteFilterExecutor &filter,
                  const common::ObDatum &min_datum,
                  const bool &is_min_prefix,
                  const common::ObDatum &max_datum,
                  const bool &is_max_prefix,
                  sql::ObBoolMask &fal_desc);
  
  int black_filter_on_min_max(const uint32_t col_idx,
                              const uint64_t row_count,
                              const ObObjMeta &obj_meta,
                              sql::ObBlackFilterExecutor &filter,
                              common::ObIAllocator &allocator,
                              const bool use_vectorize);
private:
  ObAggRowReader agg_row_reader_;
  ObSkipIndexColMeta meta_;
  sql::ObBitVector *skip_bit_;      // to be compatible with the black filter filter() method
  common::ObIAllocator *allocator_;
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObSkipIndexFilterExecutor);
};

} // end namespace blocksstable
} // end namespace oceanbase
#endif
