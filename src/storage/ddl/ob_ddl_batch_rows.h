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

#include "data_plane/blocksstable/ob_datum_row.h"
#include "storage/ddl/ob_ddl_vector.h"
#include "share/ob_batch_selector.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObColDesc;
} // namespace schema
} // namespace share
namespace storage
{
struct ObColumnSchemaItem;

// Row-layout metadata used by the online DDL vector buffer.  This intentionally
// lives with the buffer instead of depending on the removed table-load module.
struct ObDDLRowFlag
{
  ObDDLRowFlag() : flag_(0) {}
  void reset() { flag_ = 0; }
  OB_INLINE int64_t get_column_count(const int64_t column_count) const
  {
    return uncontain_hidden_pk_ ? column_count + 1 : column_count;
  }
  TO_STRING_KV(K_(uncontain_hidden_pk), K_(has_delete_row), K_(lob_id_only));
  union
  {
    struct
    {
      bool uncontain_hidden_pk_ : 1;
      bool has_delete_row_ : 1;
      bool lob_id_only_ : 1;
      int64_t reserved_ : 61;
    };
    int64_t flag_;
  };
};

class ObDDLBatchRows
{
public:
  ObDDLBatchRows();
  ~ObDDLBatchRows();
  void reset();
  void reuse();
  int init(const common::ObIArray<share::schema::ObColDesc> &col_descs,
           const sql::ObBitVector *col_nullables, const int64_t max_batch_size,
           const ObDDLRowFlag &row_flag);
  int init(const common::ObIArray<ObColumnSchemaItem> &column_schemas,
           const int64_t max_batch_size,
           const ObDDLRowFlag &row_flag);

  // Deep copy
  int append_row(const blocksstable::ObDatumRow &datum_row);
  int append_row(const blocksstable::ObStorageDatum *datums, const int64_t column_count);
  int append_row(const ObIArray<ObDatum *> &datums);
  int append_batch(const ObDDLBatchRows &vectors, const int64_t offset,
                   const int64_t size);
  int append_batch(const IVectorPtrs &vectors, const int64_t offset, const int64_t size);
  int append_batch(const ObIArray<ObDatumVector> &datum_vectors, const int64_t offset,
                   const int64_t size);
  int append_selective(const ObDDLBatchRows &src, const uint16_t *selector,
                       const int64_t size);
  int append_selective(const IVectorPtrs &vectors, const uint16_t *selector, int64_t size);
  int append_selective(const IVectorPtrs &vectors, share::ObBatchSelector &selector);
  int append_selective(const ObIArray<ObDatumVector> &datum_vectors, const uint16_t *selector,
                       int64_t size);

  const ObIArray<ObDDLVector *> &get_vectors() const { return vectors_; }
  int64_t get_max_batch_size() const { return max_batch_size_; }
  const ObDDLRowFlag &get_row_flag() const { return row_flag_; }
  void set_row_flag(const ObDDLRowFlag &row_flag) { row_flag_ = row_flag; }
  inline int64_t get_column_count() const
  {
    return row_flag_.uncontain_hidden_pk_ ? vectors_.count() - 1 : vectors_.count();
  }

  inline void set_size(const int64_t size) { size_ = size; }
  inline int64_t size() const { return size_; }
  inline int64_t remain_size() const { return max_batch_size_ - size_; }
  inline bool empty() const { return 0 == size_; }
  inline bool full() const { return size_ == max_batch_size_; }
  inline bool is_inited() const { return is_inited_; }

  // Total memory usage
  int64_t memory_usage() const;
  // Rows bytes usage
  int64_t bytes_usage() const;

  TO_STRING_KV(K_(vectors), K_(max_batch_size), K_(row_flag), K_(size), K_(is_inited));

private:
  int init_vectors(const common::ObIArray<share::schema::ObColDesc> &col_descs,
                   const sql::ObBitVector *col_nullables, int64_t max_batch_size);
  int init_vectors(const common::ObIArray<ObColumnSchemaItem> &column_schemas, int64_t max_batch_size);

private:
  common::ObArenaAllocator allocator_; // resident memory allocator
  ObArray<ObDDLVector *> vectors_;
  int64_t max_batch_size_;
  ObDDLRowFlag row_flag_;
  int64_t size_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase
