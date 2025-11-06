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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SESSION_WAIT_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SESSION_WAIT_

#include "share/ob_virtual_table_scanner_iterator.h"
#include "lib/stat/ob_session_stat.h"
#include "observer/virtual_table/ob_all_virtual_diag_index_scan.h"

namespace oceanbase
{
namespace common
{
class ObObj;
}

namespace observer
{

class ObAllVirtualSessionWait : public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualSessionWait();
  virtual ~ObAllVirtualSessionWait();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
  inline void set_addr(common::ObAddr &addr) {addr_ = &addr;}
  virtual int set_ip(common::ObAddr *addr);
  inline void set_session_mgr(sql::ObSQLSessionMgr *session_mgr) { session_mgr_ = session_mgr; }


protected:
  virtual int get_all_diag_info();
  inline sql::ObSQLSessionMgr* get_session_mgr() const { return session_mgr_; }
  ObWrapperAllocator alloc_wrapper_;
  common::ObSEArray<std::pair<uint64_t, common::ObDISessionCollect>, 8, ObWrapperAllocator &>
      session_status_;

private:
  enum WAIT_COLUMN
  {
    SESSION_ID = common::OB_APP_MIN_COLUMN_ID,
    SVR_IP,
    SVR_PORT,
    TENANT_ID,
    EVENT,
    P1TEXT,
    P1,
    P2TEXT,
    P2,
    P3TEXT,
    P3,
    LEVEL,
    WAIT_CLASS_ID,
    WAIT_CLASS_NO,
    WAIT_CLASS,
    STATE,
    WAIT_TIME_MICRO,
    TIME_REMAINING_MICRO,
    TIME_SINCE_LAST_WAIT_MICRO
  };
  common::ObAddr *addr_;
  common::ObString ipstr_;
  int32_t port_;
  uint32_t session_iter_;
  sql::ObSQLSessionMgr *session_mgr_;
  common::ObDISessionCollect *collect_;
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualSessionWait);
};

class ObAllVirtualSessionWaitI1 : public ObAllVirtualSessionWait, public ObAllVirtualDiagIndexScan
{
public:
  ObAllVirtualSessionWaitI1() {}
  virtual ~ObAllVirtualSessionWaitI1() {}
  virtual int inner_open() override
  {
    return set_index_ids(key_ranges_);
  }

protected:
  virtual int get_all_diag_info();
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualSessionWaitI1);
};

}
}
#endif /* OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_ALL_VIRTUAL_SESSION_WAIT */

