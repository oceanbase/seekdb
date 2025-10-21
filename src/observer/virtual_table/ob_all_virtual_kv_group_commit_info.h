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
#ifndef OB_ALL_VIRTUAL_KV_GROUP_COMMIT_INFO_H_
#define OB_ALL_VIRTUAL_KV_GROUP_COMMIT_INFO_H_

#include "share/ob_virtual_table_scanner_iterator.h"
#include "observer/omt/ob_multi_tenant_operator.h"
#include "observer/table/group/ob_table_tenant_group.h"
#include "observer/table/group/ob_i_table_struct.h"
namespace oceanbase
{
namespace observer
{

class ObAllVirtualKvGroupCommitInfo : public common::ObVirtualTableScannerIterator,
                                      public omt::ObMultiTenantOperator
{
public:
  ObAllVirtualKvGroupCommitInfo()
    : ObVirtualTableScannerIterator(),
      cur_idx_(0),
      group_infos_()
  {}
  virtual ~ObAllVirtualKvGroupCommitInfo() { reset(); }
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
private:
  enum GROUP_COLUMN {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    TENANT_ID,
    GROUP_TYPE,
    LS_ID,
    TABLE_ID,
    SCHEMA_VERSION,
    QUEUE_SIZE,
    BATCH_SIZE,
    CREATE_TIME,
    UPDATE_TIME
  };
private:
  virtual bool is_need_process(uint64_t tenant_id) override;
  virtual int process_curr_tenant(common::ObNewRow *&row) override;
  virtual void release_last_tenant() override;
private:
  int64_t cur_idx_;
  char ipbuf_[common::OB_IP_STR_BUFF];
  ObSEArray<table::ObTableGroupInfo, 128> group_infos_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualKvGroupCommitInfo);
};

} /* namespace observer */
} /* namespace oceanbase */
#endif
