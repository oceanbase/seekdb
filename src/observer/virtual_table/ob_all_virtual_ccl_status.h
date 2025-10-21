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

#ifndef OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_CCL_STATUS_H
#define OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_CCL_STATUS_H

#include "share/ob_virtual_table_scanner_iterator.h"
#include "observer/omt/ob_multi_tenant_operator.h"
#include "sql/plan_cache/ob_plan_cache_util.h"
#include "lib/container/ob_se_array.h"
#include "sql/ob_sql_ccl_rule_manager.h"
namespace oceanbase
{
namespace observer
{ 

struct ObCCLStatus {
  uint64_t tenant_id_;
  uint64_t ccl_rule_id_;
  ObString format_sqlid_;
  uint64_t max_concurrency_;
  uint64_t cur_concurrency_;
  TO_STRING_KV(K_(tenant_id),
               K_(ccl_rule_id),
               K_(format_sqlid),
               K_(max_concurrency),
               K_(cur_concurrency));
};

struct ObGetAllCCLStatusOp {
  explicit ObGetAllCCLStatusOp(common::ObIArray<ObCCLStatus>& tmp_ccl_status)
    : allocator_(nullptr),
      tmp_ccl_status_(tmp_ccl_status)
  {}
  int operator()(common::hash::HashMapPair<sql::ObFormatSQLIDCCLRuleKey, sql::ObCCLRuleConcurrencyValueWrapper*> &entry);
  void set_allocator(ObIAllocator *allocator) { allocator_ = allocator; }
private:
  ObIAllocator *allocator_;
  common::ObIArray<ObCCLStatus>& tmp_ccl_status_;
};


class ObAllVirtualCCLStatus : public common::ObVirtualTableScannerIterator,
                                 public omt::ObMultiTenantOperator
{
public:
ObAllVirtualCCLStatus()
    : ObVirtualTableScannerIterator(),
      cur_idx_(0),
      svr_addr_()
  {
    MEMSET(svr_ip_buf_, 0, common::OB_IP_STR_BUFF);
  }
  virtual ~ObAllVirtualCCLStatus() { reset(); }
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
  int set_svr_addr(common::ObAddr &addr);
private:
  virtual bool is_need_process(uint64_t tenant_id) override;
  virtual int process_curr_tenant(common::ObNewRow *&row) override;
  virtual void release_last_tenant() override;
private:
  enum COLUMN_ID
  {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    TENANT_ID,
    CCL_RULE_ID,
    FORMAT_SQLID,
    CURRENCT_CONCURRENCY,
    MAX_CONCURRENCY
  };
  int64_t cur_idx_;
  ObAddr svr_addr_;
  char svr_ip_buf_[common::OB_IP_STR_BUFF];
  common::ObSEArray<ObCCLStatus, 1000> tmp_ccl_status_;
  
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualCCLStatus);
};

} //end namespace observer
} //end namespace oceanbase

#endif /* OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_CCL_STATUS_H */


