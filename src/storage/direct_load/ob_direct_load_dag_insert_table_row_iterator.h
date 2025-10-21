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

#include "storage/blocksstable/ob_batch_datum_rows.h"
#include "storage/blocksstable/ob_datum_row.h"
#include "storage/ddl/ob_ddl_struct.h"
#include "storage/ddl/ob_tablet_slice_row_iterator.h"
#include "storage/direct_load/ob_direct_load_batch_rows.h"
#include "storage/direct_load/ob_direct_load_dag_insert_table_row_handler.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadInsertTabletContext;
class ObDirectLoadIStoreRowIterator;
class ObDirectLoadDMLRowHandler;

class ObDirectLoadDagTabletSliceRowIterator : public ObITabletSliceRowIterator
{
protected:
  ObDirectLoadDagTabletSliceRowIterator();

public:
  virtual ~ObDirectLoadDagTabletSliceRowIterator();
  int init(ObDirectLoadInsertTabletContext *insert_tablet_ctx, const int64_t slice_idx,
           const ObIArray<ObDirectLoadIStoreRowIterator *> &row_iters,
           ObDirectLoadDMLRowHandler *dml_row_handler, bool is_delete_full_row);
  ObTabletID get_tablet_id() const override { return tablet_id_; }
  int64_t get_slice_idx() const override { return slice_idx_; }

protected:
  virtual int do_init() { return OB_SUCCESS; }
  int inner_get_next_row(blocksstable::ObDatumRow *&row);
  int close();

protected:
  ObTabletID tablet_id_;
  ObDirectLoadInsertTabletContext *insert_tablet_ctx_;
  int64_t slice_idx_;
  const ObIArray<ObDirectLoadIStoreRowIterator *> *row_iters_;
  ObDirectLoadDMLRowHandler *dml_row_handler_;
  ObDirectLoadDagInsertTableRowHandler row_handler_;
  int64_t pos_;
  int64_t row_count_;
  bool is_delete_full_row_;
  bool is_inited_;
};

class ObDirectLoadDagInsertTableRowIterator final : public ObDirectLoadDagTabletSliceRowIterator
{
public:
  ObDirectLoadDagInsertTableRowIterator();
  virtual ~ObDirectLoadDagInsertTableRowIterator() = default;
  int get_next_row(const blocksstable::ObDatumRow *&row) override;
  int get_next_batch(const blocksstable::ObBatchDatumRows *&datum_rows) override
  {
    return OB_NOT_SUPPORTED;
  }

private:
  int do_init() override;
  int inner_get_next_row(blocksstable::ObDatumRow *&row);

private:
  blocksstable::ObDatumRow insert_datum_row_;
  blocksstable::ObDatumRow delete_datum_row_;
  int64_t rowkey_column_count_;
  int64_t column_count_;
};

class ObDirectLoadDagInsertTableBatchRowIterator final
  : public ObDirectLoadDagTabletSliceRowIterator
{
public:
  ObDirectLoadDagInsertTableBatchRowIterator() = default;
  virtual ~ObDirectLoadDagInsertTableBatchRowIterator() = default;
  int init(ObDirectLoadInsertTabletContext *insert_tablet_ctx, const int64_t slice_idx,
           const ObIArray<ObDirectLoadIStoreRowIterator *> &row_iters,
           ObDirectLoadDMLRowHandler *dml_row_handler);
  int get_next_row(const blocksstable::ObDatumRow *&row) override { return OB_NOT_SUPPORTED; }
  int get_next_batch(const blocksstable::ObBatchDatumRows *&datum_rows) override;

private:
  int do_init() override;
  int inner_get_next_batch(blocksstable::ObBatchDatumRows *&datum_rows);

private:
  ObDirectLoadBatchRows batch_rows_;
  blocksstable::ObBatchDatumRows datum_rows_;
};

} // namespace storage
} // namespace oceanbase
