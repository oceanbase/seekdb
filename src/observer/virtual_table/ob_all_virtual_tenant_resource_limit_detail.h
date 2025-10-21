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

#ifndef OB_ALL_VIRTUAL_OB_TENANT_RESOURCE_LIMIT_DETAIL_H_
#define OB_ALL_VIRTUAL_OB_TENANT_RESOURCE_LIMIT_DETAIL_H_

#include "observer/omt/ob_multi_tenant_operator.h"
#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/resource_limit_calculator/ob_resource_limit_calculator.h"

namespace oceanbase
{
namespace share
{
class ObResourceInfo;
}

namespace observer
{
class ObResourceLimitDetailTable
  : public common::ObVirtualTableScannerIterator,
    public omt::ObMultiTenantOperator
{
public:
  ObResourceLimitDetailTable();
  virtual ~ObResourceLimitDetailTable();
public:
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
  int set_addr(common::ObAddr &addr);
  enum COLUMN_NAME
  {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    TENANT_ID,
    RESOURCE_NAME,
    LIMIT_TYPE,
    LIMIT_VALUE,
  };
private:
  virtual bool is_need_process(uint64_t tenant_id) override;
  virtual int process_curr_tenant(common::ObNewRow *&row) override;
  virtual void release_last_tenant() override;
  int get_next_resource_limit_val_(int64_t &val);
private:
  common::ObAddr addr_;
  char ip_buf_[common::OB_IP_STR_BUFF];
  share::ObLogicResourceStatIterator iter_;
  share::ObResourceConstraintIterator constraint_iter_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObResourceLimitDetailTable);
};

} // end namespace observer
} // end namespace oceanbase

#endif
