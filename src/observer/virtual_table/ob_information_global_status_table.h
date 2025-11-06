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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_INFORMATION_GLOBAL_STATUS_TABLE_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_INFORMATION_GLOBAL_STATUS_TABLE_

#include "share/ob_virtual_table_scanner_iterator.h"
#include "src/share/ob_server_struct.h"

namespace oceanbase
{
namespace observer
{
class ObInfoSchemaGlobalStatusTable : public common::ObVirtualTableScannerIterator
{
  #define GLOBAL_STATUS_MAP_BUCKET_NUM 10

  static const int32_t GLOBAL_STATUS_COLUMN_COUNT = 2;
  enum GLOBAL_STATUS_COLUMN {
    VARIABLE_NAME = common::OB_APP_MIN_COLUMN_ID,
    VARIABLE_VALUE,
  };

  enum VARIABLE {
    THREADS_CONNECTED = 0,
    UPTIME
  };

  typedef common::hash::ObHashMap<common::ObString, common::ObObj> AllStatus;
public:
  ObInfoSchemaGlobalStatusTable();
  virtual ~ObInfoSchemaGlobalStatusTable();

  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();

  inline void set_cur_session(sql::ObSQLSessionInfo *session)
  {
    cur_session_ = session;
  }
  inline void set_global_ctx(const share::ObGlobalContext *global_ctx)
  {
    global_ctx_ = global_ctx;
  }
private:
  int fetch_all_global_status(AllStatus &all_status);
  DISALLOW_COPY_AND_ASSIGN(ObInfoSchemaGlobalStatusTable);
private:
  sql::ObSQLSessionInfo *cur_session_;
  const share::ObGlobalContext *global_ctx_;
  static const char *const variables_name[];
};
}
}

#endif // OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_INFORMATION_GLOBAL_STATUS_TABLE_
