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

#include "storage/direct_load/ob_direct_load_datum_row.h"
#include "storage/direct_load/ob_direct_load_external_multi_partition_table.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadStoreLobTableCtx;

struct ObTableLoadLobTableBuildParam
{
public:
  ObTableLoadLobTableBuildParam();
  bool is_valid() const;
  TO_STRING_KV(KP_(lob_table_ctx), KPC_(lob_column_idxs), K_(table_data_desc), KP_(file_mgr));

public:
  ObTableLoadStoreLobTableCtx *lob_table_ctx_;
  const common::ObArray<int64_t> *lob_column_idxs_;
  storage::ObDirectLoadTableDataDesc table_data_desc_;
  storage::ObDirectLoadTmpFileManager *file_mgr_;
};

class ObTableLoadLobTableBuilder
{
public:
  ObTableLoadLobTableBuilder();
  ~ObTableLoadLobTableBuilder();
  int init(const ObTableLoadLobTableBuildParam &param);
  int append_delete_row(const ObTabletID &tablet_id,
                        const ObDirectLoadDatumRow &row);
  int close();
  int get_tables(storage::ObDirectLoadTableHandleArray &table_array,
                 storage::ObDirectLoadTableManager *table_mgr);

private:
  ObTableLoadStoreLobTableCtx *lob_table_ctx_;
  const common::ObArray<int64_t> *lob_column_idxs_;
  ObDirectLoadDatumRow datum_row_;
  ObDirectLoadExternalMultiPartitionTableBuilder table_builder_;
  bool is_inited_;
};

} // namespace observer
} // namespace oceanbase
