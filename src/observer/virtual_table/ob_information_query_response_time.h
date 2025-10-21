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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_INFORMATION_QUERY_RESPONSE_TIME_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_INFORMATION_QUERY_RESPONSE_TIME_
#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/ob_tenant_mgr.h"
#include "observer/mysql/ob_query_response_time.h"
#include "observer/omt/ob_multi_tenant_operator.h"

namespace oceanbase {
namespace common {
class ObObj;

}
namespace share {
namespace schema {
class ObTableSchema;
class ObDatabaseSchema;

}  // namespace schema
}  // namespace share

namespace observer {

class ObInfoSchemaQueryResponseTimeTable : public common::ObVirtualTableScannerIterator, 
                                          public omt::ObMultiTenantOperator
{
public:
  ObInfoSchemaQueryResponseTimeTable();
  virtual ~ObInfoSchemaQueryResponseTimeTable();
  virtual int inner_open() override;
  virtual int inner_get_next_row(common::ObNewRow*& row) override;
  virtual bool is_need_process(uint64_t tenant_id) override;
  virtual int process_curr_tenant(common::ObNewRow *&row) override;
  virtual void release_last_tenant() override;
  virtual void reset() override;
  int set_ip(common::ObAddr* addr);
  inline void set_addr(common::ObAddr& addr)
  {
    addr_ = &addr;
  }
  int process_row_data(ObNewRow *&row, ObObj* cells);

private:
  enum SYS_COLUMN {
    TENANT_ID = common::OB_APP_MIN_COLUMN_ID,
    SVR_IP,
    SVR_PORT,
    QUERY_RESPPONSE_TIME,
    COUNT,
    TOTAL,
    SQL_TYPE
  };
  common::ObAddr* addr_;
  common::ObString ipstr_;
  int32_t port_;
  ObRespTimeInfoCollector time_collector_;
  int32_t utility_iter_;
  int32_t sql_type_iter_;
};

}  // namespace observer
}  // namespace oceanbase
#endif /* OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_INFORMATION_QUERY_RESPONSE_TIME_ */
