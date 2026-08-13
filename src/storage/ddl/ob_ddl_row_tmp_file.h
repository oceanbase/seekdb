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

#ifndef OCEANBASE_STORAGE_OB_DDL_ROW_TMP_FILE_H_
#define OCEANBASE_STORAGE_OB_DDL_ROW_TMP_FILE_H_

#include "storage/ddl/ob_ddl_struct.h"
#include "storage/blocksstable/ob_batch_datum_rows.h"
#include "query/engine/basic/ob_spill_batch_spool.h"

namespace oceanbase
{
namespace storage
{

struct ObDDLChunk;
struct ObChunk;

struct ObDDLRowFile
{
public:
  ObDDLRowFile() :
    is_opened_(false),
    tablet_id_(),
    slice_idx_(-1),
    column_count_(0),
    spool_factory_(nullptr),
    spool_(nullptr),
    bdrs_(),
    rotation_recommended_(false)
  {}
  ~ObDDLRowFile()
  {
    if (is_opened_) {
      IGNORE_RETURN close();
    }
  }
  int open(const ObIArray<ObColumnSchemaItem>  &all_column_schema_its,
           const ObTabletID &tablet_id,
           const int64_t slice_idx,
           const int64_t max_batch_size,
           query::ObISpillBatchSpoolFactory &spool_factory,
           const int64_t memory_limit = INT64_MAX,
           const int64_t dir_id = 0);
  int close();
  int append_batch(const blocksstable::ObBatchDatumRows &brs);
  int seal();
  int get_next_batch(blocksstable::ObBatchDatumRows *&bdrs);
  OB_INLINE bool should_rotate() const { return rotation_recommended_; }
  query::ObSpillBatchSpoolStats get_stats() const;
  OB_INLINE bool is_opened() const { return is_opened_; }
  TO_STRING_KV(K(is_opened_), K(tablet_id_), K(slice_idx_), K(column_count_),
      K(rotation_recommended_), K(bdrs_));

private:
  DISALLOW_COPY_AND_ASSIGN(ObDDLRowFile);

private:
  bool is_opened_;
  ObTabletID tablet_id_;
  int64_t slice_idx_;
  int64_t column_count_;
  query::ObISpillBatchSpoolFactory *spool_factory_;
  query::ObISpillBatchSpool *spool_;
  blocksstable::ObBatchDatumRows bdrs_;
  bool rotation_recommended_;
};

class ObDDLRowFileGenerator
{
public:
  ObDDLRowFileGenerator() :
    is_inited_(false),
    is_generation_sync_output_(false),
    tablet_id_(ObTabletID::INVALID_TABLET_ID),
    slice_idx_(-1),
    max_batch_size_(0),
    row_file_memory_limit_(INT64_MAX),
    all_column_schema_its_(),
    row_file_arr_(),
    row_file_arr_for_output_(nullptr),
    sync_chunk_data_(nullptr),
    spool_factory_(nullptr)
  {
    row_file_arr_.set_attr(ObMemAttr("DDLRowFiles"));
  }
  ~ObDDLRowFileGenerator()
  {
    reset();
  }
  void reset();
  int init(const ObTabletID &tablet_id,
           const int64_t slice_idx,
           const int64_t max_batch_size,
           const int64_t row_file_memory_limit,
           const ObIArray<ObColumnSchemaItem> &all_column_schema_its,
           const bool is_sync_generation,
           query::ObISpillBatchSpoolFactory &spool_factory);
  int append_batch(const blocksstable::ObBatchDatumRows &bdrs,
                   const bool is_slice_end,
                   ObDDLChunk &output_chunk);
  int try_generate_output_chunk(const bool is_slice_end,
                                ObDDLChunk &output_chunk);

public:
  static const int64_t ROW_FILE_MEMORY_LIMIT = 512L * 1024L;

private:
  DISALLOW_COPY_AND_ASSIGN(ObDDLRowFileGenerator);

private:
  bool is_inited_;
  bool is_generation_sync_output_;
  ObTabletID tablet_id_;
  int64_t slice_idx_;
  int64_t max_batch_size_;
  int64_t row_file_memory_limit_;
  ObArray<ObColumnSchemaItem> all_column_schema_its_;
  ObArray<ObDDLRowFile *> row_file_arr_;
  ObArray<ObDDLRowFile *> *row_file_arr_for_output_;
  ObChunk *sync_chunk_data_;
  query::ObISpillBatchSpoolFactory *spool_factory_;
};

} // end namespace storage
} // end namespace oceanbase
#endif // OCEANBASE_STORAGE_OB_DDL_ROW_TMP_FILE_H_
