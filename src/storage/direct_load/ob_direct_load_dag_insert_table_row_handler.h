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

#include "lib/allocator/page_arena.h"

namespace oceanbase
{
namespace table
{
class ObTableLoadSqlStatistics;
} // namespace table
namespace blocksstable
{
class ObDatumRow;
class ObBatchDatumRows;
} // namespace blocksstable
namespace storage
{
class ObDirectLoadInsertTabletContext;
class ObDirectLoadDagLobBuilder;
class ObDirectLoadDatumRow;

class ObDirectLoadDagInsertTableRowHandler
{
public:
  ObDirectLoadDagInsertTableRowHandler();
  ~ObDirectLoadDagInsertTableRowHandler();
  int init(ObDirectLoadInsertTabletContext *insert_tablet_ctx);
  int switch_slice(const int64_t slice_idx);
  int handle_row(blocksstable::ObDatumRow &datum_row);
  int handle_batch(blocksstable::ObBatchDatumRows &datum_rows);
  int close();

private:
  ObDirectLoadInsertTabletContext *insert_tablet_ctx_;
  table::ObTableLoadSqlStatistics *sql_statistics_;
  ObDirectLoadDagLobBuilder *lob_builder_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase
