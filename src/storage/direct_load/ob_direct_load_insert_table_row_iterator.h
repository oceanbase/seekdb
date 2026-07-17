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

#include "storage/blocksstable/ob_datum_row.h"
#include "storage/ddl/ob_ddl_struct.h"
#include "storage/direct_load/ob_direct_load_insert_table_row_handler.h"

namespace oceanbase
{
namespace sql
{
class ObLoadDataStat;
} // namespace sql
namespace table
{
class ObTableLoadSqlStatistics;
} // namespace table
namespace storage
{
class ObDirectLoadInsertTabletContext;
class ObDirectLoadIStoreRowIterator;
class ObDirectLoadDMLRowHandler;

class ObDirectLoadInsertTableRowIterator final : public ObIDirectLoadRowIterator
{
public:
  ObDirectLoadInsertTableRowIterator();
  virtual ~ObDirectLoadInsertTableRowIterator();
  int init(ObDirectLoadInsertTabletContext *insert_tablet_ctx,
           const ObIArray<ObDirectLoadIStoreRowIterator *> &row_iters,
           ObDirectLoadDMLRowHandler *dml_row_handler,
           sql::ObLoadDataStat *job_stat);
  int get_next_row(const blocksstable::ObDatumRow *&datum_row) override;
  int get_next_row(const bool skip_lob, const blocksstable::ObDatumRow *&row) override;
  int close();
private:
  int inner_get_next_row(blocksstable::ObDatumRow *&datum_row);

protected:
  ObDirectLoadInsertTabletContext *insert_tablet_ctx_;
  const ObIArray<ObDirectLoadIStoreRowIterator *> *row_iters_;
  ObDirectLoadDMLRowHandler *dml_row_handler_;
  sql::ObLoadDataStat *job_stat_;
  ObDirectLoadInsertTableRowHandler row_handler_;
  blocksstable::ObDatumRow insert_datum_row_;
  blocksstable::ObDatumRow delete_datum_row_;
  int64_t rowkey_column_count_;
  int64_t column_count_;
  int64_t pos_;
  int64_t row_count_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase
