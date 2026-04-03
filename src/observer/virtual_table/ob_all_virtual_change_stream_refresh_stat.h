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

#ifndef OB_ALL_VIRTUAL_CHANGE_STREAM_REFRESH_STAT_H_
#define OB_ALL_VIRTUAL_CHANGE_STREAM_REFRESH_STAT_H_

#include "share/ob_virtual_table_scanner_iterator.h"
#include "observer/omt/ob_multi_tenant_operator.h"

namespace oceanbase
{
namespace observer
{

class ObAllVirtualChangeStreamRefreshStat : public common::ObVirtualTableScannerIterator,
                                             public omt::ObMultiTenantOperator
{
public:
  ObAllVirtualChangeStreamRefreshStat();
  virtual ~ObAllVirtualChangeStreamRefreshStat();

  virtual int inner_get_next_row(common::ObNewRow *&row) override;
  virtual void reset() override;

protected:
  virtual bool is_need_process(uint64_t tenant_id) override;
  virtual int process_curr_tenant(common::ObNewRow *&row) override;
  virtual void release_last_tenant() override;

private:
  enum COLUMN_ID_LIST
  {
    TENANT_ID = common::OB_APP_MIN_COLUMN_ID,
    CHANGE_STREAM_REFRESH_SCN,
    CHANGE_STREAM_MIN_DEP_LSN,
    CHANGE_STREAM_PENDING_TX_COUNT,
    CHANGE_STREAM_FETCH_TX,
    CHANGE_STREAM_FETCH_LSN,
    CHANGE_STREAM_FETCH_SCN
  };

  bool row_produced_;

  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualChangeStreamRefreshStat);
};

} // end namespace observer
} // end namespace oceanbase

#endif /* OB_ALL_VIRTUAL_CHANGE_STREAM_REFRESH_STAT_H_ */
