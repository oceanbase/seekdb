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

#ifndef OB_ALL_VIRTUAL_SQL_WORKAREA_HISTORY_STAT_H
#define OB_ALL_VIRTUAL_SQL_WORKAREA_HISTORY_STAT_H

#include "sql/engine/ob_tenant_sql_memory_manager.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/ob_virtual_table_scanner_iterator.h"
#include "common/row/ob_row.h"

namespace oceanbase
{
namespace observer
{

class ObSqlWorkareaHistoryStatIterator
{
public:
  ObSqlWorkareaHistoryStatIterator();
  ~ObSqlWorkareaHistoryStatIterator() { destroy(); }
public:
  void destroy();
  void reset();
  int init(const uint64_t effective_tenant_id);
  int get_next_wa_stat(sql::ObSqlWorkAreaStat *&wa_stat, uint64_t &tenant_id);
private:
  int get_next_batch_wa_stats();
private:
  common::ObSEArray<sql::ObSqlWorkAreaStat, 32> wa_stats_;
  common::ObSEArray<uint64_t, 16> tenant_ids_;
  int64_t cur_nth_wa_;
  int64_t cur_nth_tenant_;
};

class ObSqlWorkareaHistoryStat : public common::ObVirtualTableScannerIterator
{
public:
  ObSqlWorkareaHistoryStat();
  virtual ~ObSqlWorkareaHistoryStat() { destroy(); }

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
    OPERATION_TYPE,
    OPERATION_ID, // OB_APP_MIN_COLUMN_ID + 5
    ESTIMATED_OPTIMAL_SIZE,
    ESTIMATED_ONEPASS_SIZE,
    LAST_MEMORY_USED,
    LAST_EXECUTION,
    LAST_DEGREE,      // OB_APP_MIN_COLUMN_ID + 10
    TOTAL_EXECUTIONS,
    OPTIMAL_EXECUTIONS,
    ONEPASS_EXECUTIONS,
    MULTIPASSES_EXECUTIONS,
    ACTIVE_TIME,       // OB_APP_MIN_COLUMN_ID + 15
    MAX_TEMPSEG_SIZE,
    LAST_TEMPSEG_SIZE,
    TENAND_ID,         // OB_APP_MIN_COLUMN_ID + 18
    POLICY,
    DB_ID,
  };
  int fill_row(
    uint64_t tenant_id,
    sql::ObSqlWorkAreaStat &wa_stat,
    common::ObNewRow *&row);
  int get_server_ip_and_port();
private:
  common::ObString ipstr_;
  int32_t port_;
  ObSqlWorkareaHistoryStatIterator iter_;
};


} /* namespace observer */
} /* namespace oceanbase */

#endif /* OB_ALL_VIRTUAL_SQL_WORKAREA_HISTORY_STAT_H */
