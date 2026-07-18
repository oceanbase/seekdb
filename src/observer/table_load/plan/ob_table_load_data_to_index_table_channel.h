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

#include "observer/table_load/plan/ob_table_load_data_channel.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadRowProjector;

class ObTableLoadDataToIndexTableChannel : public ObTableLoadTableChannel
{
public:
  ObTableLoadDataToIndexTableChannel(ObTableLoadTableOp *up_table_op,
                                     ObTableLoadTableOp *down_table_op);
  virtual ~ObTableLoadDataToIndexTableChannel();
  int handle_insert_row(const ObTabletID &tablet_id,
                        const storage::ObDirectLoadDatumRow &datum_row) override;
  int handle_insert_row(const ObTabletID &tablet_id,
                        const blocksstable::ObDatumRow &datum_row) override;
  int handle_insert_batch(const ObTabletID &tablet_id,
                          const blocksstable::ObBatchDatumRows &datum_rows) override;
  int handle_delete_row(const ObTabletID &tablet_id,
                        const storage::ObDirectLoadDatumRow &datum_row) override;
  int handle_update_row(const ObTabletID &tablet_id,
                        const storage::ObDirectLoadDatumRow &datum_row) override;
  int handle_update_row(const ObTabletID &tablet_id,
                        ObIArray<const storage::ObDirectLoadExternalRow *> &rows,
                        const storage::ObDirectLoadExternalRow *result_row) override;
  int handle_update_row(ObArray<const storage::ObDirectLoadMultipleDatumRow *> &rows,
                        const storage::ObDirectLoadMultipleDatumRow *result_row) override;
  int handle_update_row(const ObTabletID &tablet_id, const storage::ObDirectLoadDatumRow &old_row,
                        const storage::ObDirectLoadDatumRow &new_row,
                        const storage::ObDirectLoadDatumRow *result_row) override;
  int handle_insert_delete_conflict(const ObTabletID &tablet_id,
                                    const storage::ObDirectLoadDatumRow &datum_row) override;
private:
  int create_row_projector() override;
  ObDirectLoadTableType::Type get_table_type() override
  {
    return ObDirectLoadTableType::EXTERNAL_TABLE;
  }
};

} // namespace observer
} // namespace oceanbase
