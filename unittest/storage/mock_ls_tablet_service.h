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

#ifndef OCEANBASE_STORAGE_MOCK_LS_TABLET_SERVICE
#define OCEANBASE_STORAGE_MOCK_LS_TABLET_SERVICE

#include "storage/ls/ob_ls_tablet_service.h"

namespace oceanbase
{
namespace storage
{
// mock different ls tablet service for dml interfaces(table_scan, insert_rows...)
// just override prepare_dml_running_ctx

class MockInsertRowsLSTabletService : public ObLSTabletService
{
public:
  MockInsertRowsLSTabletService() = default;
  virtual ~MockInsertRowsLSTabletService() = default;
protected:
  virtual int prepare_dml_running_ctx(
      const common::ObIArray<uint64_t> *column_ids,
      const common::ObIArray<uint64_t> *upd_col_ids,
      ObTabletHandle &tablet_handle,
      ObDMLRunningCtx &run_ctx) override;
private:
  int prepare_relative_tables(
      const int64_t schema_version,
      const common::ObIArray<uint64_t> *upd_col_ids,
      common::ObIArray<ObTabletHandle> &tablet_handles,
      ObDMLRunningCtx &run_ctx);
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_MOCK_LS_TABLET_SERVICE
