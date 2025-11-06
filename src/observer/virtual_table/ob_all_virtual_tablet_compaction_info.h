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

#ifndef SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_TABLET_COMPACTION_INFO_H_
#define SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_TABLET_COMPACTION_INFO_H_

#include "observer/virtual_table/ob_virtual_table_tablet_iter.h"
namespace oceanbase
{
namespace observer
{
class ObAllVirtualTabletCompactionInfo : public ObVirtualTableTabletIter
{
  enum COLUMN_ID_LIST
  {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    TENANT_ID,
    LS_ID,
    TABLET_ID,
    FINISH_SCN,
    WAIT_CHECK_SCN,
    MAX_RECEIVED_SCN,
    SERIALIZE_SCN_LIST,
    VALIDATED_SCN,
  };
public:
  ObAllVirtualTabletCompactionInfo();
  virtual ~ObAllVirtualTabletCompactionInfo();
public:
  virtual void reset();
private:
  virtual int process_curr_tenant(common::ObNewRow *&row) override;
private:
  char medium_info_buf_[common::OB_MAX_VARCHAR_LENGTH];
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualTabletCompactionInfo);
};

}
}
#endif /* SRC_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_TABLET_COMPACTION_INFO_H_ */
