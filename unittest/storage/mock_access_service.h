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

#ifndef OCEANBASE_STORAGE_MOCK_ACCESS_SERVICE
#define OCEANBASE_STORAGE_MOCK_ACCESS_SERVICE

#include "storage/tx_storage/ob_access_service.h"

namespace oceanbase
{
namespace storage
{
class ObLSTabletService;

class MockObAccessService : public ObAccessService
{
public:
  MockObAccessService(ObLSTabletService *tablet_service = nullptr);
  virtual ~MockObAccessService() = default;
public:
  int insert_rows(
      const share::ObLSID &ls_id,
      const common::ObTabletID &tablet_id,
      transaction::ObTxDesc &tx_desc,
      const ObDMLBaseParam &dml_param,
      const common::ObIArray<uint64_t> &column_ids,
      blocksstable::ObDatumRowIterator *row_iter,
      int64_t &affected_rows);
public:
  ObLSTabletService *tablet_service_; // different kinds of mock ls tablet service
};
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_MOCK_ACCESS_SERVICE
