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

#ifndef OCEANBASE_DATA_PLANE_API_DDL_OB_DIRECT_INSERT_H_
#define OCEANBASE_DATA_PLANE_API_DDL_OB_DIRECT_INSERT_H_

#include <stdint.h>
#include <utility>
#include "common/ob_tablet_id.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_array.h"
#include "lib/ob_define.h"

namespace oceanbase
{
namespace common
{
struct ObDatum;
class ObIVector;
}
namespace query
{
class ObISpillBatchSpoolFactory;
}
namespace data_plane
{

// Installs the query worker state required by a direct-insert background
// thread.  The context must outlive the session passed to start().
class ObIDirectInsertWorkerContext
{
public:
  virtual ~ObIDirectInsertWorkerContext() {}
  virtual void bind_current_thread() = 0;
};

struct ObDirectInsertStartParam final
{
  typedef common::ObTabletID Participant;

  ObDirectInsertStartParam()
    : ddl_task_id_(0), execution_id_(0), table_id_(0), worker_count_(0),
      participants_()
  {}

  bool is_valid() const
  {
    return ddl_task_id_ > 0 && execution_id_ >= 0 && table_id_ > 0
        && worker_count_ > 0 && !participants_.empty();
  }

  int64_t ddl_task_id_;
  int64_t execution_id_;
  int64_t table_id_;
  int64_t worker_count_;
  common::ObArray<Participant> participants_;
};

struct ObDirectInsertPlanFacts final
{
  ObDirectInsertPlanFacts()
    : regenerate_heap_table_pk_(false), vector_rowkey_vid_(false),
      has_table_autoinc_(false), rowkey_doc_id_(false),
      data_table_without_pk_(false)
  {}

  bool regenerate_heap_table_pk_;
  bool vector_rowkey_vid_;
  bool has_table_autoinc_;
  bool rowkey_doc_id_;
  bool data_table_without_pk_;
};

struct ObDirectInsertWritePolicy final
{
  ObDirectInsertWritePolicy()
    : vector_generated_id_(false), idempotent_tablet_autoinc_(false),
      idempotent_table_autoinc_(false), idempotent_doc_id_(false)
  {}

  bool vector_generated_id_;
  bool idempotent_tablet_autoinc_;
  bool idempotent_table_autoinc_;
  bool idempotent_doc_id_;
};

enum ObDirectInsertAutoincScope
{
  DIRECT_INSERT_TABLE_AUTOINC = 0,
  DIRECT_INSERT_TABLET_AUTOINC
};

struct ObDirectInsertAutoincParam final
{
  static const int64_t RANGE_INTERVAL = 10000;

  ObDirectInsertAutoincParam()
    : enabled_(false), slice_count_(0), slice_index_(0),
      range_interval_(0)
  {}

  void reset()
  {
    enabled_ = false;
    slice_count_ = 0;
    slice_index_ = 0;
    range_interval_ = 0;
  }

  bool is_valid() const
  {
    return !enabled_ || (slice_count_ > 0 && slice_index_ >= 0
        && range_interval_ > 0);
  }

  bool enabled_;
  int64_t slice_count_;
  int64_t slice_index_;
  int64_t range_interval_;
};

struct ObDirectInsertRowView final
{
  ObDirectInsertRowView() : datums_(nullptr), datum_count_(0) {}
  ObDirectInsertRowView(common::ObDatum *const *datums,
                        const int64_t datum_count)
    : datums_(datums), datum_count_(datum_count)
  {}

  bool is_valid() const { return nullptr != datums_ && datum_count_ > 0; }
  common::ObDatum *const *datums_;
  int64_t datum_count_;
};

// A batch is borrowed for one append_batch() call.  Selection is either a
// contiguous range or a uint16 index list; one virtual call always handles the
// whole selected batch.
struct ObDirectInsertBatchView final
{
  enum SelectionType
  {
    INVALID_SELECTION = 0,
    CONTIGUOUS_SELECTION,
    INDEX_SELECTION
  };

  ObDirectInsertBatchView()
    : vectors_(nullptr), vector_count_(0),
      selection_type_(INVALID_SELECTION), offset_(0), row_count_(0),
      indices_(nullptr)
  {}

  static ObDirectInsertBatchView contiguous(
      common::ObIVector *const *vectors,
      const int64_t vector_count,
      const int64_t offset,
      const int64_t row_count)
  {
    ObDirectInsertBatchView view;
    view.vectors_ = vectors;
    view.vector_count_ = vector_count;
    view.selection_type_ = CONTIGUOUS_SELECTION;
    view.offset_ = offset;
    view.row_count_ = row_count;
    return view;
  }

