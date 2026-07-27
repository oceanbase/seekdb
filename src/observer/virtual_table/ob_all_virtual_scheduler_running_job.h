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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SCHEDULER_RUNNING_JOB_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SCHEDULER_RUNNING_JOB_

#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"
#include "sql/session/ob_sql_session_mgr.h"
namespace oceanbase
{
namespace common
{
class ObNewRow;
class ObScanner;
}
namespace sql
{
class ObSQLSessionInfo;
}
namespace observer
{
class ObAllVirtualSchedulerRunningJob : public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualSchedulerRunningJob();
  virtual ~ObAllVirtualSchedulerRunningJob();
  inline void sesession_pool(sql::ObSQLSessionMgr *session_mgr) { session_mgr_ = session_mgr; }
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
private:
  enum SCHEDULER_RUNNING_JOB_COLUMN {
        OWNER = common::OB_APP_MIN_COLUMN_ID,
    JOB_NAME,
    JOB_SUBNAME,
    JOB_STYLE,
    DETACHED,
    SESSION_ID,
    SLAVE_PROCESS_ID,
    SLAVE_OS_PROCESS_ID,
    RUNNING_INSTANCE,
    ELAPSED_TIME,
    CPU_USED,
    DESTINATION_OWNER,
    DESTINATION,
    CREDENTIAL_OWNER,
    CREDENTIAL_NAME,
    JOB_CLASS
  };
  class FillScanner
  {
  public:
    FillScanner()
        : scanner_(NULL),
        cur_row_(NULL),
        output_column_ids_() {}
    virtual ~FillScanner(){}
    bool operator()(sql::ObSQLSessionMgr::Key key, sql::ObSQLSessionInfo *sess_info);
    int init(common::ObScanner *scanner,
             common::ObNewRow *cur_row,
             const ObIArray<uint64_t> &column_ids);
    inline void reset();
  private:
      common::ObScanner *scanner_;
      common::ObNewRow *cur_row_;
      ObSEArray<uint64_t, common::OB_PREALLOCATED_NUM> output_column_ids_;
      DISALLOW_COPY_AND_ASSIGN(FillScanner);
  };
  sql::ObSQLSessionMgr *session_mgr_;
  FillScanner fill_scanner_;
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualSchedulerRunningJob);
};
}//observer
}//oceanbase
#endif /* OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SCHEDULER_RUNNING_JOB_ */
