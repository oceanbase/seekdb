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
namespace storage
{
class ObDirectLoadMultipleDatumRow;
} // namespace storage
namespace observer
{
class ObTableLoadRowProjector;

struct ObTableLoadDataTableBuildParam
{
public:
  ObTableLoadDataTableBuildParam();
  bool is_valid() const;
  TO_STRING_KV(KP_(datum_utils), K_(table_data_desc), KP_(project), KP_(file_mgr));

public:
  const blocksstable::ObStorageDatumUtils *datum_utils_;
  storage::ObDirectLoadTableDataDesc table_data_desc_;
  ObTableLoadRowProjector *project_;
  storage::ObDirectLoadTmpFileManager *file_mgr_;
};

class ObTableLoadDataTableBuilder
{
public:
  ObTableLoadDataTableBuilder();
  virtual ~ObTableLoadDataTableBuilder();
  int init(const ObTableLoadDataTableBuildParam &param);
  int append_ack_row(const common::ObTabletID &tablet_id,
                     const ObDirectLoadDatumRow &datum_row);
  int append_delete_row(const common::ObTabletID &tablet_id,
                        const ObDirectLoadDatumRow &datum_row);
  int append_delete_row(const common::ObTabletID &tablet_id,
                        const ObDirectLoadExternalRow &row);
  int append_delete_row(const ObDirectLoadMultipleDatumRow &row);
  int close();
  int get_tables(storage::ObDirectLoadTableHandleArray &table_array,
                 storage::ObDirectLoadTableManager *table_mgr);

private:
  const blocksstable::ObStorageDatumUtils *datum_utils_;
  ObTableLoadRowProjector *project_;
  ObDirectLoadDatumRow index_row_;
  ObDirectLoadDatumRow ack_row_;
  ObDirectLoadDatumRow delete_row_;
  ObDirectLoadExternalMultiPartitionTableBuilder table_builder_;
  bool is_inited_;
};

} // namespace observer
} // namespace oceanbase
