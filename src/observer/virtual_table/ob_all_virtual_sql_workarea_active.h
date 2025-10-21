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

#ifndef OB_ALL_VIRTUAL_SQL_WORKAREA_ACTIVE_H
#define OB_ALL_VIRTUAL_SQL_WORKAREA_ACTIVE_H

#include "sql/engine/ob_tenant_sql_memory_manager.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/ob_virtual_table_scanner_iterator.h"
#include "common/row/ob_row.h"


namespace oceanbase
{
namespace observer
{

class ObSqlWorkareaActiveIterator
{
public:
  ObSqlWorkareaActiveIterator();
  ~ObSqlWorkareaActiveIterator() { destroy(); }
public:
  void destroy();
  void reset();
  int init(const uint64_t effective_tenant_id);
  int get_next_wa_active(sql::ObSqlWorkareaProfileInfo *&wa_active, uint64_t &tenant_id);
private:
  int get_next_batch_wa_active();
private:
  common::ObSEArray<sql::ObSqlWorkareaProfileInfo, 32> wa_actives_;
  common::ObSEArray<uint64_t, 16> tenant_ids_;
  int64_t cur_nth_wa_;
  int64_t cur_nth_tenant_;
};

class ObSqlWorkareaActive : public common::ObVirtualTableScannerIterator
{
public:
  ObSqlWorkareaActive();
  virtual ~ObSqlWorkareaActive() { destroy(); }

public:
  void destroy();
  void reset();
  int inner_get_next_row(common::ObNewRow *&row);

private:
  enum STORAGE_COLUMN
  {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    PLAN_ID,
    SQL_ID,
    SQL_EXEC_ID,
    OPERATION_TYPE, // OB_APP_MIN_COLUMN_ID + 5
    OPERATION_ID,
    SID,
    ACTIVE_TIME,
    WORK_AREA_SIZE,
    EXPECTED_SIZE,     // OB_APP_MIN_COLUMN_ID + 10
    ACTUAL_MEM_USED,
    MAX_MEM_USED,
    NUMBER_PASSES,
    TEMPSEG_SIZE,
    TENAND_ID,         // OB_APP_MIN_COLUMN_ID + 15
    POLICY,
    DB_ID,
  };
  int fill_row(
    uint64_t tenant_id,
    sql::ObSqlWorkareaProfileInfo &wa_active,
    common::ObNewRow *&row);
  int get_server_ip_and_port();
private:
  common::ObString ipstr_;
  int32_t port_;
  ObSqlWorkareaActiveIterator iter_;
};

} /* namespace observer */
} /* namespace oceanbase */

#endif /* OB_ALL_VIRTUAL_SQL_WORKAREA_ACTIVE_H */