  static ObDirectInsertBatchView indexed(
      common::ObIVector *const *vectors,
      const int64_t vector_count,
      const uint16_t *indices,
      const int64_t row_count)
  {
    ObDirectInsertBatchView view;
    view.vectors_ = vectors;
    view.vector_count_ = vector_count;
    view.selection_type_ = INDEX_SELECTION;
    view.row_count_ = row_count;
    view.indices_ = indices;
    return view;
  }

  bool is_valid() const
  {
    return nullptr != vectors_ && vector_count_ > 0 && row_count_ > 0
        && ((CONTIGUOUS_SELECTION == selection_type_ && offset_ >= 0)
            || (INDEX_SELECTION == selection_type_ && nullptr != indices_));
  }

  common::ObIVector *const *vectors_;
  int64_t vector_count_;
  SelectionType selection_type_;
  int64_t offset_;
  int64_t row_count_;
  const uint16_t *indices_;
};

enum ObDirectInsertWriterLayout
{
  DIRECT_INSERT_HEAP_WRITER = 0,
  DIRECT_INSERT_ORDERED_WRITER
};

enum ObDirectInsertInputFormat
{
  DIRECT_INSERT_ROW_INPUT = 0,
  DIRECT_INSERT_BATCH_INPUT
};

struct ObDirectInsertWriterRequest final
{
  ObDirectInsertWriterRequest()
    : layout_(DIRECT_INSERT_ORDERED_WRITER),
      input_format_(DIRECT_INSERT_ROW_INPUT), tablet_id_(), slice_index_(-1),
      parallel_count_(0), max_batch_size_(0), autoinc_column_index_(-1),
      idempotent_tablet_autoinc_(false), append_batch_(false),
      spool_factory_(nullptr)
  {}

  bool is_valid() const
  {
    const bool common_valid = tablet_id_.is_valid() && slice_index_ >= 0;
    const bool heap_valid = DIRECT_INSERT_HEAP_WRITER != layout_
        || (parallel_count_ > 0 && autoinc_column_index_ >= 0);
    const bool batch_valid = DIRECT_INSERT_BATCH_INPUT != input_format_
        || (max_batch_size_ > 0 && nullptr != spool_factory_);
    return common_valid && heap_valid && batch_valid;
  }

  ObDirectInsertWriterLayout layout_;
  ObDirectInsertInputFormat input_format_;
  common::ObTabletID tablet_id_;
  int64_t slice_index_;
  int64_t parallel_count_;
  int64_t max_batch_size_;
  int64_t autoinc_column_index_;
  bool idempotent_tablet_autoinc_;
  bool append_batch_;
  query::ObISpillBatchSpoolFactory *spool_factory_;
};

class ObIDirectInsertWriter
{
public:
  virtual int append_row(const ObDirectInsertRowView &row) = 0;
  virtual int append_batch(const ObDirectInsertBatchView &batch) = 0;
  virtual int close() = 0;
  virtual int64_t get_row_count() const = 0;
  virtual const common::ObTabletID &get_tablet_id() const = 0;
  virtual int64_t get_slice_index() const = 0;

protected:
  virtual ~ObIDirectInsertWriter() {}

private:
  virtual void destroy_self() = 0;
  friend class ObIDirectInsertWriterFactory;
};

class ObIDirectInsertWriterFactory
{
public:
  virtual int create(common::ObIAllocator &allocator,
                     const ObDirectInsertWriterRequest &request,
                     ObIDirectInsertWriter *&writer) = 0;
  static void destroy(ObIDirectInsertWriter *&writer);

protected:
  virtual ~ObIDirectInsertWriterFactory() {}
};

class ObIDirectInsertSession
{
public:
  virtual bool is_final() const = 0;
  virtual int prepare_ordered_input() = 0;
  virtual int complete_px_worker() = 0;
  virtual int resolve_write_policy(const ObDirectInsertPlanFacts &facts,
                                   ObDirectInsertWritePolicy &policy) const = 0;
  virtual int build_autoinc_param(ObDirectInsertAutoincScope scope,
                                  const common::ObTabletID &tablet_id,
                                  int64_t slice_index,
                                  ObDirectInsertAutoincParam &param) = 0;
  virtual int sync_tablet_autoinc(const common::ObTabletID &tablet_id,
                                  const common::ObTabletID &target_tablet_id,
                                  int64_t slice_index,
                                  int64_t row_count) = 0;
  virtual ObIDirectInsertWriterFactory &get_writer_factory() = 0;

protected:
  virtual ~ObIDirectInsertSession() {}
};

class ObDirectInsertOrchestrator final
{
public:
  static int start(common::ObIAllocator &allocator,
                   const ObDirectInsertStartParam &param,
                   ObIDirectInsertWorkerContext &worker_context,
                   ObIDirectInsertSession *&session);
  static int finish(ObIDirectInsertSession *&session);
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_DDL_OB_DIRECT_INSERT_H_
