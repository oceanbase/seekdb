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

#define USING_LOG_PREFIX STORAGE

#include "mock_access_service.h"


using namespace oceanbase::storage;

MockObAccessService::MockObAccessService(ObLSTabletService *tablet_service)
  : tablet_service_(tablet_service)
{
}

int MockObAccessService::insert_rows(
    const share::ObLSID &ls_id,
    const common::ObTabletID &tablet_id,
    transaction::ObTxDesc &tx_desc,
    const ObDMLBaseParam &dml_param,
    const common::ObIArray<uint64_t> &column_ids,
    blocksstable::ObDatumRowIterator *row_iter,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;

  if (OB_UNLIKELY(!ls_id.is_valid())
      || OB_UNLIKELY(!tablet_id.is_valid())
      || OB_UNLIKELY(!tx_desc.is_valid())
      || OB_UNLIKELY(!dml_param.is_valid())
      || OB_UNLIKELY(column_ids.count() <= 0)
      || OB_ISNULL(row_iter)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ls_id), K(tablet_id), K(tx_desc),
             K(dml_param), K(column_ids), K(row_iter));
  } else if (OB_ISNULL(tablet_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet service is null", K(ret));
  } else if (OB_FAIL(check_write_allowed_(ls_id,
                                          tablet_id,
                                          ObStoreAccessType::MODIFY,
                                          dml_param,
                                          dml_param.timeout_,
                                          tx_desc,
                                          tablet_handle,
                                          *dml_param.store_ctx_guard_))) {
    LOG_WARN("fail to check query allowed", K(ret), K(ls_id), K(tablet_id));
  } else {
    ret = tablet_service_->insert_rows(tablet_handle,
                                       dml_param.store_ctx_guard_->get_store_ctx(),
                                       dml_param,
                                       column_ids,
                                       row_iter,
                                       affected_rows);
  }
  return ret;
}
