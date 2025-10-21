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

#include "storage/direct_load/ob_direct_load_batch_rows.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadPXBatchRows
{
public:
  ObTableLoadPXBatchRows();
  ~ObTableLoadPXBatchRows();
  void reset();
  void reuse();
  int init(const common::ObIArray<share::schema::ObColDesc> &px_col_descs,
           const common::ObIArray<common::ObAccuracy> &px_col_accuracys,
           const common::ObIArray<int64_t> &px_column_project_idxs, // px column corresponds to which store column
           const common::ObIArray<share::schema::ObColDesc> &col_descs,
           const sql::ObBitVector *col_nullables, const ObDirectLoadRowFlag &row_flag,
           const int64_t max_batch_size,
           // For old path farm to pass
           const bool need_reshape);

  // Deep copy
  int append_selective(const IVectorPtrs &vectors, const uint16_t *selector, int64_t size);
  int append_selective(const ObIArray<ObDatumVector> &datum_vectors, const uint16_t *selector,
                       int64_t size);
  int append_row(const ObDirectLoadDatumRow &datum_row);
  // Shallow copy
  int shallow_copy(const IVectorPtrs &vectors, const int64_t batch_size);
  int shallow_copy(const ObIArray<ObDatumVector> &datum_vectors, const int64_t batch_size);

  const ObIArray<storage::ObDirectLoadVector *> &get_vectors() const { return vectors_; }
  storage::ObDirectLoadBatchRows &get_batch_rows() { return batch_rows_; }

  inline int64_t get_column_count() const { return vectors_.count(); }
  inline int64_t size() const { return batch_rows_.size(); }
  inline int64_t remain_size() const { return batch_rows_.remain_size(); }
  inline bool empty() const { return batch_rows_.empty(); }
  inline bool full() const { return batch_rows_.full(); }

  // Total memory usage
  inline int64_t memory_usage() const { return batch_rows_.memory_usage(); }
  // Rows bytes usage
  inline int64_t bytes_usage() const { return batch_rows_.bytes_usage(); }

  TO_STRING_KV(K_(vectors), K_(batch_rows), K_(is_inited));

private:
  ObArray<ObObjMeta> col_types_;
  ObArray<ObAccuracy> col_accuracys_;
  ObArray<storage::ObDirectLoadVector *> vectors_;
  ObArenaAllocator reshape_allocator_;
  storage::ObDirectLoadBatchRows batch_rows_;
  bool need_reshape_;
  bool is_inited_;
};

} // namespace observer
} // namespace oceanbase
